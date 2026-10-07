"""Exercise the C TLS ABI with Python's independent standard TLS peer."""

import argparse
import ctypes as c
import json
import os
from pathlib import Path
import socket
import ssl
import struct
import subprocess
import tempfile
import threading


class Error(c.Structure):
    _fields_ = [("status", c.c_int32), ("domain", c.c_uint32),
                ("native", c.c_int64), ("cause", c.c_int64)]


class Options(c.Structure):
    _fields_ = [("size", c.c_uint32), ("version", c.c_uint32),
                ("ca", c.c_char_p), ("certificate", c.c_char_p),
                ("key", c.c_char_p), ("password", c.c_char_p),
                ("server_name", c.c_char_p), ("capacity", c.c_size_t),
                ("peers", c.c_uint32), ("handshake_ms", c.c_uint32),
                ("flags", c.c_uint32)]


class Address(c.Structure):
    _fields_ = [("bytes", c.c_ubyte * 16), ("port", c.c_uint16),
                ("family", c.c_uint16), ("scope", c.c_uint32)]


def require(status, error=None, expected=0):
    if status != expected:
        detail = f"domain={error.domain} native={error.native}" if error else ""
        raise AssertionError(f"status {status}, expected {expected}: {detail}")


class Api:
    def __init__(self, path):
        self.directory = (os.add_dll_directory(str(path.parent))
                          if os.name == "nt" else None)
        self.lib = c.CDLL(str(path))
        self.functions = {}
        signatures = {
            "options_init": [c.POINTER(Options), c.c_size_t],
            "listen": [c.c_char_p, c.c_uint16, c.POINTER(Options),
                       c.POINTER(c.c_void_p), c.POINTER(Error)],
            "connect": [c.c_char_p, c.c_uint16, c.POINTER(Options),
                        c.POINTER(c.c_void_p), c.POINTER(Error)],
            "local_address": [c.c_void_p, c.POINTER(Address), c.POINTER(Error)],
            "accept_until": [c.c_void_p, c.c_uint64, c.POINTER(c.c_uint64),
                             c.POINTER(Error)],
            "close_until": [c.POINTER(c.c_void_p), c.c_uint64,
                            c.POINTER(Error)],
            "abort_until": [c.POINTER(c.c_void_p), c.c_uint64,
                            c.POINTER(Error)],
            "flush_until": [c.c_void_p, c.c_uint64, c.c_uint64,
                            c.POINTER(Error)],
        }
        for name in ("send_until", "receive_exact_until"):
            signatures[name] = [c.c_void_p, c.c_uint64, c.c_void_p,
                                c.c_size_t, c.c_uint64, c.POINTER(c.c_size_t),
                                c.POINTER(Error)]
        for name, signature in signatures.items():
            function = getattr(self.lib, "hx_os_tls_" + name)
            function.argtypes = signature
            function.restype = c.c_int32
            self.functions[name] = function
        self.deadline = self.lib.hx_os_deadline_after_ms
        self.deadline.argtypes = [c.c_uint64]
        self.deadline.restype = c.c_uint64

    def __getattr__(self, name):
        return self.functions[name]

    def options(self, folder, server=False):
        value = Options()
        require(self.options_init(c.byref(value), c.sizeof(value)))
        value.capacity = 16384
        if server:
            value.certificate = str(folder / "tls-server.pem").encode()
            value.key = str(folder / "tls-private.pem").encode()
        else:
            value.ca = str(folder / "tls-ca.pem").encode()
        return value


def receive_native(stream, count):
    data = bytearray()
    while len(data) < count:
        block = stream.recv(count - len(data))
        if not block:
            raise AssertionError("premature native EOF")
        data.extend(block)
    return bytes(data)


def connect_native(api, folder, version):
    owner = c.c_void_p()
    error = Error()
    options = api.options(folder, True)
    require(api.listen(b"127.0.0.1", 0, c.byref(options),
                       c.byref(owner), c.byref(error)), error)
    address = Address()
    require(api.local_address(owner, c.byref(address), c.byref(error)), error)
    context = ssl.create_default_context(cafile=str(folder / "tls-ca.pem"))
    context.minimum_version = version
    context.maximum_version = version
    raw = socket.create_connection(("127.0.0.1", address.port), timeout=5)
    native = context.wrap_socket(raw, server_hostname="localhost")
    peer = c.c_uint64()
    require(api.accept_until(owner, api.deadline(5000),
                             c.byref(peer), c.byref(error)), error)
    return owner, peer.value, native


def frames(api, folder, version):
    owner, peer, native = connect_native(api, folder, version)
    try:
        messages = [{"device": "温度🌐", "id": i, "values": [1, True, None]}
                    for i in range(2)]
        bodies = [json.dumps(item, ensure_ascii=False).encode()
                  for item in messages]
        wire = b"".join(struct.pack("!I", len(body)) + body for body in bodies)
        # Three-byte writes deliberately split both headers and UTF-8 data.
        for offset in range(0, len(wire), 3):
            native.sendall(wire[offset:offset + 3])
        for expected in messages:
            header = c.create_string_buffer(4)
            count, error = c.c_size_t(), Error()
            require(api.receive_exact_until(owner, peer, header, 4,
                    api.deadline(5000), c.byref(count), c.byref(error)), error)
            length = struct.unpack("!I", header.raw)[0]
            assert length <= 4096
            body = c.create_string_buffer(length)
            require(api.receive_exact_until(owner, peer, body, length,
                    api.deadline(5000), c.byref(count), c.byref(error)), error)
            assert json.loads(body.raw) == expected
            reply = header.raw + body.raw
            require(api.send_until(owner, peer, reply, len(reply),
                    api.deadline(5000), c.byref(count), c.byref(error)), error)
            assert receive_native(native, len(reply)) == reply
        raw = native.unwrap()
        raw.close()
        require(api.close_until(c.byref(owner), api.deadline(5000), None))
        assert not owner.value
    finally:
        native.close()
        api.abort_until(c.byref(owner), api.deadline(5000), None)


def backpressure(api, folder):
    owner, peer, native = connect_native(api, folder, ssl.TLSVersion.TLSv1_3)
    try:
        payload = b"x" * (16 * 1024 * 1024)
        accepted = c.c_size_t()
        error = Error()
        require(api.send_until(owner, peer, payload, len(payload),
                api.deadline(20), c.byref(accepted), c.byref(error)),
                error, expected=-7)
        assert 0 < accepted.value < len(payload)
        require(api.flush_until(owner, peer, 0, None), expected=-7)
        received = receive_native(native, accepted.value)
        assert received == payload[:accepted.value]
        require(api.flush_until(owner, peer, api.deadline(5000), None))
        # A deadline requests graceful close but must retain the owner.
        require(api.close_until(c.byref(owner), 0, None), expected=-7)
        assert owner.value
        native.unwrap().close()
        require(api.close_until(c.byref(owner), api.deadline(5000), None))
        assert not owner.value
    finally:
        native.close()
        api.abort_until(c.byref(owner), api.deadline(5000), None)


def client_verification(api, folder, expired):
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    filename = "tls-expired.pem" if expired else "tls-server.pem"
    context.load_cert_chain(str(folder / filename),
                            str(folder / "tls-private.pem"))
    listener = socket.socket()
    listener.bind(("127.0.0.1", 0))
    listener.listen(1)
    listener.settimeout(5)
    failures = []

    def native_server():
        try:
            stream, _ = listener.accept()
            stream.settimeout(5)
            try:
                with context.wrap_socket(stream, server_side=True) as secure:
                    if expired:
                        raise AssertionError("expired certificate accepted")
                    assert receive_native(secure, 4) == b"test"
                    secure.sendall(b"pass")
                    secure.unwrap().close()
            except ssl.SSLError:
                if not expired:
                    raise
            finally:
                stream.close()
        except BaseException as error:
            failures.append(error)

    worker = threading.Thread(target=native_server)
    owner, error = c.c_void_p(), Error()
    worker.start()
    try:
        options = api.options(folder)
        # localhost may resolve IPv6 first; IPv4 fallback shares the deadline.
        status = api.connect(b"localhost", listener.getsockname()[1],
                             c.byref(options), c.byref(owner), c.byref(error))
        require(status, error, expected=-22 if expired else 0)
        if not expired:
            count = c.c_size_t()
            require(api.send_until(owner, 0, b"test", 4, api.deadline(5000),
                                   c.byref(count), c.byref(error)), error)
            reply = c.create_string_buffer(4)
            require(api.receive_exact_until(owner, 0, reply, 4,
                    api.deadline(5000), c.byref(count), c.byref(error)), error)
            assert reply.raw == b"pass"
            require(api.close_until(c.byref(owner), api.deadline(5000), None))
    finally:
        api.abort_until(c.byref(owner), api.deadline(5000), None)
        worker.join(6)
        listener.close()
        assert not worker.is_alive()
    if failures:
        raise failures[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--library", required=True, type=Path)
    parser.add_argument("--fixtures", required=True, type=Path)
    parser.add_argument("--cpp", type=Path)
    args = parser.parse_args()
    api = Api(args.library.resolve())
    with tempfile.TemporaryDirectory(prefix="hx-tls-证书-") as directory:
        folder = Path(directory)
        subprocess.run([str(args.fixtures.resolve()), "--fixtures"],
                       cwd=folder, check=True, timeout=15)
        frames(api, folder, ssl.TLSVersion.TLSv1_2)
        frames(api, folder, ssl.TLSVersion.TLSv1_3)
        backpressure(api, folder)
        client_verification(api, folder, False)
        client_verification(api, folder, True)
        if args.cpp:
            # C++ argv uses narrow CRT strings on Windows, so pass filenames
            # while cwd tests native UTF-8 path resolution independently.
            subprocess.run([str(args.cpp.resolve()), "tls-server.pem",
                            "tls-private.pem", "tls-ca.pem"], cwd=folder,
                           check=True, timeout=15)
    print("TLS interop passed:", ssl.OPENSSL_VERSION)


if __name__ == "__main__":
    main()
