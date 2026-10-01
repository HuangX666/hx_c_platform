"""Build Android contracts; run only on an explicitly selected adb target."""

import argparse
import datetime
import json
import os
from pathlib import Path
import shlex
import subprocess
import uuid

ROOT = Path(__file__).resolve().parents[1]
SUITES = (
    "android_baseline", "string_tools", "lexer", "wide_wait",
    "catalog_filename", "procedure", "atomic", "cpp_utilities",
)


def run(command, log, timeout=900):
    with log.open("w", encoding="utf-8", newline="\n") as output:
        result = subprocess.run(
            [str(item) for item in command], cwd=ROOT, stdout=output,
            stderr=subprocess.STDOUT, timeout=timeout, check=False,
        )
    if result.returncode:
        lines = log.read_text(encoding="utf-8", errors="replace").splitlines()
        tail = lines[-35:]
        raise RuntimeError(f"{log}:\n" + "\n".join(tail))


def adb_text(args, *command):
    return subprocess.check_output(
        [args.adb, "-s", args.serial, *command], timeout=30
    ).decode("utf-8", errors="replace").strip()


def device_info(args):
    if adb_text(args, "get-state") != "device":
        raise RuntimeError("The explicitly selected adb target is not ready")
    properties = {
        key: adb_text(args, "shell", "getprop", key)
        for key in ("ro.product.model", "ro.build.version.sdk",
                    "ro.product.cpu.abilist", "ro.product.cpu.abi",
                    "ro.kernel.qemu", "ro.build.fingerprint")
    }
    return properties


def device_run(args, build, abi, api, record):
    info = device_info(args)
    record["device"] = info
    supported = (info["ro.product.cpu.abilist"]
                 or info["ro.product.cpu.abi"]).split(",")
    if abi not in supported or int(info["ro.build.version.sdk"]) < api:
        record["runtime"] = "skipped: target ABI/API is incompatible"
        return
    remote = "/data/local/tmp/hx_c_platform-" + uuid.uuid4().hex
    binaries = [build / "bin" / f"hx_os_test_{suite}" for suite in SUITES]
    binaries.append(build / "bin" / "hx_os_portable_baseline")
    files = list(binaries)
    if not args.static:
        files.append(build / "libhx_c_platform.so")
    for path in files:
        if not path.is_file():
            raise RuntimeError(f"Missing Android artifact: {path}")
    uploaded = []
    timed_out = False
    adb_text(args, "shell", "mkdir " + shlex.quote(remote))
    try:
        for path in files:
            destination = remote + "/" + path.name
            # Record each explicit path even if adb push fails midway.
            uploaded.append(destination)
            run([args.adb, "-s", args.serial, "push", path, destination],
                build / ("push-" + path.name + ".log"), 120)
        adb_text(args, "shell", "chmod 700 " + " ".join(
            shlex.quote(remote + "/" + path.name) for path in binaries))
        for path in binaries:
            command = (
                "cd " + shlex.quote(remote) + " && LD_LIBRARY_PATH="
                + shlex.quote(remote) + " ./" + shlex.quote(path.name)
            )
            run([args.adb, "-s", args.serial, "shell", command],
                build / ("run-" + path.name + ".log"), 60)
        record["runtime"] = (
            "passed on emulator" if info["ro.kernel.qemu"] == "1"
            else "passed on adb target; verify physical-device provenance"
        )
    except subprocess.TimeoutExpired:
        timed_out = True
        record["runtime"] = "timeout; remote process may still be running"
        record["retained_device_directory"] = remote
        raise
    finally:
        # Remove only the files uploaded by this invocation. A failed test
        # may leave its fixtures; rmdir then fails and preserves that evidence.
        if uploaded and not timed_out:
            adb_text(args, "shell", "rm -f " + " ".join(
                shlex.quote(path) for path in uploaded))
        try:
            if not timed_out:
                adb_text(args, "shell", "rmdir " + shlex.quote(remote))
        except subprocess.CalledProcessError:
            record["retained_device_directory"] = remote


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ndk", type=Path,
                        default=os.environ.get("ANDROID_NDK_ROOT"))
    parser.add_argument("--build-root", type=Path,
                        default=ROOT / "build-android")
    parser.add_argument("--abi", choices=("arm64-v8a", "armeabi-v7a"),
                        action="append")
    parser.add_argument("--api", type=int, action="append")
    parser.add_argument("--minimal", action="store_true")
    parser.add_argument("--static", action="store_true")
    parser.add_argument("--serial", help="Explicit adb device/emulator serial")
    parser.add_argument("--adb", default="adb")
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--ninja", type=Path)
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    apis, abis = args.api or [21, 28], args.abi or [
        "arm64-v8a", "armeabi-v7a"]
    if not args.ndk or min(apis) < 21 or args.jobs < 1:
        parser.error("--ndk is required; API >= 21 and jobs >= 1")
    ndk = args.ndk.resolve()
    toolchain = ndk / "build/cmake/android.toolchain.cmake"
    if not toolchain.is_file():
        parser.error(f"NDK toolchain not found: {toolchain}")
    root = args.build_root.resolve()
    root.mkdir(parents=True, exist_ok=True)
    report = {
        "utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "ndk": (ndk / "source.properties").read_text(encoding="utf-8"),
        "git": subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "dirty": bool(subprocess.check_output(
            ["git", "status", "--porcelain"], cwd=ROOT, text=True).strip()),
        "results": [],
    }
    try:
        for api in apis:
            for abi in abis:
                profile = "minimal" if args.minimal else "full"
                linkage = "static" if args.static else "shared"
                build = root / f"{abi}-api{api}-{profile}-{linkage}"
                build.mkdir(parents=True, exist_ok=True)
                record = {"abi": abi, "api": api, "profile": profile,
                          "linkage": linkage, "build": "pending",
                          "runtime": "not run: no --serial"}
                report["results"].append(record)
                command = [args.cmake, "-S", ROOT, "-B", build,
                           "-G", "Ninja"]
                if args.minimal:
                    command += ["-C", ROOT / "cmake/minimal.cmake"]
                if args.ninja:
                    command += [f"-DCMAKE_MAKE_PROGRAM={args.ninja.resolve()}"]
                command += [
                    f"-DCMAKE_TOOLCHAIN_FILE={toolchain}",
                    f"-DANDROID_ABI={abi}", f"-DANDROID_PLATFORM=android-{api}",
                    "-DANDROID_STL=c++_static", "-DCMAKE_BUILD_TYPE=Release",
                    "-DBUILD_SHARED_LIBS=" + ("OFF" if args.static else "ON"),
                    "-DHX_OS_BUILD_TESTS=ON", "-DHX_OS_BUILD_EXAMPLES=ON",
                    "-DHX_OS_BUILD_BENCHMARKS=ON",
                ]
                print(f"Building {abi} API {api} {profile} {linkage}",
                      flush=True)
                run(command, build / "configure.log")
                run([args.cmake, "--build", build, "--parallel", args.jobs],
                    build / "build.log")
                record["build"] = "passed; C99/C++11 headers included"
                if args.serial:
                    device_run(args, build, abi, api, record)
                print(record["build"] + "; " + record["runtime"], flush=True)
    finally:
        name = "report-minimal.json" if args.minimal else "report.json"
        target = root / name
        target.write_text(json.dumps(report, ensure_ascii=False, indent=2)
                          + "\n", encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()
