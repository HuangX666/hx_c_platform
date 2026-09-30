"""Emit a C99 XOR byte initializer without copying plaintext into comments."""

import argparse
from pathlib import Path
import re
import sys

C_KEYWORDS = set("auto break case char const continue default do double else "
                 "enum extern float for goto if inline int long register "
                 "restrict return short signed sizeof static struct switch "
                 "typedef union unsigned void volatile while".split())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--name", required=True)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--text")
    source.add_argument("--input", type=Path)
    parser.add_argument("--key", type=lambda value: int(value, 0), default=0x5A)
    parser.add_argument("--output", type=Path)
    arguments = parser.parse_args()
    if (not re.fullmatch(r"[a-z][a-z0-9_]{0,31}", arguments.name)
            or arguments.name in C_KEYWORDS):
        parser.error("name must be a lowercase C identifier (1..32), "
                     "not a keyword")
    if not 1 <= arguments.key <= 255:
        parser.error("key must be in 1..255")
    if arguments.input:
        with arguments.input.open("rb") as stream:
            data = stream.read(1024 * 1024 + 1)
    else:
        data = arguments.text.encode("utf-8", errors="strict")
    if len(data) > 1024 * 1024:
        parser.error("input limit is 1 MiB")
    try:
        data.decode("utf-8", errors="strict")
    except UnicodeError:
        parser.error("input must be valid UTF-8")
    encoded = [value ^ arguments.key for value in data]
    name = arguments.name
    lines = [
        '#include "hx_os_obfuscate.h"',
        "",
        f"static const unsigned char {name}_bytes[] = {{",
    ]
    for offset in range(0, max(1, len(encoded)), 12):
        chunk = encoded[offset:offset + 12] or [0]
        values = ", ".join(f"0x{value:02x}" for value in chunk)
        lines.append("\t" + values + ",")
    lines.extend([
        "};",
        f"static const hx_os_obfuscated {name} = {{",
        f"\t{name}_bytes, {len(data)}, 0x{arguments.key:02x}",
        "};",
        "",
    ])
    result = "\n".join(lines)
    if arguments.output:
        with arguments.output.open(
                "w", encoding="utf-8", newline="\n") as stream:
            stream.write(result)
    else:
        sys.stdout.write(result)


if __name__ == "__main__":
    main()
