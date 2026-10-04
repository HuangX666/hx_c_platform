"""TCP JSON orders, framed UTF-8 messages and invalid-input checks."""

import argparse
import json
import socket
import struct
import time
from business_runner import checked_main, json_messages, run_pair


NOTE = '桌面 "设备"\\A🌐'
EXPECTED = [
    {"type": "receipt", "id": 1, "ok": True,
     "total_cents": 2500, "note": NOTE},
    {"type": "shipped", "id": 1, "ok": True},
    {"type": "receipt", "id": 2, "ok": True,
     "total_cents": 9000, "note": NOTE},
    {"type": "shipped", "id": 2, "ok": True},
    {"type": "rejected", "id": 3, "ok": False, "reason": "invalid_order"},
]


def verify_orders(server, client, expected=EXPECTED):
    if "TCP SERVER PASS: accepted=2 total_cents=11500" not in server:
        raise RuntimeError("Server order totals differ")
    if "TCP CLIENT PASS" not in client:
        raise RuntimeError("Order client did not finish")
    if json_messages(client, "[TCP receive thread] ") != expected:
        raise RuntimeError("Wrong JSON receipts or string escaping")


def order_bytes(index):
    value = {"type": "order", "id": index, "sku": 102 if index == 2 else 101,
             "quantity": 2 if index == 1 else 1 if index == 2 else 0,
             "note": NOTE}
    # Different whitespace/key order from the C serializer must be accepted.
    return json.dumps(value, ensure_ascii=False, sort_keys=True,
                      indent=1).encode("utf-8")


def socket_client(port, third=None):
    bodies = [order_bytes(1), order_bytes(2),
              order_bytes(3) if third is None else third]
    frames = [struct.pack("!H", len(body)) + body for body in bodies]
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
                    raise RuntimeError("EOF in a JSON TCP frame")
                result.extend(part)
            return result

        # Split one header/body, then coalesce the following JSON frames.
        peer.sendall(frames[0][:1])
        time.sleep(0.02)
        peer.sendall(frames[0][1:7])
        time.sleep(0.02)
        peer.sendall(frames[0][7:] + frames[1] + frames[2])
        replies = []
        for _ in EXPECTED:
            length = struct.unpack("!H", read_exact(2))[0]
            if not 0 < length < 1024:
                raise RuntimeError("Invalid JSON frame length")
            replies.append(json.loads(read_exact(length).decode("utf-8")))
    text = "".join("[TCP receive thread] " + json.dumps(reply) + "\n"
                   for reply in replies)
    text += "TCP CLIENT PASS: framed JSON received\n"
    print(text, end="")
    return text


def invalid_inputs():
    base = {"type": "order", "id": 3, "sku": 101, "note": NOTE}
    for quantity in ("1", True, 1.0):
        value = dict(base, quantity=quantity)
        yield json.dumps(value).encode(), EXPECTED[-1]
    yield json.dumps(base).encode(), EXPECTED[-1]
    invalid = {"type": "error", "ok": False, "reason": "invalid_json_request"}
    yield b'{"type":"order",', invalid
    yield b'{"type":"order","id":3,"quantity":0,"quantity":1}', invalid
    yield b'[]', invalid


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bin-dir", required=True)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--fragmented", action="store_true")
    mode.add_argument("--json-errors", action="store_true")
    args = parser.parse_args()
    if args.json_errors:
        for body, rejected in invalid_inputs():
            expected = EXPECTED[:4] + [rejected]
            run_pair(args.bin_dir, "tcp_order_server", None, "0",
                     lambda server, client: verify_orders(
                         server, client, expected),
                     client_action=lambda port: socket_client(port, body))
        print("PASS tcp_business_json_validation")
    else:
        run_pair(args.bin_dir, "tcp_order_server",
                 None if args.fragmented else "tcp_order_client", "0",
                 verify_orders,
                 client_action=socket_client if args.fragmented else None)
        print("PASS tcp_business_" +
              ("framing" if args.fragmented else "orders"))


if __name__ == "__main__":
    raise SystemExit(checked_main(main))
