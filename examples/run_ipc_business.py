"""Run and verify two independent business-demo processes on local IPC."""

import argparse
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time
import uuid


def stop(process):
    if process is None or process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=3)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=3)


def ready(process, log):
    deadline = time.monotonic() + 8
    while time.monotonic() < deadline:
        with log.open("rb") as stream:
            line = stream.readline(512)
        if line.endswith(b"\n") and line.startswith(b"Listening: "):
            return line[len(b"Listening: "):].decode("utf-8").strip()
        if process.poll() is not None:
            break
        time.sleep(0.02)
    raise RuntimeError("The server did not publish its listening address")


def verify_device(server, client):
    if "DEVICE SERVER PASS" not in server or "DEVICE CLIENT PASS" not in client:
        raise RuntimeError("The complete device workflow did not finish")
    for device, interval in ((101, 250), (102, 500)):
        expected = (
            f"device={device} OPEN -> OK online",
            f"device={device} GET -> STATE interval_ms=1000",
            f"device={device} SET {interval} -> OK interval_ms={interval}",
            f"device={device} SET 0 -> ERROR interval must be 50..5000",
            f"device={device} CLOSE -> OK offline",
            f"[receive thread] device={device} CHANGED interval_ms={interval}",
        )
        if not all(line in client for line in expected):
            raise RuntimeError(f"Missing device {device} business steps")
        state = f"device={device} GET -> STATE interval_ms={interval}"
        if client.count(state) != 2:
            raise RuntimeError("Readback after valid/rejected SET is missing")
    print("CHECK: two devices, concurrent requests, independent push receiver,")
    print("       changed state, rejected input and explicit shutdown passed.")


def verify_udp(server, client, gaps):
    if "UDP DASHBOARD PASS" not in server or "UDP SENSOR PASS" not in client:
        raise RuntimeError("UDP send/receive workflow did not finish")
    summary = re.compile(
        r"\[dashboard summary\] device=(\d+) SUMMARY accepted=(\d+) "
        r"newest=(\d+) gaps=(\d+) stale=(\d+) temperature_centi=(\d+)"
    )
    observed = {int(row[0]): tuple(map(int, row[1:]))
                for row in summary.findall(server)}
    # This controlled loopback fixture expects the tiny emitted batch to
    # arrive. These assertions do not turn ordinary UDP into reliable UDP.
    expected = {
        device: (8 if gaps else 10, 10, 2 if gaps else 0,
                 2 if gaps else 0, temperature)
        for device, temperature in ((101, 2510), (102, 2610))
    }
    if observed != expected:
        raise RuntimeError(f"Unexpected telemetry state: {observed}")
    for device in (101, 102):
        if f"[feedback thread] device={device} SUMMARY " not in client:
            raise RuntimeError(f"Device {device} received no final feedback")
    print("CHECK: two sensors, separate send/receive threads "
          "and feedback passed.")
    if gaps:
        print("CHECK: deliberate omissions, duplicates "
              "and late samples handled.")


def run(args):
    suffix = ".exe" if sys.platform == "win32" else ""
    names = ("ipc_udp_dashboard", "ipc_udp_sensor") if args.transport == "udp" \
        else ("ipc_device_server", "ipc_device_client")
    server, client = [args.bin_dir.resolve() / ("hx_os_" + name + suffix)
                      for name in names]
    for path in (server, client):
        if not path.is_file():
            raise RuntimeError(f"Build the example executable first: {path}")
    address = (f"local://business-{uuid.uuid4().hex}"
               if args.transport == "local"
               else f"{args.transport}://127.0.0.1:0")
    process = None
    with tempfile.TemporaryDirectory(prefix="hx-ipc-business-") as temporary:
        output = Path(temporary) / "server.out"
        errors = Path(temporary) / "server.err"
        try:
            # Files prevent a verbose server from filling an unread pipe.
            with output.open("wb") as stdout, errors.open("wb") as stderr:
                process = subprocess.Popen(
                    [str(server), address], stdout=stdout, stderr=stderr)
            actual = ready(process, output)
            print(f"Transport: {args.transport}; address: {actual}", flush=True)
            command = [str(client), actual]
            if args.simulate_gaps:
                command.append("--simulate-gaps")
            result = subprocess.run(
                command, capture_output=True, text=True, encoding="utf-8",
                errors="replace", timeout=25,
            )
            client_text = result.stdout
            client_error = result.stderr
            print("--- client business log ---\n" + client_text, end="")
            if client_error:
                print(client_error, file=sys.stderr, end="")
            if result.returncode:
                raise RuntimeError(f"Client exit code: {result.returncode}")
            process.wait(timeout=10)
            if process.returncode:
                raise RuntimeError(f"Server exit code: {process.returncode}")
            server_text = output.read_text(encoding="utf-8", errors="replace")
            if args.transport == "udp":
                verify_udp(server_text, client_text, args.simulate_gaps)
            else:
                verify_device(server_text, client_text)
        finally:
            stop(process)
            print("--- server business log ---")
            if output.exists():
                print(output.read_text(encoding="utf-8", errors="replace"),
                      end="")
            if errors.exists():
                print(errors.read_text(encoding="utf-8", errors="replace"),
                      file=sys.stderr, end="")
    print(f"PASS ipc_business_{args.transport}"
          + ("_gaps" if args.simulate_gaps else ""))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("transport", choices=("local", "tcp", "udp", "kcp"))
    parser.add_argument("--bin-dir", type=Path, required=True)
    parser.add_argument("--simulate-gaps", action="store_true")
    args = parser.parse_args()
    if args.simulate_gaps and args.transport != "udp":
        parser.error("--simulate-gaps is only for the ordinary UDP workflow")
    try:
        run(args)
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
