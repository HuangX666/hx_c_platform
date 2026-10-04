"""Run baseline-header consumers against an installed current shared library."""

import argparse
import os
from pathlib import Path
import subprocess


def run(arguments, **options):
    subprocess.run(arguments, check=True, **options)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build_directory", type=Path)
    parser.add_argument("--baseline", default="7cf52fc")
    parser.add_argument("--config", default="Release")
    parser.add_argument("--cmake", default="cmake")
    arguments = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    build = arguments.build_directory.resolve()
    cache = {}
    for line in (build / "CMakeCache.txt").read_text(
        encoding="utf-8"
    ).splitlines():
        if "=" in line and not line.startswith(("//", "#")):
            key, value = line.split("=", 1)
            cache[key.split(":", 1)[0]] = value
    if cache.get("BUILD_SHARED_LIBS") != "ON":
        raise SystemExit("ABI validation requires a shared-library build.")
    revision = subprocess.check_output(
        ["git", "rev-parse", "--verify", "--end-of-options",
         arguments.baseline + "^{commit}"], cwd=root, text=True
    ).strip()
    source = build / "abi-consumer-source"
    source.mkdir(exist_ok=True)
    names = subprocess.check_output(
        ["git", "ls-tree", "-r", "--name-only", revision, "include"],
        cwd=root, text=True,
    ).splitlines()
    suites = ["core", "data", "lifecycle", "config", "containers",
              "pool", "net", "loop_concurrency", "options", "cpp_pool"]
    names += ["tests/test_" + suite +
              (".cpp" if suite == "cpp_pool" else ".c") for suite in suites]
    # Newer baseline tests include small local assertion/helper headers.
    test_headers = subprocess.check_output(
        ["git", "ls-tree", "-r", "--name-only", revision, "tests"],
        cwd=root, text=True,
    ).splitlines()
    names += [name for name in test_headers if name.endswith((".h", ".hpp"))]
    for name in names:
        destination = source / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(subprocess.check_output(
            ["git", "show", revision + ":" + name], cwd=root
        ))
    cmake = [
        "cmake_minimum_required(VERSION 3.16)",
        "project(hx_old_header_consumer LANGUAGES C CXX)",
        "find_package(hx_c_platform CONFIG REQUIRED)",
        "enable_testing()",
    ]
    for suite in suites:
        extension = ".cpp" if suite == "cpp_pool" else ".c"
        cmake += [
            f"add_executable(old_{suite} tests/test_{suite}{extension})",
            f"target_include_directories(old_{suite} BEFORE PRIVATE",
            '  "${CMAKE_CURRENT_SOURCE_DIR}/include")',
            f"target_link_libraries(old_{suite} PRIVATE",
            "  hx_c_platform::hx_c_platform)",
            f"set_target_properties(old_{suite} PROPERTIES",
            "  C_STANDARD 99 C_STANDARD_REQUIRED YES C_EXTENSIONS NO",
            "  CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)",
            "if(MSVC)",
            f"  target_compile_options(old_{suite} PRIVATE /utf-8)",
            "endif()",
            f"add_test(NAME old_{suite} COMMAND old_{suite})",
            f"set_tests_properties(old_{suite} PROPERTIES TIMEOUT 30)",
        ]
        if suite == "cpp_pool" and "tests/cpp_check.hpp" in names:
            cmake += [
                "if(MSVC)",
                "  target_compile_options(old_cpp_pool PRIVATE /EHs-c-)",
                "  target_compile_definitions(old_cpp_pool PRIVATE",
                "    _HAS_EXCEPTIONS=0)",
                "else()",
                "  target_compile_options(old_cpp_pool PRIVATE",
                "    -fno-exceptions)",
                "endif()",
            ]
    (source / "CMakeLists.txt").write_text(
        "\n".join(cmake) + "\n", encoding="utf-8", newline="\n"
    )
    consumer = build / "abi-consumer-build"
    command = [
        arguments.cmake, "-S", str(source), "-B", str(consumer),
        "-G", cache["CMAKE_GENERATOR"],
        "-DCMAKE_PREFIX_PATH=" + str(build / "installed"),
        "-DCMAKE_BUILD_TYPE=" + arguments.config,
    ]
    for key, flag in (("CMAKE_GENERATOR_PLATFORM", "-A"),
                      ("CMAKE_GENERATOR_TOOLSET", "-T")):
        if cache.get(key):
            command.extend([flag, cache[key]])
    run(command)
    run([arguments.cmake, "--build", str(consumer),
         "--config", arguments.config, "--parallel", "4"])
    environment = os.environ.copy()
    environment["PATH"] = str(build / "installed/bin") + os.pathsep + \
        environment["PATH"]
    run(["ctest", "-C", arguments.config, "--output-on-failure"],
        cwd=consumer, env=environment)
    print("Baseline headers and test sources:", revision)


if __name__ == "__main__":
    main()
