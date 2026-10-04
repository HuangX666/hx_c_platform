"""Native local IPC only: two device controllers and push notifications."""

import argparse
import uuid
from business_runner import checked_main, run_pair


def verify_device(server, client):
    if "DEVICE SERVER PASS" not in server or "DEVICE CLIENT PASS" not in client:
        raise RuntimeError("Device workflow did not finish")
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
            raise RuntimeError(f"Missing device {device} steps")
        state = f"device={device} GET -> STATE interval_ms={interval}"
        if client.count(state) != 2:
            raise RuntimeError("Readback after valid/rejected SET is missing")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bin-dir", required=True)
    args = parser.parse_args()
    run_pair(args.bin_dir, "ipc_device_server", "ipc_device_client",
             f"local://business-{uuid.uuid4().hex}", verify_device)
    print("PASS ipc_business_local")


if __name__ == "__main__":
    raise SystemExit(checked_main(main))
