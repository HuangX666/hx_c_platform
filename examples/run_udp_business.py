"""UDP only: publish two sensors and observe feedback and sequence gaps."""

import argparse
import re
from business_runner import checked_main, run_pair


def verify_samples(server, client, gaps):
    if "UDP DASHBOARD PASS" not in server or "UDP SENSOR PASS" not in client:
        raise RuntimeError("UDP business threads did not finish")
    pattern = re.compile(
        r"\[UDP dashboard\] SUMMARY device=(\d+) accepted=(\d+) "
        r"newest=(\d+) gaps=(\d+) stale=(\d+) temperature_centi=(\d+)"
    )
    actual = {int(row[0]): tuple(map(int, row[1:]))
              for row in pattern.findall(server)}
    expected = {device: (8 if gaps else 10, 10, 2 if gaps else 0,
                         2 if gaps else 0, temperature)
                for device, temperature in ((101, 2510), (102, 2610))}
    if actual != expected:
        raise RuntimeError(f"Unexpected UDP state: {actual}")
    for device in (101, 102):
        if f"[UDP receive thread] SUMMARY device={device} " not in client:
            raise RuntimeError(f"Missing loopback feedback for {device}")


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
