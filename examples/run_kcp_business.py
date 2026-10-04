"""KCP only: send ordered blocks and confirm application commit over UDP."""

import argparse
from business_runner import checked_main, run_pair


def verify_transfer(server, client, dropping):
    count = 1 if dropping else 0
    if f"KCP SERVER PASS: blocks=3 total=60 dropped={count}" not in server:
        raise RuntimeError("Server did not apply all blocks exactly once")
    if f"KCP CLIENT PASS: 3 blocks committed; dropped={count}" not in client:
        raise RuntimeError("Client did not receive commit confirmation")
    for index in range(1, 4):
        if server.count(f"apply BLOCK id={index} value={index * 10}") != 1:
            raise RuntimeError("Duplicated or missing application block")
        if client.count(f"APPLIED id={index}") != 1:
            raise RuntimeError("Duplicated or missing confirmation")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bin-dir", required=True)
    parser.add_argument("--drop-first", action="store_true")
    args = parser.parse_args()
    flags = ("--drop-first",) if args.drop_first else ()
    run_pair(args.bin_dir, "kcp_transfer_server", "kcp_transfer_client", "0",
             lambda server, client: verify_transfer(
                 server, client, args.drop_first),
             server_args=flags, client_args=flags)
    print("PASS kcp_business_" + ("loss" if args.drop_first else "transfer"))


if __name__ == "__main__":
    raise SystemExit(checked_main(main))
