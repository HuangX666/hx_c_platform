"""Exercise desktop inhibition against an isolated bus and fake services."""

import argparse
import ctypes as ct
import ctypes.util
import errno
import os
from pathlib import Path
import select
import shutil
import subprocess
import tempfile
import time


def bind(library, name, result, *arguments):
    function = getattr(library, name)
    function.restype = result
    function.argtypes = list(arguments)
    return function


class Service:
    def __init__(self, library, backend, mode):
        self.bus = ct.c_void_p()
        self.backend = backend
        self.mode = mode
        self.calls = 0
        self.leases = set()
        self.maximum = 0
        self.unicode_seen = False
        self.failures = []
        self.disconnect = False
        pointer = ct.c_void_p
        string = ct.c_char_p
        callback_type = ct.CFUNCTYPE(ct.c_int, pointer, pointer, pointer)

        self.open = bind(library, "sd_bus_open_user", ct.c_int,
                         ct.POINTER(pointer))
        self.close = bind(library, "sd_bus_close", None, pointer)
        self.unref = bind(library, "sd_bus_unref", pointer, pointer)
        self.process = bind(library, "sd_bus_process", ct.c_int,
                            pointer, ct.POINTER(pointer))
        self.wait = bind(library, "sd_bus_wait", ct.c_int,
                         pointer, ct.c_uint64)
        self.read = bind(library, "sd_bus_message_read", ct.c_int,
                         pointer, string)
        self.signature = bind(library, "sd_bus_message_get_signature",
                              string, pointer, ct.c_int)
        self.interface = bind(library, "sd_bus_message_get_interface",
                              string, pointer)
        self.member = bind(library, "sd_bus_message_get_member",
                           string, pointer)
        self.sender = bind(library, "sd_bus_message_get_sender",
                           string, pointer)
        self.auto_start = bind(library, "sd_bus_message_get_auto_start",
                               ct.c_int, pointer)
        self.interactive = bind(
            library, "sd_bus_message_get_allow_interactive_authorization",
            ct.c_int, pointer)
        self.reply = bind(library, "sd_bus_reply_method_return",
                          ct.c_int, pointer, string)
        self.error = bind(library, "sd_bus_reply_method_errorf",
                          ct.c_int, pointer, string, string)
        self.callback = callback_type(self.dispatch)
        add_filter = bind(library, "sd_bus_add_filter", ct.c_int,
                          pointer, ct.POINTER(pointer), callback_type, pointer)
        add_match = bind(library, "sd_bus_add_match", ct.c_int, pointer,
                         ct.POINTER(pointer), string, callback_type, pointer)
        request_name = bind(library, "sd_bus_request_name", ct.c_int,
                            pointer, string, ct.c_uint64)
        exit_on_disconnect = bind(library, "sd_bus_set_exit_on_disconnect",
                                  ct.c_int, pointer, ct.c_int)

        try:
            self.check(self.open(ct.byref(self.bus)))
            self.check(exit_on_disconnect(self.bus, 0))
            self.check(add_filter(self.bus, None, self.callback, None))
            match = (b"type='signal',interface='org.freedesktop.DBus',"
                     b"member='NameOwnerChanged'")
            self.check(add_match(self.bus, None, match, self.callback, None))
            if backend != "none":
                name = {
                    "fdo": b"org.freedesktop.PowerManagement",
                    "alias": b"org.freedesktop.PowerManagement.Inhibit",
                    "gnome": b"org.gnome.SessionManager",
                }[backend]
                self.check(request_name(self.bus, name, 0))
        except BaseException:
            self.destroy()
            raise

    @staticmethod
    def check(result):
        if result < 0:
            raise RuntimeError("sd-bus error: " + os.strerror(-result))

    def dispatch(self, message, _user, _error):
        try:
            return self.handle(message)
        except BaseException as error:
            self.failures.append(str(error))
            return -errno.EIO

    def handle(self, message):
        interface = self.interface(message)
        member = self.member(message)
        if interface == b"org.freedesktop.DBus":
            if member == b"NameOwnerChanged":
                name, old, new = ct.c_char_p(), ct.c_char_p(), ct.c_char_p()
                self.check(self.read(message, b"sss", ct.byref(name),
                                     ct.byref(old), ct.byref(new)))
                if old.value and not new.value:
                    self.leases.discard(name.value)
                return 1
            return 0

        expected = (b"org.gnome.SessionManager" if self.backend == "gnome"
                    else b"org.freedesktop.PowerManagement.Inhibit")
        if interface != expected or member != b"Inhibit":
            return 0

        assert self.auto_start(message) == 0, "Autostart was requested"
        assert self.interactive(message) == 0, "Authorization prompt requested"
        application, reason = ct.c_char_p(), ct.c_char_p()
        if self.backend == "gnome":
            ignored, flags = ct.c_uint32(), ct.c_uint32()
            assert self.signature(message, 1) == b"susu"
            self.check(self.read(message, b"susu", ct.byref(application),
                                 ct.byref(ignored), ct.byref(reason),
                                 ct.byref(flags)))
            assert ignored.value == 0
            assert flags.value == 4, "Must not inhibit idle or screen lock"
        else:
            assert self.signature(message, 1) == b"ss"
            self.check(self.read(message, b"ss", ct.byref(application),
                                 ct.byref(reason)))

        assert application.value and reason.value
        text = reason.value.decode("utf-8")
        self.unicode_seen |= "有限分配测试" in text
        self.calls += 1
        if self.mode == "denied":
            self.check(self.error(message,
                                 b"org.freedesktop.DBus.Error.AccessDenied",
                                 b"Controlled rejection"))
        elif self.mode == "malformed":
            self.leases.add(self.sender(message))
            self.check(self.reply(message, b"s", ct.c_char_p(b"wrong")))
        elif self.mode == "timeout":
            self.leases.add(self.sender(message))
        elif self.mode == "disconnect":
            self.disconnect = True
        else:
            self.leases.add(self.sender(message))
            self.maximum = max(self.maximum, len(self.leases))
            self.check(self.reply(message, b"u", ct.c_uint32(7)))
        return 1

    def step(self):
        result = self.process(self.bus, None)
        if result < 0:
            if self.disconnect:
                return
            self.check(result)
        elif result == 0:
            self.check(self.wait(self.bus, 10000))

    def destroy(self):
        if self.bus:
            self.close(self.bus)
            self.unref(self.bus)
            self.bus = ct.c_void_p()


def finish(process):
    if process and process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=2)


def run_case(program, daemon_path, library, backend, mode, profile="c"):
    daemon = client = service = None
    previous = os.environ.get("DBUS_SESSION_BUS_ADDRESS")
    with tempfile.TemporaryDirectory(prefix="hx-desktop-") as directory:
        try:
            daemon = subprocess.Popen(
                [daemon_path, "--session", "--nofork", "--print-address=1",
                 "--address=unix:path=" + str(Path(directory) / "bus")],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            ready, _, _ = select.select([daemon.stdout], [], [], 5)
            assert ready, "Private bus did not start"
            address = daemon.stdout.readline().strip()
            assert address.startswith("unix:"), address
            os.environ["DBUS_SESSION_BUS_ADDRESS"] = address
            service = Service(library, backend, mode)
            environment = os.environ.copy()
            environment["HX_OS_TEST_DESKTOP"] = "1"
            if mode == "address":
                environment["DBUS_SESSION_BUS_ADDRESS"] = (
                    "tcp:host=127.0.0.1,port=1")
            arguments = [str(program)]
            if mode != "ok":
                arguments.append(mode)
            started = time.monotonic()
            client = subprocess.Popen(arguments, stdout=subprocess.PIPE,
                                      stderr=subprocess.PIPE, text=True,
                                      env=environment, cwd=directory)
            limit = started + 20
            while client.poll() is None:
                assert time.monotonic() < limit, "Client hung"
                service.step()
                if service.disconnect:
                    finish(daemon)
                    time.sleep(0.01)

            output, error = client.communicate(timeout=2)
            assert client.returncode == 0, output + error
            assert not service.failures, service.failures
            if mode != "disconnect":
                until = time.monotonic() + 2
                while service.leases and time.monotonic() < until:
                    service.step()
                assert not service.leases, "Leases remained after client exit"
            if mode == "ok":
                if profile != "c":
                    assert service.calls >= 1
                else:
                    assert service.maximum >= 2, "Consumers not independent"
                    assert service.unicode_seen, "UTF-8 reason not observed"
                    assert service.calls >= 5
                assert "skipped" not in output
            elif mode in ("absent", "address"):
                assert service.calls == 0
            else:
                assert service.calls == 1
                assert "Handled desktop failure:" in output
            if mode == "timeout":
                assert 0.75 <= time.monotonic() - started < 5
            label = profile + "/" + backend + "/" + mode
            print(label + ": passed", flush=True)
        finally:
            finish(client)
            if service:
                service.destroy()
            finish(daemon)
            if previous is None:
                os.environ.pop("DBUS_SESSION_BUS_ADDRESS", None)
            else:
                os.environ["DBUS_SESSION_BUS_ADDRESS"] = previous


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--program", required=True, type=Path)
    parser.add_argument("--cpp-program", type=Path)
    parser.add_argument("--missing-runtime", type=Path)
    arguments = parser.parse_args()
    library_path = (ctypes.util.find_library("systemd") or
                    ctypes.util.find_library("elogind"))
    daemon = shutil.which("dbus-daemon")
    if not library_path or not daemon:
        print("Skipped: this test needs an sd-bus runtime and dbus-daemon")
        return 77

    library = ct.CDLL(library_path)
    for backend in ("fdo", "alias", "gnome"):
        run_case(arguments.program, daemon, library, backend, "ok")
        if arguments.cpp_program:
            run_case(arguments.cpp_program, daemon, library, backend,
                     "ok", profile="cpp")
    for mode in ("absent", "denied", "malformed", "timeout", "disconnect",
                 "address"):
        backend = "none" if mode == "absent" else "fdo"
        run_case(arguments.program, daemon, library, backend, mode)

    if arguments.missing_runtime:
        environment = os.environ.copy()
        environment["LD_LIBRARY_PATH"] = (
            str(arguments.missing_runtime) + os.pathsep +
            environment.get("LD_LIBRARY_PATH", ""))
        result = subprocess.run([str(arguments.program), "absent"],
                                env=environment, capture_output=True,
                                text=True, timeout=10, check=True)
        assert "Handled desktop failure:" in result.stdout
        print("Missing runtime symbols: passed", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
