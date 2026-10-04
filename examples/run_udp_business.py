"""UDP only: publish two sensors and observe feedback and sequence gaps."""

import argparse
from business_runner import checked_main, json_messages, run_pair


def verify_samples(server, client, gaps):
    if "UDP DASHBOARD PASS" not in server or "UDP SENSOR PASS" not in client:
        raise RuntimeError("UDP business threads did not finish")
    expected = [{"type": "summary", "device_id": device,
                 "accepted": 8 if gaps else 10, "newest": 10,
                 "gaps": 2 if gaps else 0, "stale": 2 if gaps else 0,
                 "temperature_centi": temperature}
                for device, temperature in ((101, 2510), (102, 2610))]
    if json_messages(server, "[UDP summary] ") != expected:
        raise RuntimeError("Unexpected JSON sensor state")
    feedback = json_messages(client, "[UDP receive thread] ")
    for summary in expected:
        if feedback.count(summary) != 1:
            raise RuntimeError("Missing loopback JSON summary")
    samples = json_messages(server, "[UDP dashboard] ")
    if len(samples) != (16 if gaps else 20):
        raise RuntimeError("Wrong accepted JSON sample count")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bin-dir", required=True)
    parser.add_argument("--simulate-gaps", action="store_true")
    args = parser.parse_args()
    run_pair(args.bin_dir, "udp_dashboard", "udp_sensor", "0",
             lambda server, client: verify_samples(
                 server, client, args.simulate_gaps),
             client_args=("--simulate-gaps",) if args.simulate_gaps else ())
    print("PASS udp_business_" + ("gaps" if args.simulate_gaps else "samples"))


if __name__ == "__main__":
    raise SystemExit(checked_main(main))
