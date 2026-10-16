# Optional CPU and memory analysis

[简体中文](ANALYSIS.zh-CN.md) · [Interval profiler](PROFILER.md) · [README](../README.md)

Use this workflow to find CPU hotspots, invalid memory access, leaks or heap peaks. The existing interval profiler remains unchanged. Detailed analysis is explicit: enabling symbols does not record anything, start threads or install tools.

## 1. Choose a question

| Question | Mode | Main result |
| --- | --- | --- |
| Which functions use CPU? | `perf` on Linux/WSL | `report.txt`, `perf.data` |
| Invalid access, uninitialized data or leaks? | `memcheck` | `report.txt` |
| Which calls execute many instructions? | `callgrind` | `report.txt`, `callgrind.out` |
| Where does the heap peak come from? | `massif` | `report.txt`, `massif.out` |
| Which Windows threads/functions use CPU? | `wpr` | `trace.etl`; `report.txt` if xperf is installed |

Run from the source root with Python 3.9+. Python is a developer-tool dependency, not a library runtime dependency. `python tools/hx_analyze.py check` only lists installed tools. Linux normally uses `python3`.

## 2. Enable symbols in an existing build

Reuse your working compiler, SDK and build directory. The commands use existing `build-shared-linux` and `build-shared-msvc` directories; substitute yours. Prepare dependencies using [DEPENDENCIES.md](DEPENDENCIES.md) first. Windows Release analysis requires matching Release dependencies; do not mix Debug static libraries into a Release link or suppress CRT mismatch warnings.

```sh
cmake -S . -B build-shared-linux -DCMAKE_BUILD_TYPE=Release \
    -DHX_OS_DEVELOPER_ANALYSIS=ON -DHX_OS_BUILD_BENCHMARKS=ON \
    -DHX_OS_BUILD_EXAMPLES=ON
cmake --build build-shared-linux --target \
    hx_os_benchmark hx_os_profiler_benchmark hx_os_profile_statistics
```

Windows PowerShell:

```powershell
cmake -S . -B build-shared-msvc -DHX_OS_DEVELOPER_ANALYSIS=ON `
    -DHX_OS_BUILD_BENCHMARKS=ON -DHX_OS_BUILD_EXAMPLES=ON
cmake --build build-shared-msvc --config Release --target `
    hx_os_benchmark hx_os_profiler_benchmark hx_os_profile_statistics
```

`HX_OS_DEVELOPER_ANALYSIS` defaults to OFF. ON adds private `-g -fno-omit-frame-pointer` options to owned GCC/Clang C/C++ targets, or `/Zi` and full linked PDBs for MSVC. Release linker optimization is retained. Vendor targets and exported consumer flags are unchanged. Frame pointers can affect generated code: compare builds with matching analysis, optimization and workload settings. Debug/Sanitizer speed is not a Release baseline.

Keep matching executables, libraries and symbols with each recording. Windows shared-library installs include the PDB when analysis is ON. Static-library analysis also needs the original compiler PDBs from its build. Separate consumer applications and dependencies must provide their own symbols; this option does not configure them.

## 3. Linux/WSL: CPU hotspots

```sh
python3 tools/hx_analyze.py perf -B build-shared-linux -- \
    bin/hx_os_benchmark 20000000
perf report -i build-shared-linux/hx-analysis/perf/perf.data
```

The existing benchmark accepts an optional iteration count, 1 through 100,000,000; its default remains 1,000,000. Twenty million iterations provide more samples than a very short run. The helper samples user-space CPU time at 199 Hz with DWARF stacks, without requiring hardware PMU events. `report.txt` groups sampled cost by library and function; percentages are not request latency or proof of inefficient code.

perf records into a temporary native `/tmp` directory, then copies results into the existing build and removes that temporary directory. This avoids WSL DrvFS rejecting writes from perf's mapped buffer with `Bad address`. No extra build directory is created. The helper disables build-ID cache writes and limits sampled data to 64 MiB. Missing symbols, incomplete stacks, lost samples or reaching the limit restrict interpretation. It does not change perf permissions, mount tracefs or silently substitute unavailable counters.

## 4. Linux/WSL: memory and instruction costs

Install Valgrind with your distribution's package manager if `check` reports it missing. On Ubuntu, first review `apt-get -s install --no-install-recommends valgrind libc6-dbg`, then install those packages. libc symbols improve reports. perf packages must suit the running kernel; WSL kernel names can differ from distribution package names.

The public statistics example creates a profiler and snapshot, prints results and releases them. It needs no private regression sources:

```sh
python3 tools/hx_analyze.py memcheck -B build-shared-linux -- \
    bin/hx_os_profile_statistics
python3 tools/hx_analyze.py callgrind -B build-shared-linux -- \
    bin/hx_os_profiler_benchmark
python3 tools/hx_analyze.py massif -B build-shared-linux -- \
    bin/hx_os_profiler_benchmark
```

Memcheck checks invalid access, use of uninitialized values and leaks with origin tracking. Exit 86 indicates detected errors; definite, indirect and possible leaks count as failures. Still-reachable memory is reported but is not automatically a leak. Other nonzero exits can come from the workload or tool itself: inspect the reports.

Callgrind reports instructions/calls by default, not native elapsed time or hardware cache measurements. Massif reports heap snapshots and allocation origins, not all RSS, mapped pages or coroutine stacks. Neither is a memory-correctness checker. Valgrind changes execution speed substantially; a timeout needs separate investigation and is not itself a memory defect.

Use a normal symbol-enabled build for Valgrind. The helper rejects nonempty `HX_OS_SANITIZER`; run ASan/UBSan and TSan separately in existing configurations. Pools and stackful coroutines require dedicated validation; existing Sanitizer fiber hooks do not certify Valgrind compatibility. Do not hide first-party errors with broad suppressions.

## 5. Windows: WPR capture and WPA inspection

Run native Windows Python in an elevated PowerShell. WPR records; WPA and optional xperf come from the Windows Performance Toolkit. Administrator rights are for capture only, not ordinary builds or library use.

```powershell
python tools/hx_analyze.py wpr -B build-shared-msvc -- `
    bin/Release/hx_os_benchmark.exe 20000000
wpa build-shared-msvc/hx-analysis/wpr/trace.etl
```

The supplied `hx_cpu.wprp` uses a 16 MiB circular CPU-sampling buffer. The helper preserves existing WPR recordings, owns a uniquely named session and stops it even when the workload fails. Temporary capture files stay inside the selected output directory. Long recordings can overwrite early samples; the ETL includes extra metadata, so its final size can exceed the ring size. No heap tracing or application registry settings are enabled.

In WPA, add **CPU Usage (Sampled)**, filter by executable/PID from `run.log`, and group **Process → Thread ID → Stack**. Add matching EXE/DLL PDB directories to the symbol path and load symbols. If xperf is installed, the helper also exports a function summary using symbols beside the executable and a local symbol cache. System symbols may remain unresolved; no symbol server is contacted automatically. The ETL includes other host processes: keep it private and filter before interpreting percentages. xperf and perf use different percentage bases; do not compare their percentages directly.

## 6. Own workloads, reports and limits

Everything after `--` is the executable and its arguments. Paths are relative to `-B`, which is also the workload's working directory; absolute paths are accepted. Replace the benchmark with a finite, representative business example.

Each mode reuses `<build>/hx-analysis/<mode>`. A second run requires `--replace`, which replaces that mode's known artifacts, not other modes. Default `--timeout 120` limits the workload. Timeout or Ctrl+C stops the launched process tree on Windows or its new process group on Linux, and records incomplete status 124. Use finite development programs, not production applications with unsaved work or services that daemonize outside the process group. It does not attach to or terminate an existing business process.

| File | Meaning |
| --- | --- |
| `result.json` | Command, symbol-option confirmation, elapsed time, exit code |
| `run.log` | Workload output, PID, interruption information |
| `report.txt` | Tool-specific text report, when available |
| `perf.data`, `callgrind.out`, `massif.out`, `trace.etl` | Corresponding viewer's raw data |

Exit 0 means the selected tool/workload completed under its rules, not that all library code is correct. A hot function, retained allocator cache or high RSS alone does not establish a defect. Keep sampling, native timing and memory diagnostics separate. Preserve matching binaries/symbols before rebuilding.

All `hx-analysis/` directories are ignored. Regressions, traces, logs and local configuration stay out of commits. Installed documentation includes the helper/profile under `tools/`; use the consuming application's existing build directory and matching symbols.

## 7. Return to normal

```sh
cmake -S . -B build-shared-linux -DHX_OS_DEVELOPER_ANALYSIS=OFF
```

Use the MSVC directory on Windows. Rebuild affected targets to remove analysis options from binaries. OFF restores previous build behavior, keeps the separate interval profiler available and preserves local reports. Neither ON nor OFF starts recording automatically.

References: [perf](https://perfwiki.github.io/main/tutorial/), [Valgrind](https://valgrind.org/docs/manual/manual.html), [WPR profiles](https://learn.microsoft.com/en-us/windows-hardware/test/wpt/authoring-recording-profiles), [WPR/WPA](https://learn.microsoft.com/en-us/troubleshoot/windows-server/support-tools/support-tools-xperf-wpa-wpr).
