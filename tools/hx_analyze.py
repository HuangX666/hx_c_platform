"""Explicit developer analysis; no tool runs during an ordinary build."""

import argparse
import ctypes
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import time


def find_tool(name):
    found = shutil.which(name)
    if found or os.name != "nt":
        return found

    kits = Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)"))
    candidate = kits / "Windows Kits/10/Windows Performance Toolkit"
    candidate /= name + ".exe"
    return str(candidate) if candidate.is_file() else None


def require_tool(name):
    found = find_tool(name)
    if not found:
        raise RuntimeError("Missing tool: " + name + "; see docs/ANALYSIS.md")
    return found


def run(command, log, timeout, cwd, environment=None):
    # The supplied command must be a finite development workload. On timeout
    # stop only this invocation's process tree, never an attached application.
    with log.open("ab") as output:
        shown = subprocess.list2cmdline([str(item) for item in command])
        output.write(("\n$ " + shown + "\n").encode("utf-8"))
        output.flush()
        process = subprocess.Popen(command, cwd=cwd, env=environment,
                                   stdout=output, stderr=output,
                                   start_new_session=os.name != "nt")
        output.write(("PID: " + str(process.pid) + "\n").encode("ascii"))
        output.flush()
        try:
            return process.wait(timeout=timeout)
        except (subprocess.TimeoutExpired, KeyboardInterrupt):
            if process.poll() is None:
                if os.name == "nt":
                    subprocess.run(["taskkill", "/PID", str(process.pid),
                                    "/T", "/F"], stdout=output,
                                   stderr=output, check=False, timeout=10)
                else:
                    os.killpg(process.pid, signal.SIGTERM)
                    try:
                        process.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        pass
                    # The recorder can exit before a stubborn child does.
                    try:
                        os.killpg(process.pid, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
            process.wait(timeout=10)
            output.write(b"\nWorkload interrupted; analysis is incomplete.\n")
            return 124


def run_linux(args, command, output):
    if not sys.platform.startswith("linux"):
        raise RuntimeError("Run this mode in Linux/WSL, not Git Bash.")

    log = output / "run.log"
    report = output / "report.txt"
    if args.mode == "perf":
        tool = require_tool("perf")
        data = output / "perf.data"
        # DrvFS can reject writes from perf's mmap buffer with EFAULT. Record
        # on the native filesystem, then copy to the requested local report.
        with tempfile.TemporaryDirectory(prefix="hx-perf-", dir="/tmp") as tmp:
            native = Path(tmp) / "perf.data"
            capture = [tool, "record", "--no-buildid-cache", "--max-size",
                       "64M", "-F", "199", "-e", "cpu-clock:u", "--call-graph",
                       "dwarf,8192", "-o", str(native), "--"]
            code = run(capture + command, log, args.timeout, args.build)
            if native.is_file():
                shutil.copyfile(native, data)
        if data.is_file() and data.stat().st_size:
            exported = run([tool, "report", "--stdio", "--no-children",
                            "--percent-limit", "1", "--sort", "dso,symbol",
                            "-i", str(data)], report, 60, args.build)
            code = code or exported
        return code

    tool = require_tool("valgrind")
    options = [tool, "--tool=" + args.mode,
               "--log-file=" + str(output / "valgrind.log")]
    if args.mode == "memcheck":
        options += ["--leak-check=full", "--show-leak-kinds=all",
                    "--errors-for-leak-kinds=definite,indirect,possible",
                    "--track-origins=yes", "--error-exitcode=86"]
    elif args.mode == "callgrind":
        options += ["--callgrind-out-file=" + str(output / "callgrind.out")]
    else:
        options += ["--massif-out-file=" + str(output / "massif.out"),
                    "--time-unit=B", "--max-snapshots=64"]

    code = run(options + command, log, args.timeout, args.build)
    if args.mode == "memcheck":
        shutil.copyfile(output / "valgrind.log", report)
    else:
        name = "callgrind_annotate" if args.mode == "callgrind" else "ms_print"
        data = output / (args.mode + ".out")
        if data.is_file():
            exported = run([require_tool(name), str(data)], report, 60,
                           args.build)
            code = code or exported
    return code


def run_windows(args, command, output):
    if os.name != "nt":
        raise RuntimeError("Run WPR in native Windows PowerShell.")
    if not ctypes.windll.shell32.IsUserAnAdmin():
        raise RuntimeError("WPR needs an elevated terminal; nothing started.")

    tool = require_tool("wpr")
    status = subprocess.run([tool, "-status"], capture_output=True,
                            timeout=10, text=True, errors="replace")
    if status.returncode or "not recording" not in status.stdout.lower():
        raise RuntimeError("WPR is active or unknown; preserving its session.")

    profile = Path(__file__).resolve().with_name("hx_cpu.wprp")
    instance = "hx-analysis-" + str(os.getpid())
    log = output / "wpr.log"
    trace = output / "trace.etl"
    code = run([tool, "-start", str(profile) + "!HxCpu",
                "-instancename", instance, "-recordtempto", str(output)],
               log, 30, args.build)
    if code:
        run([tool, "-cancel", "-instancename", instance], log, 15, args.build)
        return code

    try:
        code = run(command, output / "run.log", args.timeout, args.build)
    finally:
        stopped = run([tool, "-stop", str(trace), "-skipPdbGen",
                       "-instancename", instance], log, 60, args.build)
        if stopped:
            run([tool, "-cancel", "-instancename", instance], log, 15,
                args.build)
    code = code or stopped
    exporter = find_tool("xperf")
    if exporter and trace.is_file():
        environment = os.environ.copy()
        environment["_NT_SYMBOL_PATH"] = str(Path(command[0]).parent)
        environment["_NT_SYMCACHE_PATH"] = str(output / "symbols")
        exported = run([exporter, "-i", str(trace), "-symbols", "-a",
                        "profile", "-detail"], output / "report.txt", 60,
                       args.build, environment)
        code = code or exported
    print("Open in WPA: " + str(trace))
    print("Filter CPU Usage (Sampled) by the workload's process.")
    return code


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", nargs="?", default="check",
                        choices=("check", "perf", "memcheck", "callgrind",
                                 "massif", "wpr"))
    parser.add_argument("-B", "--build", type=Path, default=Path("build"))
    parser.add_argument("--timeout", type=int, default=120,
                        help="stop this finite workload after N seconds")
    parser.add_argument("--replace", action="store_true",
                        help="replace this mode's previous local artifacts")
    words = sys.argv[1:]
    split = words.index("--") if "--" in words else len(words)
    args = parser.parse_args(words[:split])
    command = words[split + 1:]
    args.build = args.build.resolve()

    if args.mode == "check":
        for name in ("perf", "valgrind", "callgrind_annotate", "ms_print",
                     "wpr", "wpa"):
            print(name + ": " + (find_tool(name) or "not installed"))
        return 0
    if not command or not args.build.is_dir() or args.timeout <= 0:
        parser.error("Use an existing -B directory, timeout > 0 and -- CMD")

    cache = args.build / "CMakeCache.txt"
    cache_text = cache.read_text("utf-8") if cache.is_file() else ""
    enabled = "HX_OS_DEVELOPER_ANALYSIS:BOOL=ON" in cache_text
    print("Developer analysis symbols: " + ("enabled" if enabled else
                                           "not confirmed; see ANALYSIS.md"))
    if args.mode in ("memcheck", "callgrind", "massif"):
        for line in cache_text.splitlines():
            key, _, value = line.partition("=")
            if key == "HX_OS_SANITIZER:STRING" and value:
                parser.error("Use a non-Sanitizer build for Valgrind.")

    program = args.build / command[0]
    command[0] = str(program.resolve()) if program.is_file() else command[0]
    output = args.build / "hx-analysis" / args.mode
    if output.exists() and any(output.iterdir()) and not args.replace:
        parser.error("Report exists; use --replace to reuse " + str(output))
    output.mkdir(parents=True, exist_ok=True)
    artifacts = ("run.log", "report.txt", "valgrind.log", "perf.data",
                 "perf.data.old", "callgrind.out", "massif.out", "wpr.log",
                 "trace.etl", "result.json")
    for name in artifacts:
        (output / name).unlink(missing_ok=True)

    result = {"mode": args.mode, "command": command, "build": str(args.build),
              "analysis_symbols": enabled, "timeout_seconds": args.timeout}
    started = time.monotonic()
    try:
        if args.mode == "wpr":
            result["exit_code"] = run_windows(args, command, output)
        else:
            result["exit_code"] = run_linux(args, command, output)
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        result.update(exit_code=1, error=str(error))
        print(str(error), file=sys.stderr)
    finally:
        result["elapsed_seconds"] = time.monotonic() - started
        (output / "result.json").write_text(
            json.dumps(result, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8")
    print("Local report: " + str(output))
    print("Exit code: " + str(result["exit_code"]))
    return result["exit_code"]


if __name__ == "__main__":
    sys.exit(main())
