# Verification record and target plan

[简体中文](VALIDATION.zh-CN.md) · [README](../README.md)

This records development validation through 2026-09-29. A design, implemented
adapter, successful compile and successful target run are different states.
Local tests do not certify every operating-system release, filesystem, CPU,
libc or network provider.

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
| ARMv7-A/AArch64 embedded glibc | Shared Linux code + SDK toolchain | Pending SDK/CI | Pending board |
| Embedded musl | Shared Linux code, feature detection | Pending SDK/CI | Pending board |
| Embedded uClibc-ng | Shared Linux code, capabilities require review | Pending SDK | Pending board |
| Android | Reuse Linux/POSIX/epoll concepts | Not adapted | Not run |
| iOS | Reuse POSIX/Apple concepts | Not adapted | Not run |
| BSD | Reuse POSIX, needs identity/join/backend adaptation | Not adapted | Not run |
| FreeRTOS/bare metal | Out of current scope | — | — |

Android needs explicit bionic API-level and application permission/storage
decisions. iOS needs deployment/sandbox/background execution and lifecycle
checks; no fork is assumed. BSD needs native thread identifiers and finite
join semantics, not a Linux fallback. macOS currently uses no Objective-C;
its clock_gettime SDK/deployment requirement must be verified. Embedded
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
