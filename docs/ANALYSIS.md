# Optional CPU and memory analysis

[简体中文](ANALYSIS.zh-CN.md) · [Interval profiler](PROFILER.md) · [README](../README.md)

Use the developer analysis workflow (`tools/hx_analyze.py`) when you want to find **CPU hotspots**, check for **invalid memory accesses, uninitialized reads, or memory leaks**, or inspect **heap allocation peaks** during development. It complements the manually instrumented [interval profiler](PROFILER.md) and stays completely off in normal builds—enabling debug symbols at build time never starts background recording or installs external hooks at runtime.

For post-mortem crash collection on customer Windows machines, use the separate [WER LocalDumps configuration API](LOCAL_DUMPS.md) instead (developer analysis flags and WPR traces do not configure crash dumps, and crash dumps do not replace memory checking or CPU profiling).

## 1. Choose a question

Pick the `hx_analyze.py` mode that answers your current engineering question:

| Question | Mode | Platform & Tool | Primary Artifacts |
| --- | --- | --- | --- |
| Which functions spend the most user-space CPU time? | `perf` | Linux / WSL (`perf`) | `report.txt`, `perf.data` |
| Are there out-of-bounds accesses, uninitialized reads, or memory leaks? | `memcheck` | Linux / WSL (Valgrind Memcheck) | `report.txt` |
| Which call paths execute the most CPU instructions? | `callgrind` | Linux / WSL (Valgrind Callgrind) | `report.txt`, `callgrind.out` |
| Which call stacks account for the heap memory peak? | `massif` | Linux / WSL (Valgrind Massif) | `report.txt`, `massif.out` |
| Which Windows threads and functions spend the most CPU time? | `wpr` | Windows (WPR capture + WPA / optional `xperf`) | `trace.etl` (plus `report.txt` when `xperf` is installed) |

Run the helper script from the repository root using Python 3.9+ (Python is only used by this developer script, never by the runtime library). To check which profiling tools are installed on your machine, run (`python3` on Linux):

```sh
python3 tools/hx_analyze.py check
```

## 2. Enable symbols in an existing build

To get readable function names and source lines in call stacks, configure your existing build directory with `-DHX_OS_DEVELOPER_ANALYSIS=ON` and rebuild the targets you want to inspect (replace `build-shared-linux` or `build-shared-msvc` with your build directory; prepare third-party dependencies via [DEPENDENCIES.md](DEPENDENCIES.md) first):

```sh
# Linux / macOS:
cmake -S . -B build-shared-linux -DCMAKE_BUILD_TYPE=Release \
    -DHX_OS_DEVELOPER_ANALYSIS=ON -DHX_OS_BUILD_BENCHMARKS=ON \
    -DHX_OS_BUILD_EXAMPLES=ON
cmake --build build-shared-linux --target \
    hx_os_benchmark hx_os_profiler_benchmark hx_os_profile_statistics
```

```powershell
# Windows PowerShell (Visual Studio generator):
cmake -S . -B build-shared-msvc -DHX_OS_DEVELOPER_ANALYSIS=ON `
    -DHX_OS_BUILD_BENCHMARKS=ON -DHX_OS_BUILD_EXAMPLES=ON
cmake --build build-shared-msvc --config Release --target `
    hx_os_benchmark hx_os_profiler_benchmark hx_os_profile_statistics
```

> [!IMPORTANT]
> **What `HX_OS_DEVELOPER_ANALYSIS=ON` Changes**
> - `HX_OS_DEVELOPER_ANALYSIS` defaults to `OFF`. Turning it `ON` adds private `-g -fno-omit-frame-pointer` flags to first-party GCC/Clang C/C++ targets, or `/Zi` and full linked `.pdb` symbol files on MSVC while preserving `Release` linker optimizations.
> - Third-party vendor targets and exported CMake consumer flags remain untouched (for Windows `Release` analysis, always link matching `Release` dependency builds—never mix `Debug` static libraries into a `Release` link).
> - Because preserving frame pointers can slightly affect register allocation and codegen, keep analysis flags, optimization levels, and workloads identical when comparing before/after benchmark numbers.

## 3. Linux/WSL: CPU hotspots

Run `hx_os_benchmark` under `perf` to sample user-space CPU call stacks:

```sh
python3 tools/hx_analyze.py perf -B build-shared-linux -- \
    bin/hx_os_benchmark 20000000
perf report -i build-shared-linux/hx-analysis/perf/perf.data
```

- **Iteration count**: `hx_os_benchmark` accepts an optional iteration count from `1` to `100,000,000` (default `1,000,000`). Passing `20000000` gives a longer run so the `199 Hz` user-space CPU clock sampler (with DWARF stack unwinding, requiring no hardware PMU counters) collects enough samples.
- **WSL DrvFS compatibility**: To avoid WSL DrvFS rejecting writes from `perf`'s memory-mapped buffer with `Bad address`, `hx_analyze.py` records into a temporary native `/tmp` directory first, copies the completed artifacts (capped at `64 MiB`) into `<build>/hx-analysis/perf/`, and cleans up the temporary directory.
- **Interpreting sampled percentages**: `report.txt` groups sampled CPU cost by shared library and function. Remember that a high percentage means the function did most of the active CPU work during the run—it is not a latency measurement or automatic proof of an inefficiency.

## 4. Linux/WSL: memory and instruction costs

If `hx_analyze.py check` reports that Valgrind is missing, install it through your distribution package manager (for example, `valgrind` and `libc6-dbg` on Ubuntu so `libc` frames resolve cleanly).

You can run Memcheck, Callgrind, and Massif directly against the public profiler examples:

```sh
python3 tools/hx_analyze.py memcheck -B build-shared-linux -- \
    bin/hx_os_profile_statistics
python3 tools/hx_analyze.py callgrind -B build-shared-linux -- \
    bin/hx_os_profiler_benchmark
python3 tools/hx_analyze.py massif -B build-shared-linux -- \
    bin/hx_os_profiler_benchmark
```

- **`memcheck` (invalid memory access and leak checker)**: Tracks out-of-bounds accesses, uninitialized values (with origin tracking), and memory leaks. Detected errors or definite/indirect/possible leaks fail with exit code `86` (`still reachable` blocks are reported in `report.txt` without failing the run).
- **`callgrind` (instruction and call-graph profiler)**: Counts executed instructions and function calls to help analyze algorithmic scaling (instruction counts are not wall-clock time or hardware cache measurements).
- **`massif` (heap peak profiler)**: Snapshots heap allocations over time to show which call stacks contributed to the peak heap footprint (covers heap allocations rather than all process RSS, mapped pages, or coroutine stacks).
- **Do not combine Valgrind with compiler Sanitizers**: Run Valgrind against a standard symbol-enabled build (`HX_OS_SANITIZER` empty). `hx_analyze.py` rejects build directories configured with ASan, UBSan, or TSan so the two runtimes do not conflict.

## 5. Windows: WPR capture and WPA inspection

On Windows, use `wpr` (Windows Performance Recorder) and `wpa` (Windows Performance Analyzer) from the Windows Performance Toolkit. Because kernel ETW CPU sampling requires administrator privileges, run the capture command from an **elevated PowerShell** using native Windows Python (building and using the library itself never requires elevation):

```powershell
python tools/hx_analyze.py wpr -B build-shared-msvc -- `
    bin/Release/hx_os_benchmark.exe 20000000
wpa build-shared-msvc/hx-analysis/wpr/trace.etl
```

- **Isolated circular buffer**: The bundled `tools/hx_cpu.wprp` profile configures a `16 MiB` circular CPU-sampling buffer under a dedicated session name and stops its session cleanly even if the target workload exits with an error.
- **Inspecting stacks in WPA**:
  1. Open `trace.etl` in WPA and add the **CPU Usage (Sampled)** graph.
  2. Filter to the target executable and PID logged in `run.log` (system-wide ETW traces capture all active processes on the host, so always filter to your process before reading percentages), then expand **Process → Thread ID → Stack**.
  3. Add your build directory containing matching `.exe` / `.dll` `.pdb` files to WPA's symbol path and load symbols (if `xperf` is installed, the script also exports a text function summary to `report.txt`; note that `xperf` and Linux `perf` compute percentages against different denominators and should not be compared directly).

## 6. Own workloads, reports and limits

Everything after `--` is the target executable and its arguments, resolved relative to the `-B` build directory (which also serves as the working directory). Replace the benchmark target with any finite, representative workload:

- **Output directory and `--replace`**: Each mode writes to `<build>/hx-analysis/<mode>/`. Re-running the same mode requires passing `--replace`, which overwrites only that mode's artifacts while leaving other modes untouched.
- **Timeout handling (`--timeout`)**: Runs default to `--timeout 120` seconds. If a run times out or receives `Ctrl+C`, the helper terminates the launched process tree (Windows) or new process group (Linux) and records exit status `124`. Only run finite development workloads—never point the runner at production services with unsaved state or processes that daemonize outside the process group.

Generated files in `<build>/hx-analysis/<mode>/`:

| File | Description |
| --- | --- |
| `result.json` | Executed command, symbol-flag verification, elapsed time, and exit code |
| `run.log` | Workload `stdout`/`stderr` output, PID, and timeout/interruption notes |
| `report.txt` | Text summary generated by the selected tool (when supported) |
| `perf.data`, `callgrind.out`, `massif.out`, `trace.etl` | Raw trace files for `perf report`, `kcachegrind`, `ms_print`, or `wpa` |

## 7. Return to normal

When you finish profiling, set `HX_OS_DEVELOPER_ANALYSIS=OFF` and rebuild affected targets to return to standard binaries (existing reports under `hx-analysis/` are preserved):

```sh
cmake -S . -B build-shared-linux -DHX_OS_DEVELOPER_ANALYSIS=OFF
```

References: [Linux `perf` Tutorial](https://perfwiki.github.io/main/tutorial/), [Valgrind User Manual](https://valgrind.org/docs/manual/manual.html), [Authoring WPR Recording Profiles](https://learn.microsoft.com/en-us/windows-hardware/test/wpt/authoring-recording-profiles), and [WPR / WPA Performance Analysis](https://learn.microsoft.com/en-us/troubleshoot/windows-server/support-tools/support-tools-xperf-wpa-wpr).
