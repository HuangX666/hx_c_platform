"""Run the shipped IPC programs and an independent wire-protocol peer."""

import argparse
import importlib.util
from pathlib import Path
import queue
import secrets
import socket
import subprocess
import sys
import threading
import time
import uuid
from urllib.parse import urlsplit

sys.dont_write_bytecode = True


def launch(server, address, count):
    process = subprocess.Popen(
        [server, address, str(count)], stdout=subprocess.PIPE,
        stderr=subprocess.PIPE)
    ready = queue.Queue(maxsize=1)
    reader = threading.Thread(
        target=lambda: ready.put(process.stdout.readline()))
    reader.start()
    try:
        line = ready.get(timeout=5).decode("utf-8").strip()
        if not line.startswith("Listening: "):
            raise RuntimeError(f"Server did not start: {line}")
        return process, line[len("Listening: "):]
    except BaseException:
        stop(process)
        raise
    finally:
        reader.join(timeout=5)
        if reader.is_alive():
            raise RuntimeError("Server output reader did not retire")


def stop(process):
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
    process.communicate(timeout=3)


def finished(process, count):
    output, errors = process.communicate(timeout=10)
    if process.returncode or f"Handled {count}".encode() not in output:
        raise RuntimeError(
            f"Server failed: {output!r}, {errors!r}, {process.returncode}")


def example(server, client, scheme, count):
    address = (f"local://example-{uuid.uuid4().hex}"
               if scheme == "local" else f"{scheme}://127.0.0.1:0")
    process, actual = launch(server, address, count)
    try:
        result = subprocess.run(
            [client, actual], capture_output=True, timeout=20)
        if result.returncode:
            raise RuntimeError(f"Client failed: {result.stderr!r}")
        if b"Reply:" not in result.stdout:
            raise RuntimeError("Client did not display a reply")
        finished(process, count)
        print(f"PASS {scheme}: {Path(client).name}, {count} requests")
    finally:
        stop(process)


def independent_peer(server, protocol, address):
    process, actual = launch(server, address, 2)
    peer = None
    try:
        peer = protocol.Peer(actual)
        first = protocol.frame(
            protocol.REQUEST, peer.local, peer.remote, b"first",
            message=71, channel=3, encoding=1)
        second = protocol.frame(
            protocol.REQUEST, peer.local, peer.remote,
            "second: 中文😀".encode(), message=72, channel=9, encoding=1)
        # Split a header, then combine the remainder with a second frame.
        peer.send(first[:3])
        time.sleep(0.005)
        peer.send(first[3:63])
        time.sleep(0.005)
        peer.send(first[63:] + second)
        replies = {}
        while len(replies) < 2:
            header, body = peer.receive()
            if header[2] == protocol.REPLY:
                replies[header[10]] = (header[9], body)
                peer.send(protocol.frame(
                    protocol.ACK, peer.local, peer.remote,
                    message=header[8]))
        if replies != {71: (3, b"first"),
                       72: (9, "second: 中文😀".encode())}:
            raise RuntimeError(f"Reply correlation failed: {replies!r}")
        finished(process, 2)
        print("PASS independent Python peer: fragmented/coalesced frames")
    finally:
        if peer:
            peer.close()
        stop(process)


def invalid_peer(server, protocol, truncated):
    process, actual = launch(server, "tcp://127.0.0.1:0", 1)
    bad = good = None
    try:
        bad = protocol.Peer(actual)
        if truncated:
            wire = protocol.frame(protocol.DATA, bad.local, bad.remote,
                                  b"unfinished", message=50)
            bad.send(wire[:-3])
        else:
            header = list(protocol.HEADER.unpack(protocol.frame(
                protocol.DATA, bad.local, bad.remote, message=50)))
            header[4] = 0xffffffff
            bad.send(protocol.HEADER.pack(*header))
        bad.close()
        bad = None
        good = protocol.Peer(actual)
        if good.request("still alive") != "still alive":
            raise RuntimeError("Invalid peer affected another connection")
        finished(process, 1)
        print(f"PASS incomplete/oversized message (truncated={truncated})")
    finally:
        if bad:
            bad.close()
        if good:
            good.close()
        stop(process)


def udp_sessions(server, protocol):
    process, actual = launch(server, "udp://127.0.0.1:0", 2)
    address = urlsplit(actual)
    peer = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    peer.settimeout(5)
    peer.connect((address.hostname, address.port))

    def receive(local, kind, message=0):
        deadline = time.monotonic() + 5
        while True:
            peer.settimeout(max(0.001, deadline - time.monotonic()))
            if time.monotonic() >= deadline:
                raise TimeoutError("UDP session test expired")
            data = peer.recv(65507)
            header = protocol.HEADER.unpack(data[:64])
            if header[7] != local:
                continue
            if header[2] == protocol.PING:
                peer.send(protocol.frame(protocol.PONG, local, header[6]))
            if header[2] == kind and (not message or header[10] == message):
                return header, data[64:]

    def handshake(local):
        peer.send(protocol.frame(protocol.HELLO, local, 0, auxiliary=65536))
        header, _ = receive(local, protocol.WELCOME)
        peer.send(protocol.frame(protocol.PONG, local, header[6],
                                 auxiliary=1))
        receive(local, protocol.PONG)
        return header[6]

    try:
        old = secrets.randbits(64) or 1
        old_remote = handshake(old)
        first = protocol.frame(protocol.REQUEST, old, old_remote, b"old",
                               message=1)
        peer.send(first)
        if receive(old, protocol.REPLY, 1)[1] != b"old":
            raise RuntimeError("Initial UDP request failed")
        current = old ^ 0x5a5a5a5a5a5a5a5a
        new_remote = handshake(current)
        if new_remote == old_remote:
            raise RuntimeError("UDP server reused a session nonce")
        peer.send(first)
        peer.send(protocol.frame(protocol.HELLO, old, 0, auxiliary=65536))
        peer.send(protocol.frame(protocol.REQUEST, current, new_remote,
                                 b"new", message=1))
        if receive(current, protocol.REPLY, 1)[1] != b"new":
            raise RuntimeError("Stale UDP traffic replaced the new session")
        finished(process, 2)
        print("PASS UDP restart: stale HELLO/data and reused request ID")
    finally:
        peer.close()
        stop(process)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server", required=True)
    parser.add_argument("--client", required=True)
    parser.add_argument("--threads")
    parser.add_argument("--python-peer", required=True)
    parser.add_argument("--kcp", action="store_true")
    parser.add_argument("--local", action="store_true")
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location("ipc_peer", args.python_peer)
    protocol = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(protocol)
    schemes = ["tcp", "udp"]
    if args.kcp:
        schemes.append("kcp")
    if args.local:
        schemes.append("local")
    for scheme in schemes:
        example(args.server, args.client, scheme, 1)
        if args.threads and scheme != "udp":
            example(args.server, args.threads, scheme, 20)
    independent_peer(args.server, protocol, "tcp://127.0.0.1:0")
    if args.local and sys.platform != "win32":
        independent_peer(args.server, protocol,
                         f"local://python-{uuid.uuid4().hex}")
    invalid_peer(args.server, protocol, False)
    invalid_peer(args.server, protocol, True)
    udp_sessions(args.server, protocol)


if __name__ == "__main__":
    main()
