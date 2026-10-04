"""Native local IPC only: two device controllers and push notifications."""

import argparse
import uuid
from business_runner import checked_main, json_messages, run_pair


def verify_device(server, client):
    if "DEVICE SERVER PASS" not in server or "DEVICE CLIENT PASS" not in client:
        raise RuntimeError("Device workflow did not finish")
    replies = json_messages(client, "[IPC reply] ")
    notices = json_messages(client, "[IPC notification] ")
    if len(replies) != 15 or len(notices) != 2:
        raise RuntimeError("Missing or duplicate JSON replies/notifications")
    for device, interval in ((101, 250), (102, 500)):
        actual = [reply for reply in replies
                  if reply.get("device_id") == device]
        stages = (("opened", 1000), ("state", 1000), ("updated", interval),
                  ("state", interval), ("error", interval),
                  ("state", interval), ("closed", interval))
        expected = [{"type": kind, "device_id": device, "interval_ms": value,
                     "ok": kind != "error",
                     "code": "invalid_interval" if kind == "error" else ""}
                    for kind, value in stages]
        if actual != expected:
            raise RuntimeError(f"Wrong JSON device sequence: {actual}")
        notice = {"type": "changed", "device_id": device,
                  "interval_ms": interval}
        if notices.count(notice) != 1:
            raise RuntimeError("Wrong JSON notification")
    if replies[-1] != {"type": "bye", "device_id": 0, "ok": True,
                       "interval_ms": 0, "code": ""}:
        raise RuntimeError("Missing JSON shutdown reply")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bin-dir", required=True)
    args = parser.parse_args()
    run_pair(args.bin_dir, "ipc_device_server", "ipc_device_client",
             f"local://business-{uuid.uuid4().hex}", verify_device)
    print("PASS ipc_business_local")


if __name__ == "__main__":
    raise SystemExit(checked_main(main))
