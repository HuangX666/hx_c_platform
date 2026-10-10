"""Run the TLS JSON client and server with your certificate files."""

import argparse
from pathlib import Path
from business_runner import checked_main, json_messages, run_pair


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
    parser.add_argument("--certificate", required=True, type=Path)
    parser.add_argument("--private-key", required=True, type=Path)
    parser.add_argument("--ca", required=True, type=Path)
    args = parser.parse_args()
    run_pair(args.bin_dir, "tls_json_server", "tls_json_client", "0",
             verify, server_args=(str(args.certificate.resolve()),
                                  str(args.private_key.resolve())),
             client_args=(str(args.ca.resolve()),))


if __name__ == "__main__":
    raise SystemExit(checked_main(main))
