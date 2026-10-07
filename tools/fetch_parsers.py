"""Compatibility entry for the common pinned dependency source provider."""

import argparse
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
PARSERS = ("cjson", "libyaml", "minixml", "inih", "tomlc")


def prepare(packages, provider, cmake):
    subprocess.run([
        cmake, "-S", str(ROOT / "tools/sources"),
        "-B", str(ROOT / "build-dependency-sources"),
        "-DHX_OS_DEPENDENCIES=" + ";".join(packages),
        "-DHX_OS_DEPENDENCY_PROVIDER=" + provider,
    ], check=True)


def main(packages=PARSERS):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("packages", nargs="*", choices=packages)
    parser.add_argument("--provider", default="SUBMODULE",
                        choices=("SUBMODULE", "FETCHCONTENT"))
    parser.add_argument("--cmake", default="cmake")
    args = parser.parse_args()
    prepare(args.packages or packages, args.provider, args.cmake)


if __name__ == "__main__":
    main()
