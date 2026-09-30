"""Build and run C99/C++11 consumers against an isolated installation."""

import argparse
import os
from pathlib import Path
import subprocess


def run(arguments, **options):
    subprocess.run(arguments, check=True, **options)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build_directory", type=Path)
    parser.add_argument("--config", default="Debug")
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--ctest", default="ctest")
    arguments = parser.parse_args()
    build = arguments.build_directory.resolve()
    root = Path(__file__).resolve().parents[1]
    cache = {}
    for line in (build / "CMakeCache.txt").read_text(
        encoding="utf-8"
    ).splitlines():
        if "=" in line and not line.startswith(("//", "#")):
            key, value = line.split("=", 1)
            cache[key.split(":", 1)[0]] = value
    prefix = build / "installed"
    consumer = build / "installed-consumer"
    run([
        arguments.cmake, "--install", str(build),
        "--config", arguments.config, "--prefix", str(prefix),
    ])
    command = [
        arguments.cmake, "-S", str(root / "tests/install_consumer"),
        "-B", str(consumer), "-G", cache["CMAKE_GENERATOR"],
        "-DCMAKE_PREFIX_PATH=" + str(prefix),
        "-DCMAKE_BUILD_TYPE=" + arguments.config,
    ]
    for key, flag in (
        ("CMAKE_GENERATOR_PLATFORM", "-A"),
        ("CMAKE_GENERATOR_TOOLSET", "-T"),
    ):
        if cache.get(key):
            command.extend([flag, cache[key]])
    run(command)
    run([
        arguments.cmake, "--build", str(consumer),
        "--config", arguments.config,
    ])
    environment = os.environ.copy()
    environment["PATH"] = str(prefix / "bin") + os.pathsep + environment["PATH"]
    run(
        [arguments.ctest, "-C", arguments.config, "--output-on-failure"],
        cwd=consumer, env=environment,
    )


if __name__ == "__main__":
    main()

