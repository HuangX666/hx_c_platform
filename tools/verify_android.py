"""Build Android contracts; run only on an explicitly selected adb target."""

import argparse
import datetime
import json
import os
import re
from pathlib import Path
import shlex
import subprocess
import uuid

ROOT = Path(__file__).resolve().parents[1]


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


def native_tests(args, build):
    data = json.loads(subprocess.check_output([
        args.ctest, "--test-dir", str(build), "--show-only=json-v1",
    ], text=True, encoding="utf-8"))
    selected, omitted = [], {}
    requested = set(args.suite or ())
    for test in data["tests"]:
        name = test["name"]
        if requested and name not in requested:
            continue
        command = test.get("command", [])
        executable = Path(command[0]) if command else None
        if (executable and executable.is_absolute()
                and executable.is_relative_to(build)
                and not executable.is_file()):
            raise RuntimeError(f"Missing configured test: {executable}")
        if executable and executable.is_file():
            with executable.open("rb") as source:
                native = source.read(4) == b"\x7fELF"
        else:
            native = False
        if not native:
            omitted[name] = "host command; run in the desktop matrix"
            continue
        properties = {p["name"]: p["value"]
                      for p in test.get("properties", [])}
        if properties.get("DISABLED"):
            omitted[name] = "disabled by this CMake configuration"
            continue
        selected.append({"name": name, "command": command,
                         "properties": properties})
    known = {t["name"] for t in selected} | set(omitted)
    if requested - known:
        raise RuntimeError("Unknown configured suites: " +
                           ", ".join(sorted(requested - known)))
    if not selected:
        raise RuntimeError("No runnable native tests selected")
    return selected, omitted


def runtime_files(build):
    files = list(build.glob("libhx_c_platform.so*"))
    files.extend(build.glob("libhx_archive_7zip.so*"))
    manifest = build / "hx_openssl_runtime_Release.txt"
    if manifest.exists():
        for item in manifest.read_text(encoding="utf-8").splitlines():
            source = Path(item)
            if not source.is_file():
                raise RuntimeError(f"Missing selected runtime: {source}")
            actual = source.resolve()
            if actual.name in ("libcrypto-hx.so", "libssl-hx.so"):
                # Linker aliases would shadow the native bridge's BoringSSL.
                files.append(actual)
                continue
            files.extend((source, actual))
            files.extend(path for path in source.parent.glob("*.so*")
                         if path.resolve() == actual)
    return files


def configured_features(build):
    config = (build / "private/hx_os_config.h").read_text(encoding="utf-8")
    enabled = set(re.findall(r"#define (HX_OS_\w+)\s+1\b", config))
    features = {name: "HX_OS_HAVE_" + name in enabled
                for name in ("OPENSSL", "TLS", "ARCHIVE", "7ZIP", "ICONV",
                             "CJSON", "LIBYAML", "MINIXML", "INIH", "TOMLC")}
    features["CRYPTO"] = ("HX_OS_ENABLE_CRYPTO" in enabled
                          and features["OPENSSL"])
    return features


def deployment(tests, build, remote):
    files, folders = {}, {remote, remote + "/tmp"}

    def location(path):
        path = Path(os.path.abspath(path))
        if path.is_relative_to(build):
            if path == build:
                return remote
            return remote + "/" + path.relative_to(build).as_posix()
        if path.is_relative_to(ROOT):
            return remote + "/source/" + path.relative_to(ROOT).as_posix()
        return remote + "/runtime/" + path.name

    def add(path, destination=None):
        path = Path(path)
        destination = destination or location(path)
        if path.is_dir():
            folders.add(destination)
            for child in path.iterdir():
                add(child)
        elif path.is_file():
            old = files.get(destination)
            if old and old.resolve() != path.resolve():
                raise RuntimeError(f"Deployment name conflict: {destination}")
            files[destination] = path
            parent = destination.rsplit("/", 1)[0]
            while parent.startswith(remote + "/"):
                folders.add(parent)
                parent = parent.rsplit("/", 1)[0]
        else:
            raise RuntimeError(f"Missing Android artifact: {path}")
        return destination

    for path in runtime_files(build):
        # SDK prefixes often live under ROOT, but native loader lookup
        # needs dependencies beside executables for Android RUNPATH.
        add(path, remote + "/bin/" + path.name)
    commands = []
    for test in tests:
        command = [add(Path(test["command"][0]))]
        for argument in test["command"][1:]:
            path = Path(argument)
            command.append(add(path) if path.is_absolute() else argument)
        properties = test["properties"]
        directory = location(properties.get("WORKING_DIRECTORY", build))
        folders.add(directory)
        commands.append((test, command, directory))
    return files, folders, commands


def device_run(args, build, abi, api, record):
    record["runtime"] = "failed: device setup or test execution"
    info = device_info(args)
    record["device"] = info
    record["native_machine"] = adb_text(args, "shell", "uname -m")
    record["execution"] = (
        "native ISA" if abi == info["ro.product.cpu.abi"]
        else "guest/compatibility ISA; not a native CPU validation"
    )
    record["tests"] = {}
    supported = (info["ro.product.cpu.abilist"]
                 or info["ro.product.cpu.abi"]).split(",")
    if abi not in supported or int(info["ro.build.version.sdk"]) < api:
        record["runtime"] = "skipped: target ABI/API is incompatible"
        return
    tests, record["host_tests"] = native_tests(args, build)
    benchmark = build / "bin/hx_os_portable_baseline"
    tests.append({"name": "portable_baseline", "command": [str(benchmark)],
                  "properties": {"TIMEOUT": 120}})
    remote = "/data/local/tmp/hx_c_platform-" + uuid.uuid4().hex
    files, folders, commands = deployment(tests, build, remote)
    if record["execution"] != "native ISA" and any(
            Path(path).name in ("libssl.so", "libcrypto.so") for path in files):
        raise RuntimeError("Translated Android needs distinct OpenSSL "
                           "SONAMEs; rebuild the SDK with tools/dependencies")
    record["deployed_runtimes"] = sorted(
        path for path in files if ".so" in Path(path).name)
    uploaded, timed_out, failed = [], False, []
    adb_text(args, "shell", "mkdir -p " + " ".join(
        shlex.quote(path) for path in sorted(folders)))
    try:
        for destination, path in files.items():
            uploaded.append(destination)
            run([args.adb, "-s", args.serial, "push", path, destination],
                build / ("push-" + path.name + ".log"), 120)
        executables = {command[0] for _, command, _ in commands}
        executables.update(path for path in files if "/bin/" in path)
        adb_text(args, "shell", "chmod 700 " + " ".join(
            shlex.quote(path) for path in sorted(executables)))
        for test, command, directory in commands:
            name, props = test["name"], test["properties"]
            environment = ["TMPDIR=" + remote + "/tmp",
                           "HX_OS_TEST_TMPDIR=" + remote + "/tmp"]
            environment.append("LD_LIBRARY_PATH=" + remote + "/bin")
            # Only distinct OpenSSL SONAMEs are deployed for translation;
            # native-bridge transitive lookup also needs this explicit path.
            environment += props.get("ENVIRONMENT", [])
            shell = "cd " + shlex.quote(directory) + " && env "
            shell += " ".join(shlex.quote(v) for v in environment + command)
            record["tests"][name] = "failed or interrupted"
            log = build / ("run-" + name + ".log")
            with log.open("w", encoding="utf-8", newline="\n") as output:
                result = subprocess.run(
                    [args.adb, "-s", args.serial, "shell", shell],
                    stdout=output, stderr=subprocess.STDOUT,
                    timeout=max(60, props.get("TIMEOUT", 60)) *
                    args.timeout_scale, check=False)
            content = log.read_text(encoding="utf-8", errors="replace")
            notes = [line for line in content.splitlines()
                     if line.startswith("SKIP ")]
            skipped = result.returncode == props.get("SKIP_RETURN_CODE")
            passed = result.returncode == 0
            if props.get("WILL_FAIL"):
                passed = not passed
            patterns = props.get("PASS_REGULAR_EXPRESSION", [])
            if patterns:
                passed = passed and any(re.search(p, content) for p in patterns)
            if any(re.search(p, content)
                   for p in props.get("FAIL_REGULAR_EXPRESSION", [])):
                passed = False
            state = "skipped" if skipped else "passed" if passed else "failed"
            record["tests"][name] = state
            if notes or skipped:
                record.setdefault("test_notes", {})[name] = notes or [content]
            if state == "failed":
                failed.append(name)
            print(f"  {name}: {state}", flush=True)
        if failed:
            raise RuntimeError("Android tests failed: " + ", ".join(failed))
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
        # Never remove test-created files or a timed-out program's resources.
        if uploaded and not timed_out:
            for offset in range(0, len(uploaded), 64):
                adb_text(args, "shell", "rm -f " + " ".join(
                    shlex.quote(p) for p in uploaded[offset:offset + 64]))
        if not timed_out:
            for path in sorted(folders, key=len, reverse=True):
                try:
                    adb_text(args, "shell", "rmdir " + shlex.quote(path))
                except subprocess.CalledProcessError:
                    record["retained_device_directory"] = remote


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ndk", type=Path,
                        default=os.environ.get("ANDROID_NDK_ROOT"))
    parser.add_argument("--build-root", type=Path,
                        default=ROOT / "out" / "android")
    parser.add_argument("--abi",
                        choices=("arm64-v8a", "armeabi-v7a", "x86_64"),
                        action="append",
                        help="x86_64 supplements the two ARM baselines")
    parser.add_argument("--api", type=int, action="append")
    parser.add_argument("--suite", action="append",
                        help="CTest name; default runs every native test")
    parser.add_argument("--ctest", default="ctest")
    parser.add_argument("--timeout-scale", type=float, default=1.0)
    parser.add_argument("--parsers", action="store_true",
                        help="Enable all five optional parser backends")
    parser.add_argument("--iconv", action="store_true")
    parser.add_argument("--provider", default="SUBMODULE",
                        choices=("SUBMODULE", "FETCHCONTENT"))
    parser.add_argument("--sevenzip-library", type=Path,
                        help="Optional target 7-Zip 26.04 runtime")
    parser.add_argument("--minimal", action="store_true")
    parser.add_argument("--static", action="store_true")
    parser.add_argument("--serial", help="Explicit adb device/emulator serial")
    parser.add_argument("--adb", default="adb")
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--ninja", type=Path)
    parser.add_argument("--dependency-prefix", type=Path,
                        help="OpenSSL/archive SDK for one selected ABI/API")
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    apis, abis = args.api or [21, 28], args.abi or [
        "arm64-v8a", "armeabi-v7a"]
    if ((args.dependency_prefix or args.sevenzip_library)
            and (len(apis) != 1 or len(abis) != 1)):
        parser.error("--dependency-prefix requires one explicit ABI and API")
    if (not args.ndk or min(apis) < 21 or args.jobs < 1
            or not 0 < args.timeout_scale <= 10):
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
        "suites": args.suite or "all configured native CTest tests",
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
                    "-DHX_OS_DEPENDENCY_PROVIDER=" + args.provider,
                    "-DHX_OS_ENABLE_ICONV=" + ("ON" if args.iconv else "OFF"),
                ]
                for backend in ("CJSON", "LIBYAML", "MINIXML", "INIH", "TOMLC"):
                    command += [f"-DHX_OS_ENABLE_{backend}=" +
                                ("ON" if args.parsers else "OFF")]
                if args.sevenzip_library:
                    command += ["-DHX_OS_ENABLE_7ZIP=ON",
                                "-DHX_OS_TEST_7ZIP_LIBRARY=" +
                                str(args.sevenzip_library.resolve())]
                else:
                    command += ["-DHX_OS_ENABLE_7ZIP=OFF"]
                if args.dependency_prefix:
                    command += [
                        "-DHX_OS_ENABLE_CRYPTO=ON",
                        "-DHX_OS_ENABLE_ARCHIVE=ON",
                        "-DHX_OS_ENABLE_TLS=ON",
                        "-DHX_OS_DEPENDENCY_PREFIX=" +
                        str(args.dependency_prefix.resolve()),
                    ]
                else:
                    command += ["-DHX_OS_ENABLE_CRYPTO=OFF",
                                "-DHX_OS_ENABLE_ARCHIVE=OFF"]
                record["tls"] = "requested" if args.dependency_prefix else (
                    "disabled: no target OpenSSL SDK")
                record["archive"] = "requested" if args.dependency_prefix else (
                    "disabled: no target archive SDK")
                record["crypto"] = "enabled" if args.dependency_prefix else (
                    "disabled: no target OpenSSL SDK")
                print(f"Building {abi} API {api} {profile} {linkage}",
                      flush=True)
                run(command, build / "configure.log")
                record["features"] = configured_features(build)
                for module in ("crypto", "tls", "archive"):
                    record[module] = ("enabled" if record["features"][
                        module.upper()] else "disabled by configuration")
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
