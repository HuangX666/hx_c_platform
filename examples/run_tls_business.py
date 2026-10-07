"""Run the two TLS JSON programs with a freshly generated local test CA."""

import argparse
from pathlib import Path
import subprocess
import tempfile
from business_runner import checked_main, executable, json_messages, run_pair


def verify(server, client):
    readings = json_messages(server, "[TLS receive] ")
    responses = json_messages(client, "[TLS receive] ")
    expected = [{"type": "accepted", "id": 1},
                {"type": "accepted", "id": 2},
                {"type": "alarm", "id": 2, "reason": "too_hot"},
                {"type": "accepted", "id": 3}]
    if len(readings) != 3 or responses != expected:
        raise RuntimeError("Unexpected TLS JSON business results")
    if [item["celsius"] for item in readings] != [20, 81, 22]:
        raise RuntimeError("Telemetry values differ")
    if any(item["device"] != "温度传感器🌐" for item in readings):
        raise RuntimeError("UTF-8 device names differ")
    if "TLS SERVER PASS" not in server or "TLS CLIENT PASS" not in client:
        raise RuntimeError("TLS session did not finish")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bin-dir", required=True)
    args = parser.parse_args()
    generator = executable(args.bin_dir, "test_tls")
    with tempfile.TemporaryDirectory(prefix="hx-tls-demo-") as directory:
        root = Path(directory)
        subprocess.run([str(generator), "--fixtures"], cwd=root,
                       check=True, timeout=15)
        run_pair(args.bin_dir, "tls_json_server", "tls_json_client", "0",
                 verify, server_args=(str(root / "tls-server.pem"),
                                      str(root / "tls-private.pem")),
                 client_args=(str(root / "tls-ca.pem"),))


if __name__ == "__main__":
    raise SystemExit(checked_main(main))
