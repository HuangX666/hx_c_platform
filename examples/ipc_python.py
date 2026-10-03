"""Independent standard-library TCP/Unix peer for the HXIP v1 protocol.

Start hx_os_ipc_server first, then run this file with its printed address.
The C library is not loaded by this Python example.
"""

import argparse
import secrets
import socket
import struct
import time
from urllib.parse import urlsplit

HEADER = struct.Struct(">4sBBHIIQQQQQQ")
HELLO, WELCOME, DATA, REQUEST, REPLY, ACK = range(1, 7)
PING, PONG, CLOSE, KCP_PACKET = range(7, 11)
MAX_MESSAGE = 65536


def frame(kind, source, destination, body=b"", message=0, channel=0,
          auxiliary=0, status=0, encoding=0):
    return HEADER.pack(
        b"HXIP", 1, kind, encoding, len(body), status & 0xffffffff,
        source, destination, message, channel, auxiliary, 0) + body


class Peer:
    def __init__(self, address, timeout=5):
        endpoint = urlsplit(address)
        if endpoint.scheme == "tcp":
            if endpoint.hostname not in ("127.0.0.1", "::1"):
                raise ValueError("This example only connects to loopback")
            family = (socket.AF_INET6 if endpoint.hostname == "::1"
                      else socket.AF_INET)
            self.socket = socket.socket(family, socket.SOCK_STREAM)
            target = (endpoint.hostname, endpoint.port)
        elif endpoint.scheme == "unix":
            self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            target = endpoint.path
        else:
            raise ValueError("Use tcp:// or unix:// for this Python example")
        self.timeout = timeout
        self.deadline = time.monotonic() + timeout
        self.local = secrets.randbits(64) or 1
        self.remote = 0
        self.serial = 0
        self.socket.settimeout(timeout)
        try:
            self.socket.connect(target)
            self.send(frame(HELLO, self.local, 0, auxiliary=MAX_MESSAGE))
            while True:
                header, _ = self.receive()
                if header[2] == WELCOME:
                    self.remote = header[6]
                    self.send(frame(PONG, self.local, self.remote,
                                    auxiliary=1))
                elif header[2] == PONG and header[10] == 2:
                    break
        except BaseException:
            self.socket.close()
            raise

    def _remaining(self):
        remaining = self.deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError("IPC deadline expired")
        self.socket.settimeout(remaining)

    def send(self, data):
        self._remaining()
        self.socket.sendall(data)

    def _read_exact(self, length):
        data = bytearray()
        while len(data) < length:
            self._remaining()
            part = self.socket.recv(length - len(data))
            if not part:
                raise EOFError("Peer closed before a complete frame")
            data.extend(part)
        return bytes(data)

    def receive(self):
        while True:
            header = HEADER.unpack(self._read_exact(HEADER.size))
            if header[0] != b"HXIP" or header[1] != 1 or header[11]:
                raise ValueError("Invalid HXIP header")
            if header[4] > MAX_MESSAGE:
                raise ValueError("Peer exceeded the message limit")
            if header[7] != self.local:
                raise ValueError("Message addressed to another session")
            if self.remote and header[6] != self.remote:
                raise ValueError("Message from another session")
            body = self._read_exact(header[4])
            if header[2] == PING:
                self.send(frame(PONG, self.local, header[6],
                                message=header[8]))
                continue
            return header, body

    def request(self, text):
        self.deadline = time.monotonic() + self.timeout
        self.serial += 1
        self.send(frame(REQUEST, self.local, self.remote,
                        text.encode("utf-8"), message=self.serial,
                        encoding=1))
        while True:
            header, body = self.receive()
            if header[2] == ACK and header[8] == self.serial and header[5]:
                status = header[5] - (1 << 32)
                raise RuntimeError(f"Peer rejected request: {status}")
            if header[2] == REPLY and header[10] == self.serial:
                self.send(frame(ACK, self.local, self.remote,
                                message=header[8]))
                return body.decode("utf-8")

    def close(self):
        self.socket.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("address")
    parser.add_argument("text", nargs="?", default="Hello from Python")
    args = parser.parse_args()
    peer = Peer(args.address)
    try:
        print(f"Reply: {peer.request(args.text)}")
    finally:
        peer.close()


if __name__ == "__main__":
    main()
