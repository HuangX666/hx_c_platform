# Verification record and target plan

[简体中文](VALIDATION.zh-CN.md) · [README](../README.md)

This records development validation through 2026-09-30. A design, implemented
adapter, successful compile and successful target run are different states.
Local tests do not certify every operating-system release, filesystem, CPU,
libc or network provider.

The latest consolidated results appear under **Final delivery matrix** below.
Earlier checkpoints are historical; their smaller suite counts and pending
states are not the final coverage claims.

## OBS completion batch 1 (2026-09-30)

Windows MSVC x64 Debug and WSL GCC 15.2 Debug passed all 12 CTest suites.
The system-information, C++ platform and existing C++ pool suites passed
ASan/UBSan (3/3). SYSINFO=OFF passed its C and C++ contracts on both hosts.
Public C headers and all C++ facade headers compiled in isolation. The
source-width check uses the repository's explicit 8-column Tab setting.
macOS, Win7 runtime and embedded target validation remains pending.

## Object-layout checkpoint

On 2026-09-29, internal layouts were compared before/after on MSVC 19.40 x64,
MSVC 19.29 x86, GCC 15.2 x86_64 and Clang 21.1.8 x86_64. Each probe measured
25 C objects, nine C++ result-state types and 216 complete C++ job types.
The job matrix varies result kind, callable size and callable alignment.
No measured type grew or changed alignment. This is evidence for that matrix,
not a guarantee for every user-defined type, standard library or target ABI.

The C++ changes use an empty/result base when its size fits the aligned
reference-count boundary, and retain member storage for short nonempty
results. This avoids both extra void storage and a 32-bit alignment hole.
TLS keys, config documents and object pools place their existing 8-byte
aligned atomics before the smaller pointer members on 32-bit builds.

Representative sizes in bytes, before -> after:

| Object | MSVC x64 | MSVC x86 | Linux GCC/Clang x86_64 |
|---|---|---|---|
| TLS key | 40 -> 40 | 32 -> 24 | 40 -> 40 |
| Config document | 120 -> 120 | 80 -> 72 | 120 -> 120 |
| Object pool | 40 -> 40 | 32 -> 24 | 40 -> 40 |
| Empty callable, void job | 48 -> 40 | 32 -> 32 | 40 -> 32 |
| Empty callable, int job | 48 -> 48 | 40 -> 40 | 40 -> 40 |
| Empty callable, 16-byte aligned result job | 96 -> 80 | 80 -> 80 | 64 -> 64 |
| Empty callable, 64-byte aligned result job | 320 -> 256 | 320 -> 256 | 256 -> 128 |
| Empty callable, 128-byte aligned result job | 640 -> 512 | 640 -> 512 | 512 -> 256 |

The aligned probe results each contain a byte array whose size equals the
requested alignment. These are complete job sizes, including result and
callable storage, not allocator size classes or process RSS. C atomics keep
their original width/alignment. No packing directive, shared bitfield or
cache-line padding policy was introduced. Already compact structures and
intentional alignment probes were left unchanged.

All 13 public C headers match their pre-change hashes. Windows DLL exports
still match the 235 public functions. The C++ facade's internal template
layout has changed: rebuild its consumers and plugins together; do not mix
objects instantiated from different header versions. Source APIs are intact.
See [C++ storage and rebuild rules](CPP_POOL.md#storage-layout-and-rebuilding).

Validation after the layout change:

- Windows x64 static Debug, x64 DLL Release and x86/v142 Release: 10/10 suites
  per configuration, including isolated public headers.
- WSL GCC ASan/UBSan: 10/10 suites; TSan: the affected C++ task suite passed.
- Clang C++11 task suite passed against the existing compatible installed
  Linux C library. This is a facade check, not a rebuilt Clang C-library run.
- Windows DLL installation: 5/5 independent C/C++ consumers passed.
- New result-lifetime cases exercise 16/32/64/128-byte alignment, move-only
  transfer, discarding an unconsumed result and exceptions before a result is
  constructed. Existing cases cover cancellation, constructor failure,
  reference/void outcomes, allocator rollback and result retention.
- The five changed source files passed the skill's line-width check.

Temporary comparison sources, baseline copies and per-toolchain measurements
are under the local ignored `build-layout/` directory. No throughput gain,
cache-line improvement, ARM/Apple layout validation or hx_c_log performance
acceptance is inferred from the size reductions.

## Containers and C++ task checkpoint

C containers and the optional C++11 facade were added after configuration
parsing. Coroutines are deferred. The following are local compile/run results,
including the existing foundation/configuration regression suites:

| Configuration | Evidence |
|---|---|
| Windows x64/MSVC 19.40 Debug static, all parser backends | 10/10 suites |
| Windows x64/MSVC 19.40 Release DLL, all parsers and GNU iconv | 10/10 suites |
| Windows x86/MSVC 19.29/v142 Release, builtin parsers | 10/10 suites |
| CMake 3.16.8/NMake/MSVC 19.29 x64, C++11 and all parsers | 10/10 suites |
| WSL GCC 15.2 Release shared, all parsers and GNU iconv | 10/10 suites |
| WSL GCC ASan/UBSan, all parsers | 10/10 suites |
| WSL GCC TSan, all parsers | 10/10 suites |
| WSL Clang 21.1.8, strict C++11 facade linked to the installed C library | Task suite passed |
| Windows and WSL, containers/config/network/loop/pools OFF | 7/7 each |
| Windows static/shared and WSL shared installed consumers | 5/5 each |
| Windows C-only build, tests/examples OFF | Built without enabling CXX |
| Windows DLL and Linux so exported functions | 235 match public headers |

The container suite covers ring wraparound, random map operations against a
reference model, ordered AVL insertion/deletion and comparison-count bounds,
heap order, vector sort/search, iterator invalidation, byte keys, owned values,
hx_string clone/move/rollback, alignment and finite item/byte budgets. Injected
allocation failures reach initial hash buckets and later rehashes and verify
existing entries and complete release. Reserved POD vector append and deque
enqueue/dequeue are checked to cause no additional allocator calls. This is
allocation-path evidence, not a throughput benchmark or a hard latency bound.

The C++ suite exercises lambdas, ordinary/member pointers, move-only values,
reference/void results, user exceptions, constructor/allocation failures,
queue-full rejection before callable copying, queued/cooperative cancellation,
independent cancellation sources, same-pool deadlock rejection, close timeout
and retry, preparation/close races, and results that outlive pool destruction.
No sanitizer error was reported in the exercised paths; that is not proof of
all possible thread schedules. Public C headers and the C++ facade compile
independently and with repeated inclusion. Native event failure details are
preserved on submission rollback.

The 23 changed first-party source/build files passed the skill's line-width
check. The workflow passed actionlint and now explicitly tests CONTAINERS=OFF
in the minimal job. The pinned uthash header and license SHA256 values match
the retained vendor files. No vendor source was modified. The new bilingual
container/pool documents spell out ownership and performance limits.
UTF-8 without BOM / LF checks passed for 116 first-party files, and checked
README/documentation links resolve inside the source tree.

Reproduce the new tests with the normal build and CTest commands in README.
For a focused invocation on a built tree use `ctest -R containers` or
`ctest -R cpp_pool`, with `-C Debug`/`Release` for multi-configuration builds.
Use separate builds with `HX_OS_SANITIZER=address,undefined` and `thread` on
GCC/Clang Linux, and `python tools/verify_install.py build --config Release`
for the five installed consumers. The CMake 3.16 check used an explicit v142
CXX compiler path after refreshing a cached CXX=NOTFOUND result.

macOS and ARM/musl/uClibc-ng embedded SDK/board runs remain target work.
Windows 7 SP1/8.1 remains the intended baseline; compiling with v142 on the
current Windows host does not replace old-OS runtime validation. CI is
prepared but was not run remotely. No hx_c_log migration or five-times-baseline
performance acceptance was performed. Older checkpoint counts below are
history and should not be confused with the current ten-suite configuration.

## Configuration-module checkpoint

After the foundation checkpoint below, configuration parsing was added and
validated on the same development hosts:

| Configuration | Evidence |
|---|---|
| Windows x64/MSVC 19.40 Debug static, all five parser backends | 8/8 suites |
| Windows x64/MSVC 19.40 Release DLL, all parsers and GNU iconv | 8/8 suites |
| Windows x86/MSVC 19.29/v142 Release, builtin parsers | 8/8 suites |
| CMake 3.16.8/NMake/MSVC 19.29 x64, all parser backends | 8/8 suites |
| WSL GCC 15.2 Release shared, all parsers and GNU iconv | 8/8 suites |
| WSL GCC ASan/UBSan, all parser backends including vendor objects | 8/8 suites |
| WSL GCC TSan, all parser backends | config suite, including concurrency |
| Windows and WSL minimal, CONFIG=OFF | 5/5 suites on each |
| Installed Windows static/shared and WSL shared consumers | 3/3 each |
| Windows DLL and Linux so exported functions | 194 match public headers |

The config suite covers UTF-8 paths/values, JSON large integers and embedded
NUL, Unicode surrogate errors, exact XML mixed content, duplicate policies,
format rejection, line endings, resource budgets, individual allocation
failure points in every enabled parser, allocator reentry and four concurrent
parser threads. Native-only YAML/TOML features have separate cases.
Locale testing runs with a comma decimal locale when available (Windows here);
the optional locale case is skipped when neither configured locale is installed.
Final syntax fixes were rechecked on Windows and under ASan/UBSan.

The installed consumers include C99, C++11 and the configuration example.
Public headers compile independently in both languages. The 29 changed
first-party source/script files passed the skill's width checks after fixes;
UTF-8 without BOM / LF was checked across 96 first-party files. All 592 regular
files in the five parser archives were compared byte-for-byte with the
extracted source. The updated workflow passed actionlint, but has not run
remotely.

The older foundation numbers below are retained as history, not the final
configuration-module suite count. macOS, old Windows releases and embedded
SDK/board runs remain pending. No configuration fuzz campaign, complete
language-conformance certification, throughput comparison, or hx_c_log
integration/performance acceptance is claimed.


## Executed foundation configurations

| Configuration | Build / run evidence |
|---|---|
| Windows x64, MSVC 19.40, static Debug, GNU libiconv 1.18 | 7/7 suites |
| Windows x64, MSVC 19.40, shared Release, GNU libiconv 1.18 | 7/7 suites |
| Windows x86, MSVC 19.29/v142, static Release, codec OFF | 7/7 suites |
| CMake 3.16.8 + NMake/MSVC 19.29 x64, codec OFF | 7/7 suites |
| WSL Linux x86_64, GCC 15.2, static Debug, GNU libiconv 1.18 | 7/7 suites |
| WSL Linux x86_64, GCC 15.2, shared Release, GNU libiconv 1.18 | 7/7 suites |
| WSL GCC AddressSanitizer + UndefinedBehaviorSanitizer, system iconv | 7/7 suites |
| WSL GCC ThreadSanitizer, codec OFF | 7/7 suites |
| WSL Clang, thread pool OFF, codec OFF | 6/6 applicable suites |
| Windows / WSL minimal: network, loop, pools, codec OFF | 4/4 per host |
| Installed static/shared C99 and C++11 consumers, Windows / WSL | 2/2 each |
| Shared public exports, Windows / Linux | 170 functions match headers |

Public headers compile separately and twice per translation unit in C99 and
C++11 without PCH or unity assumptions. First-party targets use the recorded
warning profile with warnings as errors. Upstream iconv warnings are not counted
as a clean first-party build. The bundled source-width checker passed on the
covered first-party source/scripts; workflows were checked with actionlint.
UTF-8/BOM/LF and documentation links are checked separately.

Build directories contain CTest's Testing/Temporary/LastTest.log, build logs
and installed-consumer logs. They are ignored development artifacts. The
verification scripts and CI workflow remain in source. CI has been authored
and syntax-checked, not executed on GitHub in this workspace.

## Contract coverage

- core: alignment/context ownership, UTF-8 errors, status-returning clocks,
  retained events, timeout/retry, thread join and TLS-key lifetime.
- data: string aliasing/copy/move/format, invalid boundaries, embedded NUL,
  UTF-16 round trips, strict errors/replacement/progress, GBK/Shift-JIS/EUC-KR/
  CP1252 vectors when enabled, pool bounds/alignment/reallocation/cache,
  objects/arena, file append/sync/short read/EOF and directory retry.
- paths: Chinese and supplementary-plane characters, nested paths longer
  than 260 characters, malformed UTF-8 rejection and cleanup.
- lifecycle: injected allocator failures and rollback, delayed native TLS
  destructor, concurrent pool allocate/reallocate/collect, and budget retention
  while an upstream free callback is still running.
- pool: bounded queue rejection, pending cancellation, cleanup exactly once,
  timeout retention, retry, task handles outliving the pool, self-wait rejection.
- net: local TCP accept/connect/copy-send/read/cancel/deadline, UDP empty and
  truncated datagrams (direct and asynchronous), localhost DNS, timers, posting,
  callback reentry checks and completed handles outliving a loop.
- loop_concurrency: four posting threads, 2,000 completions, bounded pending
  work, capacity rejection, zero-budget close of timers and closed admission.
- options: disabled constructors and legacy encodings report NOT_SUPPORTED.

These tests do not include forced regular-file short-write injection,
disk-full/reboot durability, real UNC servers, every codepage mapping,
suspend/resume, signal interruption stress, every native provider, delayed
external DNS servers or plugin unload inside a running host. Those are target
acceptance tasks, not claims hidden inside "cross-platform supported".

## Target status

| Target | Design / source | Target build | Target run |
|---|---|---|---|
| Current Windows x86/x64 | Implemented | Passed | Passed |
| Windows 7 SP1 / 8.1 x86/x64 | API baseline retained | Current-host v142 build | Pending old OS |
| WSL Linux x86_64/glibc | Implemented | Passed | Passed in WSL |
| Native Linux device/server | Shared POSIX + epoll implementation | SDK dependent | Pending target |
| macOS Intel/Apple Silicon | POSIX + kqueue/Mach implementation | Pending Apple host | Pending |
| ARMv7-A/AArch64 embedded glibc | Shared Linux code + SDK toolchain | GCC 15.2 cross build passed | Partial QEMU; native board pending |
| Embedded musl | Shared Linux code, feature detection | Pending SDK/CI | Pending board |
| Embedded uClibc-ng | Shared Linux code, capabilities require review | Pending SDK | Pending board |
| Android | Reuse Linux/POSIX/epoll concepts | Not adapted | Not run |
| iOS | Reuse POSIX/Apple concepts | Not adapted | Not run |
| BSD | Reuse POSIX, needs identity/join/backend adaptation | Not adapted | Not run |
| FreeRTOS/bare metal | Out of current scope | — | — |

Android needs explicit bionic API-level and application permission/storage
decisions. iOS needs deployment/sandbox/background execution and lifecycle
checks; no fork is assumed. BSD needs native thread identifiers and finite
join semantics, not a Linux fallback. Only the optional Apple performance
activity adapter uses Objective-C/Foundation behind the C interface. Intel
10.13 and ARM64 11.0 deployment targets still require native validation. Embedded
acceptance must lock CPU/float ABI, endianness, libc version/configuration,
kernel floor, time_t width and atomic support.

## Reproduce and extend

~~~sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug --parallel 4
cd build
ctest -C Debug --output-on-failure
~~~

~~~sh
python tools/verify_install.py build --config Debug
python tools/check_exports.py build-shared/libhx_c_platform.so
~~~

On Windows use the DLL path and --tool dumpbin for export checks. Use the
minimal switches documented in the README for disabled-feature validation.
For sanitizers, use separate GCC/Clang build directories and set
HX_OS_SANITIZER to address,undefined or thread. Do not combine ASan with TSan.

The workflow plans Windows/Linux/macOS static/shared builds, minimal features,
sanitizers, native musl in a container, and ARMv7/AArch64 cross-builds with QEMU.
Emulation is useful but does not replace actual board/SDK acceptance.
On the actual board, build and run the same C example and applicable CTest
suites, test 32-bit/time64 limits and real filesystem/network behaviors, then
record compiler/SDK/kernel/libc versions and raw results.

## Performance boundary

The optional benchmark target was built and run on Windows and WSL. Its
aggregate timings are development instrumentation, not controlled acceptance
data or a before/after performance claim. No baseline subtraction or hx_c_log
migration benchmark was performed.

| Path | Current cost relevant to later measurement |
|---|---|
| Atomic operation | Out-of-line C call; native intrinsic/builtin; x86 CAS loop |
| String append with capacity | UTF-8 validation and copying; no pool/global lock |
| String self-alias/format | Temporary allocation can be required |
| Encoding | Scalar validation; legacy codec invocation per codepoint |
| File I/O | Native calls and path conversion on open/query |
| Event set/wait | Internal mutex and condition, retained predicate |
| Pool allocate/free | Per-pool mutex; upstream allocation only on growth |
| Task submission | Bounded queue lock, task/event allocation |
| Async request submission | Request/event/mutex allocation; default send copy |
| Loop pump | Bounded linear scan for requests/deadlines; native wait backend |

No SIMD or hand-written bulk-copy assembly has been introduced without evidence.
PAUSE/YIELD and matching atomics are localized. Measure size, throughput, tail
latency, allocation count, lock contention and syscalls on the actual target
before optimizing. A timer heap, reusable request storage or codec batching
should follow a demonstrated workload need. The locked hx_c_log five-times
and no-single-thread-regression criteria remain separate future gates.

## OBS completion batch 2 (2026-09-30)

Windows MSVC x64 and WSL GCC passed 14/14 suites; related ASan/UBSan
passed 3/3 (library, C++ convenience, version-negotiation example). The
LIBRARY=OFF C/C++ checks passed on both hosts. The test plugin is a separate
module with a tiny C ABI, avoiding embedding non-PIC static library objects
into the sanitizer fixture. macOS and Win7 runtime remain pending.

## OBS completion batch 3 (2026-09-30)

Windows/WSL full CTest passed 16/16; ASan/UBSan passed 4/4
(data, file_utils, cpp_platform, text_file_example). FILE_UTILS=OFF passed C/C++ tests on both
hosts; FILE_UTILS=ON with CONTAINERS=OFF passed on Linux. Tests exercised UTF-8
long paths, BOM policy, exact input limits, output preservation, same-file copy
rejection, backup collisions, cwd round trips, glob, bounded cleanup and
allocator ownership. Windows symlink creation was skipped for insufficient
privilege; Linux no-follow removal passed. Other native targets remain pending.

## OBS completion batch 4 (2026-09-30)

Windows/WSL full CTest passed 18/18; related ASan/UBSan passed 4/4
(env, environment_example, cpp_platform, file_utils). ENV=OFF checks passed
on both hosts. Coverage includes empty versus missing values, UTF-8, retained
outputs, conditional replacement, concurrent library calls and an output
allocator that reenters environment reading without deadlock. Windows path
regressions also passed after extracting the shared UTF-8/UTF-16 helper.

## OBS completion batch 5 (2026-09-30)

Windows/WSL full CTest passed 20/20; ASan/UBSan passed 6/6 (paths, env,
file_utils, C++ wrappers and both new C examples). PATHS=ON with ENV=OFF passed
4/4 and PATHS=OFF passed 3/3 on each host, including isolated C99/C++11 headers.
Coverage includes XDG relative/empty fallbacks, UTF-8, root preservation,
buffer size negotiation, allocation failure and queries without creation.
Windows known folders work in a test process without COM initialization.
macOS, Win7 and embedded native validation remain pending.

## OBS completion batch 6 (2026-09-30)

Windows/WSL full CTest passed 22/22; related ASan/UBSan passed 5/5
(core, lifecycle, thread_extra, cpp_platform, thread_options_example).
THREAD_EX=OFF passed 3/3 on both hosts with isolated C99/C++11 headers.
Tests verify capacity-limited initialization with canaries, invalid stack sizes,
actual Linux stack reservation, UTF-8 name truncation, cross-thread naming,
semaphore overflow without partial updates, deadline timeout and subsequent
successful retry/join. No additional lock/allocation enters default creation.
Native Windows 7 naming fallback and Apple behavior still need target runs.

## OBS completion batch 7 (2026-09-30)

Windows/WSL full CTest passed 24/24; ASan/UBSan 7/7 and TSan 5/5.
The relevant sets cover pipes, loop concurrency, C++ callbacks and the C transfer
example; ASan also covers file and socket regressions. Tests include copied and
borrowed writes, EOF/broken peer, preserved pending SIGPIPE, allocation rollback,
shared admission limits, cross-thread cancellation versus completion, zero
operation deadlines, callback reentry and close/retry without buffer retirement
violations. The same C example executes on both platforms.

With NETWORK=OFF on both hosts: LOOP=ON/PIPE=ON passed 4/4; LOOP=ON/PIPE=OFF
passed 3/3; LOOP=OFF/PIPE=ON passed 3/3. All build isolated C99/C++11 public
headers. One Windows minimal incremental link reported LNK1236 for pipe_native;
relinking that target unchanged passed, followed by its 3/3 tests. The root cause
of that transient linker failure is not established.

Measured size/alignment (bytes), using the optional `hx_os_layout_report` target
with HX_OS_BUILD_BENCHMARKS=ON:

| Object | MSVC x86 | MSVC x64 | Linux x64 |
| --- | --- | --- | --- |
| hx_os_operation (unchanged public type) | 72/8 | 88/8 | 88/8 |
| hx_os_pipe_operation (new) | 40/8 | 48/8 | 48/8 |
| hx_os_thread_options (new) | 12/4 | 16/8 | 16/8 |
| hx_os_socket (private) | 40/8 | 56/8 | 56/8 |
| hx_os_file (private) | 12/4 | 16/8 | 8/4 |
| hx_os_loop (private) | 112/8 | 176/8 | 176/8 |
| hx_os_request (private) | 696/8 | 768/8 | 288/8 |
| hx_os_pipe (private) | 36/4 | 56/8 | 56/8 |

The x86 result is a compiled/executed layout probe, not a full new x86 test run.
macOS, native Win7, ARMv7/AArch64 and musl execution remain pending. Layout
measurements and bounded syscall/allocation review are not throughput evidence.

## OBS completion batch 8 (2026-09-30)

Windows/WSL full CTest passed 26/26; related ASan/UBSan 6/6 and TSan 4/4.
NET_OPTIONS=OFF/POSITIONED_IO=OFF passed 3/3 per host with NETWORK enabled.
NETWORK=OFF/POSITIONED_IO=ON passed 4/4 per host; Windows also had FILE_UTILS
and all unrelated convenience modules disabled. Independent C99/C++11 headers
compiled. Tests cover four simultaneous positioned readers/writers and an
unchanged shared cursor, first-use handle publication, short reads, EOF, invalid
ranges, append/readonly rejection, explicit socket reuse, actual option lengths,
peer addresses and post-attachment mutation rejection.

Windows raw TCP_NODELAY returned one byte; the test now zero-initializes storage
and checks the actual native length instead of assuming sizeof(int). Native raw
results are unchanged. The private file layout probe measured x86 Windows 16/4,
x64 Windows 24/8, x64 Linux 12/4 (size/alignment); other reported objects remain
unchanged. These are layout/correctness results, not a throughput benchmark.

The pipe byte-count pointer correction follows Microsoft's documented Win7
[ReadFile](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-readfile)
and [WriteFile](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-writefile)
contracts. Win7/macOS/embedded runtime evidence still remains pending.

## OBS completion batch 9 (2026-09-30)

Windows/WSL full CTest passed 28/28; ASan/UBSan passed 5/5 (data, file_utils,
file_info, cpp_platform and file_info_example). FILE_INFO=OFF/BOM=OFF passed
3/3 on each host. All new public headers compiled independently as C99/C++11.
Tests cover capacity canaries, metadata failure preservation, native read-only
attributes, POSIX owner mode projection and symlink follow modes, directory
capacity retries/types and all five Unicode BOMs including overlapping UTF-32LE
prefixes. POSIX permission tests use a unique /tmp directory on the native
filesystem, avoiding assumptions about Windows-mounted permission emulation.
Windows ACL permissions are deliberately marked unknown, not inferred.

The extended info layout probe measured 72/8 bytes (size/alignment) on Windows
x86/x64 and Linux x64. Existing public layouts remain unchanged. Native macOS,
Win7 and embedded runs, Windows symlink privileges and a filesystem that forces
DT_UNKNOWN directory fallback remain separate target validation.

## OBS completion batch 10 (2026-09-30)

Windows/WSL full CTest passed 30/30; related ASan/UBSan and TSan each passed 4/4
(config, config_serialize, cpp_platform, config_serialize_example). Serialization
checks cover exact large JSON numbers, escaped NUL/Unicode, INI grouping,
properties round trips, incompatible-type rejection, depth/node/work/output/heap
limits, allocation failure, concurrent immutable readers and atomic save/reload.
All existing enabled parser backends passed after numeric-helper extraction.

Double checks cover signed zero, DBL_MIN/DBL_MAX, round trips, overflow, complete
token rejection and canonical infinities/NaNs. Both hosts passed under a comma
numeric locale, checking that the process locale and current decimal separator
were restored. WSL initially lacked a generated alternate locale; its existing
localedef and OS locale sources generated a private de_DE.UTF-8 under
build-obs-linux/test-locales. Only the test command used LOCPATH; no system locale
or installed dependency was changed. Full/ASan/TSan runs used that private locale.

CONFIG=ON with CONFIG_SERIALIZE/TEXT_NUMBER=OFF passed 3/3 per host, preserving
the existing config double getter. CONFIG=OFF/TEXT_NUMBER=ON passed 3/3 per host.
Windows CONFIG_SERIALIZE=ON/FILE_UTILS=OFF passed 4/4, including explicit save
rejection. Isolated C99/C++11 headers and source rules passed. Existing public
layouts were not expanded. macOS, Win7 and embedded runtime remain pending.

## OBS completion batch 11 (2026-09-30)

Windows/WSL full CTest passed 32/32. ASan/UBSan and TSan each passed 6/6
(timer, pipe, loop_concurrency, cpp_platform and the two async examples).
Coverage includes persistent request limits, allocator failure/no per-tick
allocation, slow callback coalescing, worker-thread stop/wait, cleanup once,
self-close rejection, loop destruction ordering, legacy one-shot behavior and
C++ exception/capture retirement. Public headers compiled as C99/C++11.

TIMER=OFF/LOOP=OFF passed 4/4 per host. Additional Windows LOOP=ON/TIMER=OFF
passed 3/3; Linux NETWORK=OFF/LOOP=ON/TIMER=ON passed 4/4. Layout probes ran
on Windows x86/x64 and Linux x64 (timer 40/8, 64/8, 64/8 bytes respectively).
macOS, Win7 and embedded runtime remain pending; no hard deadline is claimed.

## OBS completion batch 12 (2026-09-30)

Windows/WSL full CTest passed 32/32; ASan/UBSan and TSan each passed 3/3
(pool, cpp_pool, lifecycle). THREAD_POOL=OFF passed 2/2 per host. Public headers
compiled independently as C99/C++11. Identity tests distinguish the host, the
own worker, its cleanup and another pool. Self-wait/close reject deadlock, while
finished own-pool tasks remain readable. No layout, allocation, lock, syscall or
worker-path change; the query reuses existing TLS. Native target runs pending.

## OBS completion batch 13 (2026-09-30)

Windows/WSL full CTest passed 33/33; ASan/UBSan and TSan each passed 3/3
(core, error_text, cpp_platform). NETWORK=OFF passed 3/3 per host, with GAI
messages available without network initialization. Tests cover native text,
UTF-8 validity, unchanged numeric fallback, out-of-range codes, capacity/NUL/
canary handling, four concurrent callers and the C++ owned message. Public
headers compiled as C99/C++11. A Windows SDK macro collision in a test-local
variable was corrected before the final Windows build. Mach implementation and
its guarded test remain pending on macOS; Win7 and embedded runs are pending.

## OBS completion batch 14 (2026-09-30)

Windows full CTest passed 35/35; Linux 37/37 adds two isolated-process fault
tests. ASan/UBSan and TSan passed 7/7 each. Linux seccomp injected ENOSYS and EIO
only in the dedicated test executables: the real random-device fallback ran,
and failure clearing/native code/UUID preservation passed (neither was skipped).
Other checks cover a 1 MiB fill, four concurrent callers, UUID bits/format/
overlap/string output, CRC32 known and incremental vectors, and clearing bounds.
Public C99/C++11 headers and RANDOM=OFF combinations passed (3/3 per host).
Linux's minimal build also disabled explicit_bzero to exercise portable clearing.
No existing object layout or hot path changed. Windows legacy-provider failure,
early-boot entropy starvation, macOS, Win7 and embedded runs remain pending.

## OBS completion batch 15 (2026-09-30)

Windows full CTest passed 37/37; Linux 39/39. ASan/UBSan and TSan each passed
4/4 (data, vm, cpp_platform, mapped_file_example). VM=OFF passed 3/3 per host;
VM=ON with SYSINFO=OFF passed 4/4 per host, also FILE_UTILS=OFF on Windows.
New public C99/C++11 headers compile independently. Windows x86 VM tests and
x86/x64 layout probes ran successfully (layouts in PLATFORM_UTILITIES).

Tests cover rounded/zeroed pages, protection transitions, invalid permissions,
allocation failures, empty/oversize/unaligned maps, unchanged file cursor,
context retention, access after original file close, private-write isolation,
shared durable flush/reopen, append/pipe rejection and C++ moves/RAII. Native
execute policy rejection is permitted with a native error; no generated code is
executed. Truncation faults, network filesystem coherence, native unmap failure,
Apple hardened runtime, Win7 and embedded machines remain target validation.

## OBS completion batch 16 (2026-09-30)

Windows full CTest passed 39/39; Linux 41/41. ASan/UBSan and TSan each passed
4/4 (file_info, file_extra, cpp_platform, file_attributes_example). FILE_EXTRA
off passed 3/3 per host; on with FILE_INFO off passed 4/4 per host, also with
FILE_UTILS off on Windows. Public C99/C++11 headers passed. Windows x86 ran the
cross-process/metadata suite; layout probes measured file_times 40/8 on all
three tested ABIs.

Own test subprocesses verify shared/exclusive conflicts, elapsed monotonic
timeouts, retry after timeout, unrelated ranges and unchanged cursors. The
initial Windows helper failed because narrow CRT argv did not preserve UTF-8;
it now converts the native Unicode command line explicitly and passes with
Chinese filenames. Shell32 is a test-only dependency for this conversion.
Metadata tests cover selected timestamps, omitted access time, native errors
with and without error output, read-only flags, POSIX owner/group bits and
symlink nofollow updates on /tmp. Capacity canaries and C++ lock transfer pass.
macOS, Win7, 32-bit POSIX time ranges and remote/limited filesystems are pending.

## OBS completion batch 17 (2026-09-30)

Windows full CTest passed 42/42; Linux 44/44. ASan/UBSan and TSan each passed
7/7 (watch, loop_fd, pipe, timer, loop_concurrency, cpp_platform and the watch
example). Both modules off passed 4/4 per host; WATCH on with LOOP off passed
5/5 on Windows. Linux NETWORK off/local LOOP plus both watchers passed 5/5.
Public C99/C++11 headers passed. Windows x86 watch/stub tests passed 2/2.
Layout probes measured the values recorded in PLATFORM_UTILITIES.

Tests cover injected allocation failures with no transferred cleanup on
rejection, Unicode/supplementary-plane names, directory limits/rebuild overflow,
invalid POSIX names, callback cleanup once, self-close, timeout retention/retry
and C++ exception/capture release. External descriptor tests cover duplicated
ownership after original close, readiness I/O, request budgets, unsupported
native descriptors, cleanup reentry and loop destruction ordering.

Initial WSL watch tests on the Windows-mounted checkout received no events.
A direct native inotify probe, using both paths and /proc/self/fd, confirmed
96 notification bytes per native /tmp case and zero per /mnt/e case. statfs
identified tmpfs versus 9p. The adapter now conservatively rejects 9p instead
of accepting silent coverage; Linux tests use unique /tmp fixtures, and the
rejection is itself tested. No timeout increase was used to hide this behavior.
Native kernel-buffer saturation, unregister-failure retry, missing procfs,
Apple compilation/runtime, Win7 and embedded hardware remain target validation.

## OBS completion batch 18 (2026-09-30)

Windows MSVC x64 full CTest passed 46/46; WSL Linux passed 48/48.
ASan/UBSan and TSan each passed 7/7 (environment, pipe, system, process,
C++ platform, Unicode filename CLI and capture example). Public headers
compiled independently in C99/C++11. SYSTEM_UTILS/PROCESS off passed 5/5
on each host. Windows enabled process/system with LOOP/NETWORK/ENV/PATHS
disabled passed 5/5; Linux local capture with NETWORK/ENV/PATHS disabled
passed 7/7. Windows x86/v142 built and passed the four related tests,
including the existing file utility test and Unicode CLI example.

Controlled project-built children verify empty/space/quote/backslash/Chinese/
supplementary-plane arguments, complete environment and empty environment,
stdin EOF, simultaneous 320 KiB per output, timeout/resource retention/retry,
limit raise and explicit discard, raw binary/strict text conversion, repeated
wait, nonzero exit and actual Windows exit code 259. Injected allocator failure
at 64 call positions verifies zero outstanding context allocations and successful
cleanup. Snapshot enumeration reenters environment mutation outside the lock.
CPU checks verify AVX2 implies usable AVX. Linux emulation remains unknown.

The x86 v142 format checker rejected an existing non-ASCII snprintf format
despite /utf-8. Keeping the format ASCII and passing the same Chinese filename
as a %s argument preserves the test and passed; warnings were not disabled.
An incremental minimal Windows library reported an invalid COFF object;
rebuilding that library target resolved it and the configuration passed.

New size/alignment probes: Windows x86 string_list 8/4, process 24/8,
process_options 20/4, result 8/4, capture_options 16/4. Windows x64:
16/8, 32/8, 32/8, 8/4, 24/8. Linux x64: 16/8, 24/8, 32/8, 8/4, 24/8.
Existing public objects and loop/request sizes are unchanged.

Release division microbenchmark, one million divisions, single illustrative
samples: MSVC x64 helper/native 2509100/1998700 ns; WSL Clang x64
2604750/2521124 ns. The helper checks zero while the bare expression does not;
these samples do not establish a speedup or embedded performance claim.
No assembly was introduced. Native macOS, Win7 console inheritance, ARM/musl,
missing-procfs/OS-failure injection and actual embedded boards remain pending.

## OBS completion batch 19 (2026-09-30)

Windows MSVC x64 full CTest passed 48/48; WSL Linux passed 50/50.
Related ASan/UBSan and TSan each passed 7/7 (thread_extra, delayed, pool,
cpp_pool, cpp_platform, loop_concurrency, delayed_task_example).
Independent C99/C++11 headers passed. THREAD_EX/pool/delayed disabled
passed 4/4 per host; pool enabled but delayed disabled passed 5/5 per host;
enabled thread extensions/delayed tasks without networking passed 6/6
per host (Windows LOOP also disabled). Windows x86 passed 4/4 related tests.

Tests cover native affinity readback, Linux partial-mask rejection/restoration,
valid/invalid scheduling requests and permission status; shared capacity,
ready FIFO versus equal-due work, no early execution, future cancellation,
drain timeout escalated to cancel-pending, cleanup on workers, 64-task
cancel/close/destroy racing, retained tasks after pool destruction, C++
move-only arguments/results and cancellation, delayed allocation failure,
and close racing delayed callback preparation. Real-time priority is tested
only on a blocked worker and restored before releasing it.

Measured enabled private sizes/alignment: x64 task/pool/delay metadata
88/8, 104/8, 32/8 on Windows and Linux; x86 56/8, 64/8, 24/8.
There is no public layout change. Ordinary work adds no allocation/lock count;
future work adds one mutex and metadata, sorted insertion/cancel scanning,
clock sampling and native timed waits.

The Release pool benchmark submits 16384 no-op tasks to one worker in chunks
of 128, including wait/release. Static baseline executables were saved before
changing the batch-18 implementation (9ba7624). Each comparison alternates
baseline/current order across five processes per version, discards each
process's warmup sample and retains 25 timed samples per version.

Final medians: Windows baseline/current 9003000/9140800 ns (+1.53% elapsed),
Linux 12575598/12133141 ns (-3.52%). Ranges were 7593800..11159500 versus
7999800..11217300 ns on Windows, and 11517734..13372403 versus
11323393..13491747 ns on Linux. An earlier series had the opposite direction
(Windows 10000500/9567700, Linux 11984708/12118367), before placing the empty
future-list branch before the scheduler helper. The runs establish noisy
local observations, not a stable improvement or universal no-regression claim.
No assembly or extra ordinary-task locks/allocations were introduced.
Raw results remain ignored build artifacts (batch19-pool*-perf.json).
Native macOS, Win7, processor groups, CPU hotplug, ARM/musl and embedded boards
remain pending. This is unrelated to the unperformed hx_c_log migration.

## OBS completion batch 20 (2026-09-30)

Windows full CTest passed 51/51; WSL Linux 53/53. ASan/UBSan and TSan each
passed 8/8 (local, duplex, pipe, net, loop_concurrency, process, cpp_platform,
duplex_message_example). Both native AF_UNIX tests ran; they were not skipped.
Public C99/C++11 headers passed. Both new modules off passed 4/4 per host.
Standalone duplex with NETWORK/PIPE/SYSTEM_UTILS off passed 7/7 per host
(Windows LOOP also off), including the trimmed CLI argument path.
Local sockets with DUPLEX_PIPE/PIPE off passed Windows 4/4 (LOOP off) and
Linux 6/6 (LOOP on). Windows x86 related tests passed 4/4.

Tests verify UTF-8/supplementary-plane socket names, path bounds, unnamed peers,
existing-path preservation, accept/read NOW timeouts, connect continuation,
bidirectional stream data, short-read loops, IOCP/epoll read/write and cancel
retirement, loop CONNECT/ACCEPT refusal, parent network BUSY, file half-close,
EOF, write-after-shutdown, allocation failure cleanup and independent native
network initialization. Linux fills a real listen backlog until EAGAIN, then
accepts one connection and successfully retries the retained connecting object.
Windows process redirection rejects socket-backed file endpoints before spawn.

The first local address query tests found that validation requires a non-NULL
error-offset pointer. The local query and the shared trimmed CLI argument
adapter now supply it; the existing validation contract was preserved.

Layouts: local_address 108/1 and private local_state 112/4 on all measured
targets. With local support enabled, socket grows from 56/8 to 64/8 on x64,
40/8 to 48/8 on Windows x86. The same allocation appends state only for local
sockets. Windows duplex_file is 32/8 on x64 and 20/4 on x86; POSIX uses the
unchanged 12/4 file object. Public pre-existing layouts and operation/request
sizes did not change. No assembly or new third-party dependency was added.

Native macOS, Win7 fallback/provider absence, embedded/musl, firewall rejection,
wrong-peer accept flooding and native close-failure injection remain pending.
No Windows/WSL interop or TCP performance equivalence is claimed.


## OBS completion batch 21 (2026-09-30)

Windows full CTest passed 57/57; WSL Linux 59/59. ASan/UBSan related tests
passed 8/8. Public headers compiled independently as C99/C++11. All four
switches off passed 5/5 on each host. Windows x86 related tests passed 5/5.
Windows registry and performance enabled with RANDOM and SLEEP_INHIBIT off
passed 6/6, validating independent native linking and trimming.

The Windows suites exercised exclusive temporary HKCU keys, Unicode values,
REG_EXPAND_SZ literal reads, malformed native UTF-16, type mismatch, capacity
preservation, readonly access, two independent sleep tokens, allocation failure
rollback, idempotent activation and ownership transfer to a joined thread.
Registry fixtures delete only the exclusive leaf created by the current test;
exit cleanup is registered. Generated encoded C data was compiled and decoded,
including non-ASCII UTF-8 and C++11 constexpr/empty literals.

Linux power and registry results validate NOT_SUPPORTED paths only. ASan on
Linux does not validate the native Windows implementations. Native macOS,
Win7 rejection of execution requests, actual sleep policy behavior, permission
injection and embedded targets remain pending. Power requests are not a CPU
frequency guarantee. Apple implementation source is uncompiled on this host.
No TSan claim is made for Windows-only power ownership tests.

Measured private power state is 56/8 on Windows/Linux x64 and 32/4 on Windows
x86; obfuscated borrowed view is 24/8 and 12/4 respectively. Existing public
layouts remain unchanged. Objects allocate only when explicitly created;
no background thread, global allocator, hot-path hook or assembly was added.
The formatting configuration now also accepts the isolated Objective-C file;
C/C++ formatting rules otherwise remain the same. Third-party sources were
unchanged. Source checks and diff whitespace checks passed.


## OBS completion batch 22 (2026-09-30)

Windows full CTest 63/63; WSL Linux 65/65. Related ASan/UBSan 8/8 and TSan
6/6 passed. All three features off passed 4/4 on each host; Windows x86
related suites passed 7/7. Public C99/C++11 isolated headers, source rules
and diff checks passed. No vendor sources changed.

Calendar tests walk every day from 1900 through 2101, include 0001/9999 bounds,
negative milliseconds, leap centuries, invalid tokens, capacity canaries and
spec/output aliasing. Separate child environments test UTC and east-of-UTC
signs. German locale probes passed with Windows German_Germany.1252 and the
existing private Linux de_DE.UTF-8 locale. Only test processes set locale/TZ.
Translation tests exercise copied output, empty translation, missing fallback,
capacity limits, allocation failure in entry/array/output, preserved old values,
context ownership and four simultaneous read-only threads. Bit tests check
known bytes plus all 0..64 widths at offsets 0..7, failure preservation,
64-bit values, zero width, overflow, padding and EOF. C++ wrappers and the
combined C example ran.

Reader/writer layouts are 24/8 on Windows/Linux x64 and 12/4 on Windows x86;
lookup options are 24/8 and 16/4. Existing public layouts remain unchanged.
No existing hot path gained locks or allocations. Native Apple timezone rules,
Win7, ARM/musl and embedded board runs remain pending; Linux locale tests do
not count as Apple validation.


## OBS completion batch 23a: signals (2026-09-30)

Windows full CTest 66/66; WSL Linux 68/68. ASan/UBSan and TSan each passed
5/5 (data, calldata, signal, cpp_extensions, signal_notifications_example).
Feature-off suites passed 3/3 per host and Windows x86 related suites 4/4.
C99/C++11 isolated headers, source rules and diff checks passed.

Coverage includes generation-safe slot reuse, registration order, connect and
disconnect from callbacks, snapshot skipping, callback native error/cause
preservation, C++ exception containment, self/ancestor wait prevention,
connection/emission/nesting limits, rejection without cleanup, and allocation
failure through creation and emission. A custom allocator re-enters disconnect
during snapshot allocation, proving that user allocation runs outside the
handler lock. Four concurrent emitters are held in callbacks while another
thread disconnects; after retirement no later callback starts and cleanup runs
once. Separate barriers verify NOW close/wait failure while either callback or
cleanup is still running, followed by successful retry. Stack parameters test
all types, copied text including NUL, wrong/missing types, capacity exhaustion,
replacement without arena growth, copied-pack independence and failed-set
preservation. No network or external service is used.

Measured calldata is 776/8 on Windows/Linux x64 and Windows x86. Signal options
are 32/8 (x64) and 20/4 (Windows x86); private slots 168/8 and 160/8; handlers
88/8 and 56/8. Default slot storage is 43008 bytes on x64; matching-ID snapshots
cost 8 bytes per selected connection plus allocator overhead. No performance
speedup is claimed. Native Apple, Win7 and embedded/musl runs remain pending.
The settings and profiler portions of batch 23 are still pending at this commit.


## OBS completion batch 23b: settings (2026-09-30)

Windows full CTest passed 70/70; WSL Linux 72/72. Related ASan/UBSan and TSan
passed 8/8 each (data, config, config_serialize, settings,
settings_concurrency, settings_config, cpp_extensions, settings_example).
Feature-off tests passed 4/4 per host; Windows x86 related tests 5/5.
Windows SETTINGS on with CONFIG_SERIALIZE/TEXT_NUMBER/SIGNALS off passed
8/8, including double export through the shared private formatter. Linux
SETTINGS on with CONFIG/TEXT_NUMBER/SIGNALS/CONTAINERS off passed 8/8.
German locale probes passed on Windows and with the existing private Linux
locale. Core code does not change the process locale. Public C99/C++11 headers,
source rules and whitespace checks passed; vendor files remain unchanged.

Tests cover every value category, NUL and supplementary-plane keys/strings,
duplicate keys, 64-level construction, finite-number rejection, default-only
fallback and strict type errors. Snapshots remain unchanged and survive owner
close; a deep copy into the default allocator stays valid after the source's
custom context is destroyed. Creation, replacement and copy allocation failures
roll back. Import and serialization inject failure at 32 allocation positions
each, preserving old settings/output and freeing temporary trees. Allocation
re-enters a getter outside locks; destruction-time deallocation re-enters and
receives CLOSED safely. Observers re-enter reads/writes, hit their nesting bound,
see immutable event values and detect self-close without stopping the object.
A blocked observer proves timeout retention and retry; four writers publish
600 updates without losing distinct keys while holding snapshots concurrently.

JSON/INI/properties roundtrips, immutable source documents, explicit-only save,
default preservation, pointer rejection, unsupported root/integer conversion,
Unicode paths, corrupt-file rollback and native file-not-found detail were
verified. C++ wrappers and the direct C example ran. Initial compilation found
a test fixture missing directory_create's parents argument; the fixture now
uses explicit nonrecursive creation. No existing directory is reused.

Size/alignment on Windows/Linux x64: options 80/8, change event 48/8, value
56/8, entry 24/8, owner 144/8. Windows x86: 44/4, 32/8, 40/8, 12/4, 80/8.
These are new types; existing public layouts did not change. Updates copy a
packed root/key block and retain unchanged child nodes; no performance speedup
is claimed. Native Apple, Win7, ARM/musl and board runs remain pending.
Profiler (23c) and atomic completion (24) remain pending at this commit.

## OBS completion batch 23c: profiler (2026-09-30)

Windows full CTest passed 72/72; WSL Linux 74/74. ASan/UBSan and TSan each
passed 4/4 related suites (profiler, lifecycle, C++ extensions, C example).
Windows x86 passed the same four suites. Independent public C99/C++11 headers
compiled. PROFILER=OFF passed 3/3 on both hosts; the Linux profiler also passed
4/4 with config, signals and containers disabled.

Tests cover 201 merged completed paths, four concurrent recording threads,
inclusive/exclusive consistency, native TLS exit aborts, explicit thread
restart, UTF-8/escaped names, output/record/depth limits, atomic save, allocator
failure/reentry, caller-scope deadlock, foreign idle TLS close timeout and
retry. A warmed path repeated 1000 times made zero context allocations;
this does not measure all native runtime activity or claim a speedup.

Measured size/alignment on Windows and Linux x64: options 56/8, owner 656/8,
record 96/8, frame 24/8, thread cache header 16/8. Windows x86: 32/4, 336/4,
80/8, 24/8, 8/4 respectively. Names and bounded frame arrays are additional.
Hot entries/leaves retain mutex sections, clock reads and TLS lookups.
Apple, Win7, ARM/musl and board runs remain pending. Batch 24 is next.

## OBS completion batch 24: atomics (2026-09-30)

Windows full CTest 74/74; WSL Linux 76/76. Related ASan/UBSan and TSan
each passed 10/10 (atomic, publication example, core, lifecycle, pool,
loop concurrency, watch, settings concurrency, profiler, C++ extensions).
Windows x86 passed 5/5; trimmed builds passed 5/5 on each host. Public C99
and C++11 headers compiled independently. Size/alignment is 8/8 for the old
64-bit type, 4/4 for atomic32; pointers are 8/8 on x64 and 4/4 on x86.

Tests exercise all legal orders plus conservative invalid-order fallback,
success/failed/weak CAS expected values, signed-boundary wrapping, bitwise
operations, 80,000 contended increments and 10,000 payload publications
observed through failed acquire pointer CAS. Existing claim/publication
call sites were reviewed; no stronger hidden order was required.

GCC TSan emitted its documented unsupported-fence diagnostic. Only this
diagnostic is nonfatal for atomic.c under GCC TSan; the actual implementation
is unchanged. TSan tests do not use fences as their publication mechanism.
Fences were compiled/exercised, not certified by TSan. No assembly speedup
or target ARM memory-order validation is claimed. See [contracts](ATOMICS.md).
All 24 implementation batches are now present; final package/ABI matrix
verification follows separately.

## Final delivery matrix (2026-09-30)

All 24 approved implementation batches are present. Batch 23 has separate
signal, settings and profiler commits. No remote push, hx_c_log migration,
coroutine implementation, new third-party dependency or vendor edit occurred.

| Configuration | Final evidence |
| --- | --- |
| Windows MSVC x64 static Debug, external parsers enabled | 74/74 CTest |
| Windows MSVC x64 shared Release | 74/74 CTest |
| Windows MSVC v142 x86 static Debug | 74/74 CTest |
| WSL GCC 15.2 static Debug / shared Release | 76/76 each |
| WSL Clang 21.1.8 static Release | 76/76 |
| WSL ASan + UBSan / TSan, external parsers | 76/76 each |
| CMake 3.16.8, NMake, MSVC v142 x64 Release | 74/74 |
| All optional modules OFF, shared, Windows / WSL | 45/45 each; 576 exports each |
| Installed Windows / Linux, static / shared | 8/8 mixed C99/C++11 and 6/6 C-only consumers per configuration |
| Baseline 7cf52fc headers/tests with current DLL / so | 10/10 each |
| Full shared ABI, Windows / Linux | 576 public functions match headers, including paths and settings |
| C-only minimal Windows core | Compiled without enabling CXX |
| ARMv7 hard-float / AArch64 glibc, GCC 15.2 | Cross compilation including isolated C99/C++11 headers passed |
| ARMv7, QEMU 10.2.1 user mode | 72 passed, 2 skipped, 2 failed out of 76 |
| AArch64, QEMU 10.2.1 user mode | 73 passed, 2 skipped, 1 failed across the full 76 entries |

The full ARMv7 run and the split AArch64 run retain failures rather than
claiming native board acceptance. QEMU's two random fault-injection cases
return the pre-existing skip code 77. A native-API diagnostic probe reproduced
both remaining differences outside the library: 32-bit sysinfo returned
3696713728 bytes with unit 1 while host procfs and native x64 reported
20876582912 total bytes; available bytes were about 20 GB. Both emulated
architectures returned success for posix_spawn of a nonexistent file, followed
by child status 127. Native x64 returned ENOENT as expected. The library
preserved those native results. Do not clamp memory metrics or fabricate
spawn errors to hide the emulator behavior. CI explicitly excludes that
process test and ARMv7 sysinfo in its QEMU-only job; board acceptance must
include them. Other process-capture examples and C++ process tests passed
under binfmt with QEMU_LD_PREFIX.

Windows shared validation found and fixed the generated-obfuscation test's
DLL search directory. CMake 3.16/NMake validation found supplementary-plane
command-line text becoming question marks; its generator now reads an actual
UTF-8 input file. ARMv7 compilation found a size_t versus INT64_MAX check that
was always false; the check now exists only at applicable widths. Optional
C++ discovery moved to the tests/examples' common parent, preserving a C-only
core. Package validation explicitly builds a separate LANGUAGES C consumer.

The 22 pre-existing public struct/union declarations were compared with the
baseline and are unchanged (ignoring comments/whitespace); original operation
and error layouts remain intact. The updated symbol checker includes the
14 macro-declared path functions and hx_settings exports. Full and minimal
shared libraries have the same public symbol set. All tested public headers
compile independently and twice in C99/C++11. First-party UTF-8/no-BOM/LF,
changed source rules, documentation links and actionlint pass. Generated
outputs/dependency caches remain ignored; vendor files match the Git baseline.

### Cost and performance observations

| Path | Cost retained or added |
| --- | --- |
| Existing atomics | Direct calls, constant-order dispatch; no allocator/callback; MSVC may be stronger |
| Ordinary pool tasks | No added ordinary-task allocation/lock count; delayed tasks share capacity and worker threads |
| Async pipe/timer/process composition | Bounded request/stream storage and native I/O; no hidden unlimited queue or automatic kill |
| Signals | Bounded callback snapshot per emit; callbacks outside lock; explicit in-flight retirement |
| Settings | Copied root/key storage plus retained immutable children; allocation and observer calls outside publication lock |
| Profiler | Warm paths allocate no context storage; entry/leave still uses mutex sections, TLS and monotonic clock reads |

The final Release pool comparison repeats batch 19's fixed workload against
the saved static batch-18 binary: 16384 no-op tasks, one worker, chunks of
128, five alternating processes per version, five retained samples after
each warmup. Windows baseline/current median was 9506100/9505400 ns
(-0.01% elapsed); ranges 8128300..11529500 / 7505200..11280200.
Linux was 12574817/12832931 ns (+2.05% elapsed); ranges
11615759..14325685 / 11078454..17594985. The overlapping/noisy observations
do not prove a speedup or universal nonregression. Raw measurements are
final-pool-perf.json in the corresponding ignored build directories.
The hx_c_log five-times-baseline and single-producer nonregression gates
remain future migration acceptance, not evidence from this pool benchmark.

### Reproduce and outstanding target work

Use the existing per-batch logs plus final-build.log/final-test.log,
final-install.log and final-abi.log in the named build directories. Run
tools/verify_install.py before tools/verify_abi.py for a shared configuration.
The latter reads old headers/test sources through Git into its own ignored
build directory; it does not check out or modify baseline/vendor sources.
tools/check_exports.py accepts nm on Linux or --tool dumpbin on Windows.
The CMake 3.16 run uses ctest from inside the build directory.

cmake/minimal.cmake is a 3.16-compatible initial-cache example, not a new
runtime configuration layer. Override selected switches after -C and use the
actual SDK toolchain/sysroot. Native Apple Intel 10.13 / ARM64 11.0, Win7/8.1,
musl/uClibc-ng and physical embedded boards remain unverified. Apple source
and target-local Objective-C warnings are implemented but uncompiled here.
Cross-build warnings allowed by the existing GCC maybe-uninitialized policy
remain diagnostics, not a clean-warning claim. No remote CI was executed.
First-party distribution licensing is still undecided.

Failed batch-3 fixture directories under ignored build outputs remain:
automatic approval review rejected their requested recursive cleanup with
"blocked by policy". No alternative deletion bypass was attempted.

## 2026-10-01: common utilities and Android baseline

This extends the completed 24 batches; earlier counts and evidence stay intact.
See [common contracts](COMMON_UTILITIES.md) and [Android usage](ANDROID.md).

| Configuration | Result |
| --- | --- |
| Windows MSVC x64 Debug static, external parsers | 82/82 |
| Windows MSVC x64 Release shared | 82/82 |
| Windows MSVC v142 Win32 Debug static | 82/82 |
| Windows CMake 3.16.8 / NMake / v142 | 82/82 |
| WSL GCC Debug static / Release shared | 84/84 each |
| WSL Clang Release | 84/84 |
| WSL ASan/UBSan / TSan | 84/84 each |
| Windows all optional modules OFF, shared | 52/52 |
| WSL only PROCEDURES ON, shared | 52/52, including parameter packs without SIGNALS |
| Windows/WSL static/shared installed consumers | C/C++ 10/10 and pure C 7/7 per configuration |
| 7cf52fc headers/tests with current DLL/so | 10/10 per host |
| Windows full/minimal, WSL full/procedures-only exports | 624 each, no missing public or leaked private symbols |

Eight Android cross-builds passed using existing NDK 28.2.13676358 / Clang
19.0.1: both ARM ABIs at API 21/28 with default shared modules, both at API 21
with minimal shared modules, and both at API 21 with default static modules.
Each includes C99/C++11 public headers, tests, examples and benchmarks.
adb found no device: target execution, performance and sandbox acceptance
remain pending. CI configuration passes actionlint; no remote CI run occurred.

New coverage includes UTF-8/NUL/aliasing, transactional failure, allocation
faults, catalog overlays, lexer position/sticky errors, 48 independent BigInt
wide-arithmetic reference vectors, rounding/overflow, monotonic waits/repeat,
procedure self-wait/in-flight close/retry, allocator reentry, warm allocation
counts and C++ exception conversion. Win32 also exercises portable arithmetic.
Affected Windows suites passed again after adding the QPC spin-budget guard.

### Layout and performance observations

The 47 pre-existing named public struct/union declarations in 298bf51 are
unchanged after removing whitespace/comments. New size/alignment in bytes:

| Type | MSVC x64 / Linux x64 | MSVC Win32 |
| --- | --- | --- |
| hx_os_uint128 | 16/8 | 16/8 |
| hx_os_token | 48/8 | 24/4 |
| hx_os_lexer_options | 40/8 | 24/4 |
| hx_os_proc_options | 32/8 | 20/4 |

Natural alignment, no packing. ASCII edits/wide arithmetic add no allocation
or lock. Registered procedure calls allocate no context storage, but still
scan slots, lock and invoke indirectly. Signals share the existing retirement
implementation. Ordinary pool task paths are unchanged.

Release shared, 100000 iterations, median of three local observations (ns/op):

| Operation | Windows x64 | WSL Linux x64 |
| --- | --- | --- |
| Relaxed atomic increment | 8.58 | 8.36 |
| ASCII lower/upper pair | 24.91 | 22.20 |
| 64*64/64 | 198.37 | 6.24 |
| Single named procedure | 341.46 | 388.18 |

Windows uses portable wide arithmetic; Linux x64 uses compiler 128-bit
integers. No cross-platform parity claim. Windows benchmark intervals use
QPC so short operations are not quantized to zero by the deadline clock.
Raw outputs: build-shared-msvc/util-perf.log and build-shared-linux/util-perf.log.
There is no equivalent old workload baseline, so these are not speedup claims.

Default 200 us spin, 2 ms request, 30 waits per run: Windows mean lateness was
about 13.1..13.5 ms; WSL about 0.31..0.72 us. These are local clock/scheduler
observations, never precision guarantees. Windows keeps its existing deadline
domain and bounds spinning independently with QPC before falling back to sleep.
No global timer-resolution change. hx_c_log performance gates remain future
migration acceptance.

### History and reproduction

27 of 28 historical commit messages were shortened while retaining every
source tree, author and timestamp. Root 7cf52fc remains; old history is kept
on backup/pre-message-cleanup-20261001. Old tip 622ee2e maps to 298bf51;
historical document hashes remain accessible through the backup. No push.
New commits use short Chinese subjects.

Build directories contain util-build/test/install/abi logs; final focused
checks use util-final/util-budget prefixes. Android records are report.json
and report-minimal.json. Outputs/SDK/dependency caches remain untracked and
vendor sources unchanged. Existing macOS, Win7/8.1, musl and physical-board
validation gaps remain.
