"""Compare a shared library's exported HX symbols with the public headers."""

import argparse
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("library", type=Path)
    parser.add_argument("--tool", default="nm")
    arguments = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    expected = set()
    for header in (root / "include").glob("*.h"):
        source = header.read_text(encoding="utf-8")
        expected.update(re.findall(
            r"HX_OS_CALL\s+(hx_(?:os_|string_|settings_)\w+)\s*\(",
            source,
        ))
        for name in re.findall(
            r"^HX_OS_DECLARE_PATH\((\w+)\);", source, re.MULTILINE
        ):
            expected.update((f"hx_os_path_{name}",
                             f"hx_os_path_{name}_string"))
    command = [arguments.tool]
    windows = "dumpbin" in Path(arguments.tool).name.lower()
    if windows:
        command.extend(["/nologo", "/exports"])
    else:
        command.extend(["-D", "--defined-only"])
    command.append(str(arguments.library.resolve()))
    output = subprocess.check_output(command).decode("utf-8", errors="replace")
    if windows:
        pattern = r"^\s*\d+\s+[\dA-Fa-f]+\s+[\dA-Fa-f]+\s+_?(hx_\w+)"
    else:
        pattern = r"^\s*[\dA-Fa-f]+\s+[A-Za-z]\s+_?(hx_\w+)"
    actual = set(re.findall(pattern, output, re.MULTILINE))
    missing = expected - actual
    extra = actual - expected
    if missing or extra:
        raise SystemExit(
            f"Missing exports: {sorted(missing)}\n"
            f"Unexpected HX exports: {sorted(extra)}"
        )
    print(f"Public ABI matches: {len(expected)} exported functions.")


if __name__ == "__main__":
    main()
