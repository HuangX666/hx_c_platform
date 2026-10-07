"""Build and run C99/C++11 consumers against an isolated installation."""

import argparse
import hashlib
import os
from pathlib import Path
import subprocess


def run(arguments, **options):
    subprocess.run(arguments, check=True, **options)


def check_runtime(build, prefix, config, cache):
    enabled = lambda name: cache.get(name, "").upper() in ("1", "ON", "TRUE")
    if not enabled("HX_OS_ENABLE_CRYPTO") or enabled("HX_OS_OPENSSL_STATIC"):
        return
    if not enabled("HX_OS_INSTALL_OPENSSL_RUNTIME"):
        return
    manifest = build / f"hx_openssl_runtime_{config}.txt"
    folder = cache["CMAKE_INSTALL_BINDIR" if os.name == "nt"
                   else "CMAKE_INSTALL_LIBDIR"]
    for item in manifest.read_text(encoding="utf-8").splitlines():
        source = Path(item)
        installed = prefix / folder / source.name
        if not installed.is_file():
            raise RuntimeError(
                f"Missing runtime or broken symlink: {installed}")
        expected = hashlib.sha256(source.read_bytes()).digest()
        actual = hashlib.sha256(installed.read_bytes()).digest()
        if expected != actual:
            raise RuntimeError(
                f"Installed runtime differs from SDK: {installed}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build_directory", type=Path)
    parser.add_argument("--config", default="Debug")
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--ctest", default="ctest")
    parser.add_argument("--format-clang")
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
    prefixes = [str(prefix)]
    if cache.get("HX_OS_DEPENDENCY_PREFIX"):
        prefixes.append(cache["HX_OS_DEPENDENCY_PREFIX"])
    run([
        arguments.cmake, "--install", str(build),
        "--config", arguments.config, "--prefix", str(prefix),
    ])
    check_runtime(build, prefix, arguments.config, cache)
    command = [
        arguments.cmake, "-S", str(root / "tests/install_consumer"),
        "-B", str(consumer), "-G", cache["CMAKE_GENERATOR"],
        "-DCMAKE_PREFIX_PATH=" + ";".join(prefixes),
        "-DCMAKE_BUILD_TYPE=" + arguments.config,
    ]
    for key, flag in (
        ("CMAKE_GENERATOR_PLATFORM", "-A"),
        ("CMAKE_GENERATOR_TOOLSET", "-T"),
    ):
        if cache.get(key):
            command.extend([flag, cache[key]])
    if arguments.format_clang:
        command += ["-DHX_CONSUMER_FORMAT_SYNTAX=ON",
                    "-DHX_FORMAT_CLANG=" + arguments.format_clang]
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
    pure_c = build / "installed-consumer-c"
    command[command.index("-B") + 1] = str(pure_c)
    command.append("-DHX_CONSUMER_CPP=OFF")
    run(command)
    run([arguments.cmake, "--build", str(pure_c),
         "--config", arguments.config])
    run([arguments.ctest, "-C", arguments.config, "--output-on-failure"],
        cwd=pure_c, env=environment)


if __name__ == "__main__":
    main()
