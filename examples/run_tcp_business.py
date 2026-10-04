"""TCP only: submit orders while an independent thread receives results."""

import argparse
import socket
import struct
import time
from business_runner import checked_main, run_pair


EXPECTED = (
    "RECEIPT id=1 total_cents=2500", "SHIPPED id=1",
    "RECEIPT id=2 total_cents=9000", "SHIPPED id=2",
    "REJECTED id=3 reason=invalid_order",
)


def verify_orders(server, client):
    if "TCP SERVER PASS: accepted=2 total_cents=11500" not in server:
        raise RuntimeError("Server order totals differ")
    if "TCP CLIENT PASS" not in client:
        raise RuntimeError("Order client did not finish")
    for result in EXPECTED:
        if client.count(result) != 1:
            raise RuntimeError(f"Missing or duplicate result: {result}")


def fragmented_client(port):
    orders = (b"ORDER id=1 sku=101 qty=2", b"ORDER id=2 sku=102 qty=1",
              b"ORDER id=3 sku=101 qty=0")
    frames = [struct.pack("!H", len(order)) + order for order in orders]
    deadline = time.monotonic() + 5
    with socket.create_connection(("127.0.0.1", int(port)), timeout=5) as peer:
        def read_exact(count):
            result = bytearray()
            while len(result) < count:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise RuntimeError("TCP framing deadline expired")
                peer.settimeout(remaining)
                part = peer.recv(count - len(result))
                if not part:
                    raise RuntimeError("EOF in a TCP frame")
                result.extend(part)
            return result

        # Split the header and first body, then coalesce the other frames.
        peer.sendall(frames[0][:1])
        time.sleep(0.02)
        peer.sendall(frames[0][1:7])
        time.sleep(0.02)
        peer.sendall(frames[0][7:] + frames[1] + frames[2])
        replies = []
        for _ in EXPECTED:
            length = struct.unpack("!H", read_exact(2))[0]
            if not 0 < length < 128:
                raise RuntimeError("Invalid TCP reply length")
            replies.append(read_exact(length).decode("ascii"))
    if tuple(replies) != EXPECTED:
        raise RuntimeError(f"Wrong framing results: {replies}")
    text = "\n".join(replies) + "\nTCP CLIENT PASS: split/coalesced frames\n"
    print(text, end="")
    return text


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bin-dir", required=True)
    parser.add_argument("--fragmented", action="store_true")
    args = parser.parse_args()
    run_pair(args.bin_dir, "tcp_order_server",
             None if args.fragmented else "tcp_order_client", "0",
             verify_orders,
             client_action=fragmented_client if args.fragmented else None)
    print("PASS tcp_business_" + ("framing" if args.fragmented else "orders"))


if __name__ == "__main__":
    raise SystemExit(checked_main(main))
