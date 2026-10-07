# Verification record and target plan

Entries below describe the versions tested at the time. Historical C++ exception tests belong to the former facade and are superseded by the current Result migration record; they do not describe the current API.

[简体中文](VALIDATION.zh-CN.md) · [README](../README.md)

This records development validation through 2026-10-07. A design, implemented adapter, successful compile and successful target run are different states. Local tests do not certify every operating-system release, filesystem, CPU, libc or network provider.

The JSON entry below records the current examples. Earlier independent text examples, initial shared workflows, IPC/KCP implementation and other batches describe their historical versions.

## 2026-10-07: Windows, Linux, Android and ARM acceptance

Baseline `2d03031`; the changes in this update complete the agreed local-environment acceptance. The scope is Windows, WSL Linux, Android API 34 emulator execution with API 21/28 builds, and ARMv7/AArch64 GNU/Linux under QEMU. Physical devices are recorded separately. Apple, older Windows, other libc families and remote CI are outside this round. The workflow now has only `workflow_dispatch`; no remote run was started.

Full builds below enable the five optional parsers and source-built GNU libiconv, shared OpenSSL, libarchive with zlib/XZ/Zstd, and the separate 7-Zip 26.04 backend unless noted. C99/C++11 public headers and the optional C++20 tests compile. First-party C++ remains exception-free. The Android minimum is still API 21 for both ARM ABIs; an API 28 x86_64 build separately exercises process spawning.

| Configuration | Actual result |
| --- | --- |
| Windows MSVC x64 Debug, shared core | Full CTest 179/179 |
| Windows MSVC Win32 Debug, static core; 7-Zip adapter off | Full CTest 176/176 |
| WSL Linux x64 Release, shared core | Full CTest 181/181 |
| Installed shared consumers on Windows/WSL | C++11 51/51 and pure C 36/36 on each host |
| Installed Win32 static consumers | C++11 50/50 and pure C 35/35 |
| Old `7cf52fc` headers/tests with current shared library | 10/10 on Windows and WSL; 1,078 exported HX functions matched |
| WSL ASan/UBSan | Dependency/stream/crypto/TLS/config/archive subset 10/10; final archive, stream, error-text and process subset 6/6 |
| FetchContent Windows build, cJSON/iconv/KCP/uthash, crypto/archive off | Full CTest 172/172; missing/wrong Git pin rejection checked |
| Android x86_64, minimum API 21, shared core | API 34 native ISA: 159 native CTest tests/examples passed, 3 explicitly skipped, plus portable benchmark passed |
| Android arm64-v8a, minimum API 21, shared core | API 34 ARM translation: same 159 passed / 3 skipped, plus portable benchmark passed |
| Android x86_64, minimum API 28, shared core | API 34 native ISA: 160 passed / 2 skipped, plus portable benchmark passed; process contracts and capture included |
| Android x86_64, minimum API 21, minimal shared core | API 34 native ISA: 108 native tests/examples plus portable benchmark passed |
| Android armeabi-v7a, minimum API 21, full shared core | Core, public headers, all test/example binaries and target dependencies compiled; emulator rejects this ABI explicitly, so no Android ARMv7 run is claimed |
| Linux AArch64, shared core, QEMU user-mode | 161 passed, 2 random-injection tests skipped; process suite separately excluded for the empty-environment loader limitation below |
| Linux ARMv7 hard-float, shared core, QEMU user-mode | 160 passed, 2 random-injection tests skipped; process and sysinfo suites separately excluded with native probes below |
| Android deployment script regressions | Four host tests: native-test discovery, missing binaries/runtimes, SDK-under-source deployment, distinct SONAMEs and actual feature macros |

The Android emulator is `Qt_Android_34`, Android 14/API 34, native x86_64 with ARM64 native-bridge translation. It advertises no ARMv7 ABI. API 21 is a build minimum, not proof of running on Android 5.0. No APK sandbox, real ARM board or hardware-performance pass is inferred. The portable benchmark was executed under shared host load; its timings are observations, not performance acceptance thresholds.

The three API 21 skips are pathname Unix sockets denied by this emulator's SELinux policy, app configuration-directory lookup without an Android application context, and the process-capture example below its API 28 minimum. API 28 keeps the first two. IPC and C++ platform suites record the Unix-socket subcase skip while continuing their other transports/features. No security policy was relaxed. Cross configurations omit host-only Python interoperability drivers; desktop runs cover those separately.

Repairs established by target execution:

- Android OpenSSL uses first-party `shlib_variant` configuration, distinct `libssl-hx.so` / `libcrypto-hx.so` SONAMEs, relative RUNPATH and the selected runtime manifest. Deployment resolves aliases and omits linker names that would shadow the native bridge's system BoringSSL. Both crypto and TLS are present; successful linking alone is no longer treated as deployment evidence.
- Cross dependency builds forward the real NDK API/ABI, compiler and sysroot. libarchive uses the selected target SDK explicitly; its build fails if zlib, XZ, Zstd or OpenSSL support is absent. Actual ZIP/password ZIP/7z/TAR/Gzip/XZ/Zstd round trips run on Android, including encrypted 7z through the optional native backend.
- C99 parser integration supplies a private static-assert fallback for Bionic without editing TOML vendor source. Stream-size checks compile correctly on 32-bit targets. Android 23+ GNU `strerror_r` uses its pointer return type; API 21 retains the POSIX branch.
- Linux/Android archive ancestor checks use search-only directory descriptors where available, retaining `O_NOFOLLOW`. Packing beneath an execute-only ancestor now passes its regression instead of demanding directory-list permission. Desktop `/tmp` assumptions were removed from Android fixtures.
- POSIX examples validate main's argv instead of mistaking a native-bridge launcher in procfs for application arguments. The OS argument snapshot API still reports the OS view. POSIX spawn success is documented as child creation: permitted pre-exec failure via exit 127 is tested without inventing an errno or treating it as application readiness.
- GNU/Linux cross builds using `HX_OS_SYSROOT=/` preserve absolute build RPATH. Native 7-Zip cross builds use target compilers; Android uses Bionic's libc thread symbols and includes its C++ runtime privately.
- A Windows parallel run exposed a test-fixture collision: two archive backend processes could receive the same coarse clock value. Scratch names now include the process ID. The full suite and five parallel repetitions of both archive tests passed afterward; no library synchronization change was needed.

QEMU limits were reproduced outside hx_c_platform by `build-platform-probe/native.c`. The ARMv7 `sysinfo` syscall reports about 3.7 GB total while the same process reads about 20.9 GB from host `/proc/meminfo`; the library does not replace one metric with another to hide that mismatch. Process tests now accept both POSIX error-reporting forms, but their deliberately empty-environment child lacks `QEMU_LD_PREFIX` and cannot locate the target dynamic loader. Ordinary child/capture examples pass with that emulator prefix. These exclusions are not library passes and remain native-target checks. The random-injection LD_PRELOAD tests report their own skip rather than running a host interposer against a guest process.

Source management is now one provider: 13 pinned Git submodules by default, fixed-commit FetchContent as an alternative, and the official hash-checked GNU libiconv release archive because its Git tree lacks generated conversion tables. Old KCP/uthash snapshots and old caches are unchanged and inactive unless explicitly selected. Wrong-pin and missing-codec negative checks fail as intended; vendor worktrees remain clean. See [dependency instructions](DEPENDENCIES.md) and [Android commands](ANDROID.md).

Evidence stays in ignored build directories: `build-platform-*-ctest.log`, `build-platform-*-accepted.log`, install/ABI/export logs, `build-platform-probe/*.log`, and Android `build-platform-android-{x64,arm64,armv7,api28,minimal}/report*.json` with per-program `run-*.log`. Reports contain source revision/dirty state, NDK, build API/ABI, actual feature flags, device fingerprint, native/translated execution, deployed runtimes and explicit skips. For QEMU, pass `QEMU_LD_PREFIX=/usr/<triplet>` when running CTest; apply only the recorded exclusions, not a broad platform skip. Tests remove only their known uploaded files, preserving any generated fixture output for diagnosis.

## 2026-10-07: File/directory archives and optional encrypted 7z

Baseline `c2f216c`. Added C99 pack, unpack, single-file extraction and metadata visits, bounded options/progress/cancellation, and `hx::Archive` with explicit `Result` values. Default libarchive supports ZIP/AES-256 ZIP, TAR/TAR.GZ/TAR.XZ/TAR.Zstd, ordinary 7z and single-file Gzip/XZ/Zstd. Encrypted/header-encrypted 7z uses the separately optional exception-disabled C++11 adapter and pinned native 7-Zip 26.04. The adapter checks native module version/interface before constructing COM objects; a local 25.01 DLL was rejected with NOT_SUPPORTED. No native vendor source was patched.

| Configuration | Executed result |
| --- | --- |
| Windows MSVC x64 Debug, shared core/shared OpenSSL | Full CTest 179/179; final archive/loader subset 5/5 |
| WSL Linux x64 Release, shared core/shared OpenSSL | Full CTest 181/181; final archive/loader subset 5/5 |
| Installed C++11 and pure C consumers, both hosts | 51/51 and 36/36 |
| Baseline headers with current shared library, both hosts | 10/10 each, baseline `c2f216c` |
| WSL ASan/UBSan with leak detection | Archive + file/crypto/TLS regression 7/7; final archive/loader subset 5/5 |
| WSL TSan | Final archive/loader subset 5/5; native-7z concurrency regression passed five consecutive runs with two in-process operations and codec parallelism 2 |
| MSVC x86 Debug, static core/shared OpenSSL | Archive/C++/loader 3/3; installed C++11/pure C 50/50 and 35/35; all isolated C99/C++11 headers compiled |
| WSL shared core/static OpenSSL, CRYPTO disabled | Real native archive/C++ archive 2/2; ARCHIVE independently brings its OpenSSL dependency |
| Trimmed build, ARCHIVE and CRYPTO disabled | Unsupported archive/loader behavior 3/3; isolated C99/C++11 headers compiled |
| Native 7-Zip 26.04 host builds | Windows/Linux upstream make and the separate `tools/sevenzip` CMake helper passed |
| Export checks | All 1,078 declared HX functions exported on Windows/Linux |

Native and independent-process tests cover nested mixed-language/emoji names, long Windows paths, empty files/directories, raw empty streams, concatenated Gzip, Python-generated ZIP/TAR/Gzip/XZ, 7z detection without an extension, wrong passwords, invalid/escaping paths, existing links, CRC/trailer corruption, no-overwrite, selected extraction, byte/depth/count limits and cancellation. Allocation injection covers 100 default-backend and 180 native-bridge packing allocation points with zero remaining parent-context allocations. Sanitizer diagnostics in expected-failure child processes are explicitly rejected rather than accepted as ordinary command failure. Vendor libraries were not all built with compiler instrumentation; the checks do not establish an absence of every native defect.

Integration findings and repairs:

- Windows 7z output needed the libarchive wide-name interface; POSIX libarchive needed a per-thread UTF-8 locale. Native 7-Zip's Unix COM strings store UTF-16 units in `wchar_t`, including surrogate pairs. Chinese and supplementary-plane names/passwords now round-trip.
- The pinned libarchive Gzip reader does not validate trailer CRC/length. Our streaming zlib/liblzma/Zstd outer decoders validate completion and enforce decoded-stream limits, including after logical TAR EOF. Damaged trailers fail without publishing the current file; previously completed files remain by contract.
- ASan found leaked compressor storage when a failed pack entered libarchive's fatal state before finalization. Writer failure now retains the original error, drains necessary native finalization through a discard sink, and blocks publication. The final count-limit case also exposed 140 bytes of 7z directory metadata retained when format close was skipped. Failed opened writers now run close after marking entry data fatal, so native ownership lists are joined without padding the unfinished entry. The failing allocation/capacity cases pass leak detection.
- Concurrent per-call locale destruction triggered TSan reports in libc conversion-cache allocation/free. Overlapping operations now share an immutable locale with reference-counted lifetime under short native mutex sections; the final caller retires it. No codec work or callback holds that lock. The original concurrent scenario passes without suppressions.
- TSan also reported native 7-Zip global-cache destruction without a visible ordering edge from another operation's completed worker. The optional backend now uses a private ordered unload path, establishing synchronization between this library's module-reference releases. Codec work and application callbacks remain parallel; diagnostics and injected allocators run outside that lock. The concurrency regression passed five consecutive runs without suppressions.
- MSVC Debug compression dependencies require the matching CRT. The dependency builder now supports `HX_DEPS_CONFIGURATION=Debug`; Debug and Release SDKs use separate prefixes. One initial concurrent Windows full build had bare CL/link exit-code failures; the subsequent four-job full build succeeded without changing warning policy.

The installed 7-Zip 25.01 command-line tool independently accepted the produced ASCII-password AES ZIP and Chinese/emoji-password encrypted 7z. Windows 7-Zip's ZIP password decoder uses CP_ACP, so a UTF-8 non-ASCII ZIP password does not interoperate with it on this host; the API keeps UTF-8 and the guide records this limitation. This command-line tool is used only as a foreign test peer, never as a library fallback.

Measured public size/alignment (bytes): options 136/8, result 56/8, entry 48/8 on MSVC/GCC x64; 96/8, 56/8, 40/8 on MSVC x86. The private stack job is 1,056/8 on x64 and 904/8 on x86. Streaming data uses 64 KiB chunks; extraction adds a separate plaintext buffer, and outer decompression adds another output buffer. Native codec dictionaries/interface allocations and 7z temporary spools are outside the first-party memory budget. Progress accounting uses short per-job mutex sections; POSIX default-backend entry/exit adds two locale-lifetime lock sections. Optional 7-Zip adds short unload locks for its two module references; ordinary library close does not acquire that lock. Other existing hot paths gain no archive locks, allocations or dispatch. These are cost/layout observations, not a throughput improvement or hx_c_log performance result.

Logs are `archive-*` under shared, Win32, sanitizer and static-SDK builds; trimming uses `build-archive-off-linux`, and native-runtime helpers use `build-sevenzip-helper-*`. Paths and publication behavior are covered on the two hosts. macOS, Windows 7/8.1, Android, ARMv7/AArch64, musl and physical boards remain pending for these native dependency modules. Shared file helpers used by TLS/crypto were regression-tested. The requested OBS extension list now has implementations and examples through crypto/TLS/archive; target-specific validation remains distinct.

## 2026-10-07: Standard TLS and independent send/receive threads

Baseline `7b43315`. Added optional C99 TLS 1.2/1.3 endpoints, a borrowed exception-free C++11 facade, bounded queues/peers, explicit application cancellation and retryable graceful/abort shutdown. Each endpoint owns one I/O thread; servers share it across peers. OpenSSL runs on that thread over bounded BIO pairs and the existing native loop. This adds no locks or dispatch to ordinary non-TLS I/O. Public legacy layouts remain unchanged, and vendor source is unchanged.

| Configuration | Result |
| --- | --- |
| Windows MSVC x64 shared, full CTest | 175/175; final TLS/interop/C++/JSON subset 4/4 |
| WSL GCC x64 shared, full CTest | 177/177; final subset 4/4 |
| Windows x86 static core + shared OpenSSL | TLS/C++ 2/2; standalone C99/C++11 headers compiled |
| Installed x64 shared, both hosts | C++11 consumer 49/49; pure-C consumer 35/35 |
| WSL ASan/UBSan, leak detection enabled | TLS/C++/two-process JSON 3/3 |
| WSL TSan | TLS/C++ 2/2, including concurrent allocation-failure cleanup |
| WSL shared core embedding static OpenSSL | Native TLS/C++ 2/2 with NETWORK/LOOP enabled and THREAD_POOL disabled |
| Minimal build without crypto/network/loop | Unsupported TLS/C++ entries 2/2; isolated headers compiled |

The native C suite covers two concurrent 1 MiB directions, mutual certificates, wrong hostname, system trust rejecting the private test CA, partial data followed by abrupt EOF, cancellation and reuse, 48 allocator failure points, encrypted PEM passwords and peer-close timeout/retry without ID reuse. Independent Python clients/servers exercise TLS 1.2/1.3, three-byte fragments, multiple JSON frames, an expired certificate, DNS address fallback, backpressure/partial send counts and close retry. Python 3.14 strict verification found missing key identifiers in the original test certificate generator; the generator now adds SKI/AKI instead of weakening verification. C++ tests require exceptions disabled. Native OpenSSL is not fully sanitizer-instrumented, so these results do not certify every vendor-internal path.

Both SSL and Crypto runtime chains are now installed and hash-checked by the existing install verifier. Core static/shared packaging retains the explicit OpenSSL static option. UTF-8 certificate paths are tested through an independent Python ABI caller. Library-owned TLS workers finish native thread cleanup before join; external raw-crypto consumers still follow the documented static/unload contract.

Measured size/alignment bytes: TLS options 72/8 and peer-info 160/8 on MSVC/GCC x64; 44/4 and 160/8 on MSVC x86. Private endpoint/peer are 280/8 and 696/8 on x64, 192/8 and 616/8 on x86. Queues, four record buffers, BIO storage, native allocations and socket buffers are separate. The endpoint has no per-connection worker; queue copies, mutex sections and native request allocations remain real costs. No comparative throughput claim is made.

Logs: `tls-full-*`, `tls-delivery-*`, `tls-install.log`, `tls-native-*` in the corresponding ignored build directories. Runtime target coverage is Windows host/WSL; macOS, Windows 7/8.1, Android API 21 and embedded ARM/musl TLS SDK integration remain pending. The archive and optional encrypted-7z APIs are the remaining dependency-backed extension work.

## 2026-10-07: OpenSSL cryptography and deployment

Baseline `49b439a`. Added default-enabled CRYPTO with isolated OpenSSL contexts, SHA-256/SHA-512, HMAC and constant-time tag verification, random 32-byte keys, authenticated binary/text/file encryption, Ed25519/RSA-PSS signatures and bounded PEM key import/export. C++11 returns `Result` with exceptions disabled. All old public layouts and valid calls remain intact. OpenSSL is shared by default; static linking is explicit. Vendor source is unchanged.

| Configuration | Result |
| --- | --- |
| Windows MSVC x64 shared, full CTest | 171/171 |
| WSL GCC x64 shared, full CTest | 173/173 |
| Installed x64 shared, C++11 / pure C, both hosts | 47/47 and 34/34 |
| Windows x86 static core + shared OpenSSL, related suites | 4/4; standalone C99/C++11 headers compiled |
| Installed x86 static core, C++11 / pure C | 46/46 and 33/33 |
| WSL ASan/UBSan with leak detection | 5/5 affected suites, plus crypto file tests on native `/tmp` |
| WSL TSan | Concurrent native crypto suite passed |
| WSL shared core embedding static OpenSSL | 3/3 crypto suites |
| Minimal shared build, CRYPTO disabled | 2/2 unsupported-entry suites; standalone headers compiled |
| Old `49b439a` headers, new shared library | Core and file utility consumers passed 2/2 on each host |
| Complete C/C++ documentation programs | Compiled separately and printed `Hello` on both hosts |

The dynamic SDKs were built for Windows x64/x86 and WSL. ASan/TSan instrument first-party code; the prepared native OpenSSL SDK is not fully sanitizer-instrumented. Native Windows static-OpenSSL thread cleanup, macOS, Win7/8.1, Android, musl and embedded targets still need their own crypto validation. Android tooling explicitly records CRYPTO disabled when no target SDK is supplied; its earlier platform evidence is not crypto acceptance.

Tests cover SHA/HMAC known answers, RFC 8032 Ed25519, native AES-GCM interoperability, empty/binary/UTF-8/NUL inputs, every envelope byte being altered, wrong keys/passwords/signatures, capacity limits, file publication, first-party allocation failure, allocator reentry and four threads sharing immutable engines/keys. Dynamic builds exercise ordinary native thread-exit cleanup; the explicit cleanup API is used for the Windows static configuration when selected. Existing OpenSSL error entries and host default properties are checked. Error marks are not promised to survive OpenSSL's public save/restore API.

WSL's Windows mount rejects no-replace rename and can ignore POSIX 0600. Completed regular temporary files now have a hard-link publication fallback at the staging boundary; general file-move semantics are unchanged. Decryption checks private creation permissions and rejects incapable filesystems before writing plaintext. The mounted-filesystem test checks this refusal; separate `/tmp` runs exercise actual decryption, preserved destinations on failed authentication, and cleanup.

Relocation testing found and fixed a missing versioned OpenSSL runtime: installation now follows the complete symlink chain, and `verify_install.py` checks the installed runtime against its selected SDK. Relocated Windows and Linux programs work with only the copied core/Crypto runtimes and system libraries. Linux `ldd` confirms the relocated Crypto path with `LD_LIBRARY_PATH` unset. The dependency builder retains ENGINE ABI entry points after a host libssl/CTest compatibility probe exposed their absence in a trimmed build; that probe passed after rebuilding. Optional engine modules remain disabled. CTest itself is not forced through an application-specific `LD_LIBRARY_PATH`.

Measured public layout (size/alignment bytes): secret 24/8 and options 40/8 on MSVC/GCC x64; secret 12/4 and options 32/8 on MSVC x86. These are layout observations, not throughput gains. Existing non-crypto hot paths gain no locks, allocation or system calls; atomic file saving adds one internal writer callback per complete save. Crypto operations allocate native OpenSSL objects; file encryption uses 64 KiB of first-party working buffers plus metadata. Native allocations are outside the hx allocator budget. No comparative crypto throughput or hx_c_log performance result is claimed.

Logs use `crypto-delivery-*`; SDK rebuild logs are `build-deps-shared-*/abi-compatible-build.log`. Old-header and documentation/relocation fixtures are under ignored `build-crypto-abi` and `build-crypto-docs`. TLS and archive delivery remain separate batches. TLS concurrency is confirmed: one connection must support receiving on one application thread while another sends.

## 2026-10-07: Mutable signals and growable procedure results

Baseline `f998a6e`. Added explicit mutable subscriptions/emission using existing bounded parameter owners, general named observers, bytes/u64 views, and growable named procedures. Signals preserve successful edits and continue eligible callbacks after errors, returning the first error. Procedures publish an independent cloned result only on success. Existing signatures/layouts and unified registration order remain intact. A borrowed pack rejects destruction until dispatch returns; nested borrows unwind on failure. No vendor changes.

Windows MSVC x64 shared full CTest passed 167/167; WSL GCC shared passed 169/169. The subsequently added nested-borrow fixture passed again on both hosts; only test coverage changed after full runs. Installed x64 shared C++11/pure C consumers passed 45/45 and 33/33 on both hosts. Installed Win32 static consumers passed 44/44 and 32/32 without the optional generator test. Win32 static related tests passed 6/6, with C99/C++11 standalone headers compiled. WSL ASan/UBSan and TSan each passed the six affected suites, then reran the nested fixture and executable example 2/2; the ASan rerun explicitly enabled leak detection. Four minimal static configurations each passed 6/6: SIGNALS+PARAMETERS, PROCEDURES+PARAMETERS, SIGNALS without PARAMETERS, and both dispatch modules disabled. The final minimal configuration disables optional modules. Four old-header/source consumers from `f998a6e` passed against the new DLL and so. C++20 facade syntax passed with exceptions disabled and Qt-style emit/signals/slots macros defined.

Tests verify successive modifications around multiple errors and an intervening owned snapshot, native first-error details, aliased signal-name lifetime, destruction rejection, fixed projection limits/type rejection, unchanged legacy read-only emission, bounded recursive dispatch, four simultaneous emitters with independent packs, timeout/close retry, exactly-once cleanup, cross-kind procedure rejection, unique names, publication only on success and allocation failures through cloning/callback mutation. First-party allocation counters return to zero; pointer-pointee side effects are explicitly outside rollback. The C example prints the clamped volume from both observer and caller.

Private callback slots remain 168/8 on MSVC x64 and 160/8 on Win32; handler sizes remain 88/8 and 56/8. The stack-only opaque argument view grows to 16/8 and 8/4. The callback kind fits beside the state without increasing slot stride. Legacy subscriptions add no allocation or lock; nonempty signals retain their bounded snapshot allocation. Mutable setters allocate only when growth requires it. Legacy projection uses bounded stack calldata, and growable procedure calls explicitly clone owned values. Procedure binding storage still contains the same three pointer-sized members.

Nine alternating WSL GCC Release static runs of 200,000 named notifications used identical `benchmarks/signals.c` and SIGNALS+PARAMETERS configurations. Baseline/current median was 215.52/221.81 ns per emit, ranges 208.47..237.51 / 209.93..247.37 (about +2.92% median elapsed). The overlapping, unpinned measurements do not prove nonregression or a speedup; no Windows/embedded throughput result is claimed. Samples: `build-mutable-perf-after-linux/samples.json`.

Logs use `mutable-*`, with minimal matrix logs in `build-signal-minimal-linux`. Native macOS, Win7/8.1, Android and embedded execution remains pending. OpenSSL/TLS and archive backends remain separate work; the mutable-signal failure-policy decision is now implemented.

## 2026-10-07: Observe all signals on one handler

Baseline `d043756`. Added a read-only all-signal subscription and opaque borrowed argument view, with optional explicit copying into an owned parameter pack. It reuses bounded slots, registration snapshots and callback retirement. Existing C layouts and named-callback signatures remain unchanged. C++11 adds `connectAll`, `SignalArguments` and a Qt-macro-compatible `notify` spelling; its legacy `emit` declaration is shielded locally with the caller's macro restored. No third-party source changed.

Windows MSVC x64 shared full CTest passed 164/164; WSL GCC shared passed 166/166. Installed C++11/pure C consumers passed 43/43 and 32/32 on both hosts. Installed Win32 static consumers passed 42/42 and 31/31 without the optional format-generator case. Win32 signal, all-signal, procedure, C++ extensions and macro tests passed 5/5. Standalone C99/C++11 headers compiled; C++20 syntax also passed with an empty `emit` macro and exceptions disabled. WSL ASan/UBSan with leak detection and TSan each passed the six affected suites/example. Signals-only without PARAMETERS, procedures-only without SIGNALS, and both-disabled static configurations passed five related checks each. The minimal profile now includes the newly introduced optional modules. Old signal/procedure headers and tests from `d043756` ran successfully against the new WSL shared library.

Coverage includes mixed registration order, later names, literal asterisk, callback-time registration, quota failure, first native error retention, all existing argument types including embedded NUL, output preservation, allocator failure through complete cloning, copied values after source/handler destruction, and four concurrent emitting threads. Disconnect and close time out while callbacks are active, preserve ownership, and succeed after release; self-wait is rejected, and cleanup runs exactly once. Context allocation counters return to zero. Macro tests simulate Qt's `emit` definition; they are not a Qt application runtime test.

Complete size/alignment: private slot 168/8 on MSVC x64 and 160/8 on Win32, unchanged; handler 88/8 and 56/8; new argument view 8/8 and 4/4. C registration and borrowed getters add no allocation. The existing nonempty emit snapshot allocation remains; the new branch selects a named or all-signal callback, without an extra lock, system call or indirect-call layer. C++ capture registration and explicit cloning allocate by request.

A WSL GCC Release static comparison used the same `benchmarks/signals.c` against archived `d043756` and current source with SIGNALS alone enabled. Nine alternating runs of 200,000 notifications to one named callback gave baseline/current medians 243.02/236.02 ns per emit, ranges 225.85..263.96 / 228.58..246.93. The median difference was -2.88%, with overlapping ranges and no CPU affinity; this does not establish a speedup or universal nonregression. Raw samples are `build-signal-perf-after-linux/samples.json`. There is no new Windows or embedded throughput claim.

Logs use `signal-all-*`; isolated minimal logs are in `build-signal-minimal-linux`, ending with optional modules disabled. Native macOS, Win7/8.1, Android and embedded runtime validation remains pending. Mutable argument dispatch is waiting on the agreed failure-policy decision; crypto/TLS/archive integration is separate remaining work.

## 2026-10-07: Optional Linux desktop sleep requests

Baseline `c591e74`. Added the Linux-only LINUX_DESKTOP adapter behind the existing scoped sleep API, with optional installed libsystemd/libelogind runtime binding. It requests freedesktop PowerManagement or GNOME suspend inhibition through a private connection, without service autostart or interactive authorization. A shared one-second monotonic budget bounds method waiting; native setup has no hard deadline. Deactivation closes the private connection without a release-reply wait. Performance requests stay unsupported on Linux. Public layouts and C++11 call shapes are unchanged; no third-party source/SDK headers are copied or modified.

Windows MSVC x64 shared full CTest passed 161/161; WSL GCC shared passed 163/163. Installed C++11/pure C consumers passed 41/41 and 31/31 on both hosts; installed Win32 static consumers passed 40/40 and 30/30 without the optional format-generator case. Win32 power/C++ extension checks passed 2/2, and standalone C99/C++11 headers compiled. WSL ASan/UBSan with leak detection and TSan each passed power, isolated desktop protocol and C++ extensions 3/3. With every other optional module disabled, Linux static power/C++/isolated-service checks passed 3/3; disabling LINUX_DESKTOP or SLEEP_INHIBIT separately passed unsupported checks 2/2 each. The minimal tree ends with SLEEP_INHIBIT disabled.

The isolated fixture exercises C and C++ against all three service names, identical cookie values on independent connections, copied Unicode reasons, allocation failures, idempotence, deactivate/reactivate and cross-thread cleanup. It verifies denial, malformed replies, timeout, bus disconnection without terminating the host, nonlocal address rejection, and runtimes lacking required symbols. All mock leases retire when clients disconnect. Installed Linux pure C consumers also passed the nine service/failure scenarios. The runnable example passed on Windows and against the isolated freedesktop service. The fixture uses its own dbus-daemon and temporary working directories, leaving real desktop policy unchanged. A C++ test originally assumed compiled capability meant a service must be present; it now uses the controlled service for the positive path instead.

The WSL runtime was libsystemd0 259.5-0ubuntu3.4 and dbus-daemon 1.16.2-2ubuntu4. The adapter adds one small first-party native state and one private bus connection per active object, without background work, a library-global lock or a shared mutable bus. Native SDK allocations are outside context accounting. RTLD_NODELETE intentionally retains provider mappings for native TLS lifetime; object references/messages/connections are released. No throughput improvement, physical sleep-prevention result or zero native-runtime RSS claim is made.

Logs use `linux-power-*`; independent minimal logs live in `build-linux-desktop-minimal`. Actual GNOME/KDE desktop power policy, real libelogind, older Linux runtimes, musl, ARM/Android targets and Windows 7/8.1/macOS regression runs remain pending. Windows/macOS backend code was not changed. Signal extensions and public crypto/TLS/archive APIs remain separate work.

## 2026-10-07: Borrowed native object interoperability

Baseline `367c314`. Added independently selectable NATIVE_INTEROP queries for file/socket/process values and representation-preserving HANDLE/pthread_t copies. Values remain borrowed, without duplication, adoption or a retained owner reference. Callers exclude competing operations through native use. Attached/pending sockets return BUSY; joined threads and recorded/reaped child exits return CLOSED. The C++11 facade returns borrowed values through Result and exposes the correct native thread type. No existing public object layout changed.

Windows MSVC x64 shared full CTest passed 161/161; WSL GCC shared passed 162/162. Installed C++11/pure C consumers passed 40/40 and 30/30 on both hosts; installed Win32 static consumers passed 39/39 and 29/29 without the optional format-generator case. Win32 static native/C++ utility suites passed 2/2 and public C99/C++11 headers compiled independently. WSL ASan/UBSan with leak detection and TSan each passed 2/2. Native MSVC ASan with networking/process modules disabled passed 2/2 with interop enabled and 2/2 disabled; Windows socket/process behavior is covered by ordinary native shared/Win32 runs, not this minimal ASan configuration. WSL builds with networking/process disabled also passed interop enabled/disabled checks 2/2 each. C++20 header syntax passed with exceptions disabled.

Tests inspect/write through the native file then read through the library, compare live native thread identity, preserve short-buffer/extra storage, reject completed joins, inspect socket type, reject pending and idle attached sockets, and query a controlled child's native object before normal library waiting/reclamation. A thousand file-handle queries left the injected allocation counter unchanged; context allocations returned to zero. Queries add only field/atomic reads and output copies, with no new lock, allocation, worker or system call. No throughput improvement or lifetime extension is claimed. The native file example is compiled and run by the full suites.

Logs use `native-*` under the relevant build trees. The minimal Linux and MSVC ASan configurations finish with NATIVE_INTEROP disabled. macOS, Win7/8.1, Android and embedded target validation remains pending; pthread_t is copied as its actual native type, not assumed to be an integer. Remaining signal, Linux desktop inhibition, crypto/TLS and archive work is not marked complete here.

## 2026-10-07: Windows resources and desktop identifiers

Baseline `219f18a`. Added separately selectable WINDOWS_VERSION and WINDOWS_DESKTOP adapters, UTF-8 version strings, capacity-checked version/window/monitor/GPU records and an exception-free C++11 facade. Version resources are read without executing the target binary; HWND/HMONITOR remain borrowed. A title request for another thread in the same process returns WOULD_BLOCK before sending a message. Text buffers grow within a fixed limit; DXGI queries release all interfaces before return. No existing public layout or hot path changed.

Windows MSVC x64 shared full CTest passed 159/159; WSL GCC shared passed 160/160. Installed C++11/pure C consumers passed 39/39 and 29/29 on both hosts. Win32 static Windows utilities/C++ utilities passed 2/2, with standalone C99/C++11 header compilation. Installed Win32 static C++11/pure C consumers passed 38/38 and 28/28 (without the optional format-generator case). C++20 header syntax passed with exceptions disabled. Native MSVC x64 AddressSanitizer passed these two suites with all other optional modules disabled. The version-only, desktop-only and both-disabled configurations each also passed 2/2 under MSVC ASan. Its initial linker diagnostic required /DEBUG; after adding that configuration flag the instrumented tests passed. MSVC ASan does not establish LeakSanitizer coverage. WSL ASan/UBSan with leak detection passed 2/2; it checks the portable facade/unsupported paths, not Windows native APIs.

Owned fixtures verify numeric file/product versions, translated resource strings, a copied executable with a Chinese path, missing/invalid inputs, small/extra output capacity, all allocator-failure points through success and complete context reclamation. A hidden window verifies Chinese/supplementary-plane titles, class and process path, cross-thread rejection, empty and 255/1024/32767-unit titles, explicit failure at 32768 units, and destroyed-handle failure. Real monitor/GPU queries returned hardware and software adapter descriptions/LUIDs on this desktop; duplicate display names were retained as distinct native adapters. No rendered-frame capture or throughput speedup is claimed.

Complete record size/alignment measurements: version 36/4 and monitor 172/4 on MSVC x64/x86; window 48/8 on x64, 40/4 on x86; GPU 576/8 on both. Queries do not allocate a persistent object, start a worker or add a library-global lock. Native API allocations and graphics-driver behavior are outside the injected context accounting. Numeric records use temporary stack storage; version resource allocation is capped at 16 MiB; text starts at 256 UTF-16 units and grows to at most 32769 including terminator/probe capacity.

Logs use `windows-tools-*` under shared and Win32 trees, and the dedicated `build-windows-tools-asan` configuration logs. That sanitizer tree finishes with both adapters disabled. Native Windows 7/8.1, MinGW and other desktop/session configurations remain pending. Non-Windows results validate explicit unsupported behavior. Signal/native-handle extensions, Linux desktop inhibition, crypto/TLS and archive APIs remain separate work.

## 2026-10-07: Bounded runtime preprocessing

Baseline `2b71115`. Added independent PREPROCESSOR selection, object macros, signed-integer conditions, bounded source inclusion, owned output and per-call diagnostics. Text calls have no filesystem access unless given a resolver; file calls supply a relative resolver. Limits cover source bytes, output, retained macro storage, expansion work, directives and all nesting. Stacks grow on demand. No global macro table, background worker, compiler invocation or vendor edit was introduced.

Windows MSVC x64 shared full CTest passed 158/158; WSL GCC shared passed 159/159. Installed C++11/pure C consumers passed 38/38 and 28/28 on both hosts. Win32 static preprocess/C++ utilities passed 2/2. Three WSL static configurations passed 2/2 each: missing lexer/parameters, text-only without file utilities, and PREPROCESSOR disabled. ASan/UBSan with leak detection passed preprocess, C++ utilities and the executable example 3/3; TSan passed preprocess 1/1, including independent concurrent invocations. Standalone C99/C++11 headers compiled with the normal suite.

Tests exercise UTF-8 and BOM handling, token-boundary preservation, signed extrema and comparison precedence, defined operands across expansion frames, macro/source cycles, inactive branches, invalid directives/operators, virtual and real relative files, byte/depth/work limits, all allocation failure points through a successful call, native resolver error retention and unchanged output on failure. Four threads use independent definitions; 125 comparison combinations are checked against C expressions. Template data files and a complete executable show ordinary calls. Built-in inclusion follows links and is explicitly not a security sandbox; callbacks and file I/O have no hard deadline.

Logs use `preprocess-*` in shared, Win32, ASan, TSan and minimal build directories. MacOS, Win7/8.1, Android and embedded target runs remain pending. Signal extensions, native-platform, crypto/TLS and archive adapters remain separate work.

## 2026-10-07: Runtime parsing conveniences

Baseline `2acdb22`. Added exact-token accept/expect, expected-kind checks, quoted-string extraction, bounded nested-bracket skipping and flat token seeking. Existing lexer grammar, options, ownership and public layouts remain unchanged. The C++11 facade adds Result-based operations and an explicit borrowed native handle. These helpers process runtime input and do not add custom C/C++ syntax or build steps. Runtime macro/conditional/include preprocessing is still separate pending work.

Windows MSVC x64 shared full CTest passed 156/156; WSL GCC shared passed 157/157. Installed C++11/pure C consumers passed 37/37 and 27/27 on both hosts. Win32 static lexer/parser/C++ utilities passed 3/3; WSL all-optional-modules-off static passed their disabled checks 3/3. ASan/UBSan with leak detection passed lexer, parser, C++ utilities and the executable script example 4/4. Standalone C99/C++11 public headers compiled, and the C++20 header syntax was checked with exceptions disabled. The final explicit-this lambda capture is equivalent to the tested capture.

Tests cover non-consuming mismatches, EOF versus expected END, decoded Unicode and escapes, output/cursor preservation on allocation failure, mixed bracket types, brackets inside quoted strings/comments, default 64-level and explicit 80-level nesting, invalid depth, token-count limits, missing targets, kept whitespace/comments and inert C++ objects. Helper execution adds no allocation except owned string decoding, no lock and no OS call. Group skipping uses a fixed 256-byte stack. The target text length for flat seeking is computed once, avoiding repeated scans of a long search token. No throughput improvement or new target-board run is claimed.

Logs use `parser-*` in the shared, Win32, ASan and minimal build trees. macOS, Win7/8.1, Android and embedded runtime checks for these additions remain pending. Signal extensions, preprocessing, native-platform, crypto/TLS and archive adapters are not marked complete by this entry.

## 2026-10-06: Growable named parameter packs

Baseline `8e82749`. Added the independent PARAMETERS module, bounded named scalar/string/byte values, deep clones, borrowed views and first-error edit cursors. C++11 provides move-only `Parameters` ownership and borrowed const/mutable facades, without exceptions. Existing stack calldata layout and behavior are unchanged. Name validation is shared with the existing signal/procedure path; no third-party source was changed.

Windows MSVC x64 shared full CTest passed 154/154; WSL GCC shared passed 155/155. Both hosts' installed C++11 and pure C consumers passed 36/36 and 26/26. Win32 static params/C++ utility suites passed 2/2. WSL ASan/UBSan with leak detection passed params, calldata, signals, procedures, C++ utilities and the executable example 6/6. With every other optional module disabled, a static build passed params/C++ utilities 2/2; disabling PARAMETERS as well passed their unsupported-path checks 2/2. Standalone C99/C++11 public headers compiled successfully.

Coverage includes 150 shuffled insertions with sorted enumeration, 64-bit unsigned extrema, copied binary bytes with invalid UTF-8, embedded NUL text, self-aliased replacement, wrong-type/missing-key output preservation, field/storage limits, retained capacity on shortening, first-error edits, deep-copy lifetime, allocation failure at payload/array/clone steps, allocator reentry and complete context reclamation. One thousand warmed scalar/string updates and reads preserved the allocator call count. There is no new internal lock, thread or variant/container dependency. Lookup is binary search; insertion/removal may move entries. No throughput comparison or concurrent mutation support is claimed.

Public size/alignment measurements are: options 24/8 on MSVC/WSL x64 and 16/4 on MSVC x86; tagged value 24/8 and 16/8; edit cursor 32/8 on both widths. These are full object sizes/array strides, not a runtime speed claim. Payload capacities and geometric entry arrays have separately documented limits and bounded temporary duplication.

Logs use `params-*` under the shared, Win32, ASan and dedicated minimal build trees. The minimal tree's final configuration has PARAMETERS disabled; its enabled-run record is retained separately. New macOS/Win7/Android/embedded-board runs remain pending. Dynamic signal dispatch integration and the remaining preprocessing/platform/crypto/archive work are not marked complete here.

## 2026-10-06: Structured profiler snapshots and bounded percentiles

Baseline `7f3ad7d`. Added immutable independently owned snapshots, exclusive min/max, last samples, Welford mean/population deviation and opt-in inclusive/exclusive histograms. Existing options/handles and TSV v1 remain compatible. Each histogram has 252 fixed counters (2,016 bytes per path/channel); default recording has no histogram and warmed paths still allocate no context memory. Captures allocate outside the lock; consistent data/bin copies happen under it, name copies and square roots afterward. Snapshot budget includes allocation overhead, and disabled builds return not supported.

Windows MSVC x64 shared full CTest passed 152/152; WSL GCC shared passed 153/153. After the final equivalent copy-expression cleanup, both hosts reran the snapshot suite/example and installed consumers: C++11 35/35, pure C 25/25. Win32 static passed profiler/snapshot/C++ extensions, with the uint64 boundary fixture rerun after it was added. WSL ASan/UBSan with leak detection passed five related suites; TSan passed profiler and concurrent snapshots 2/2; PROFILER=OFF passed three related suites. C99 and C++11 standalone public-header targets passed. C++ tests require exceptions disabled. WSL also compiled the old profiler test and headers from `7f3ad7d` and ran that consumer against the new shared library.

The tests independently reconstruct mean/deviation from measured last samples and check nearest-rank intervals against a sorted sample array. They cover P0–P100, rational near-one quantiles, empty/aborted scopes, nested IDs, retained names after profiler/context closure, custom allocator failure and busy contexts, allocation reentry, per-snapshot limits, unchanged legacy TSV budgets, concurrent recording/capture, optional channel combinations and zero/UINT64_MAX bucket edges. A quiescent private fixture injects otherwise impractical elapsed values for the boundary test.

| Object | MSVC x64 / WSL x64 size, alignment | MSVC x86 size, alignment |
| --- | --- | --- |
| Public snapshot entry / array stride | 144 / 8 bytes | 136 / 8 bytes |
| Public quantile | 24 / 8 bytes | 24 / 8 bytes |
| Private path record, before optional name/bins | 160 / 8 bytes | 144 / 8 bytes |
| Private profiler | 672 / 8 bytes | 344 / 4 bytes |

WSL GCC Release shared builds with only the profiler optional module enabled were compared against baseline `7f3ad7d` with identical options. Nine alternating runs each timed 100,000 warmed single-thread enter/leave pairs; setup, capture and output are outside the timed loop. The machine was not CPU-pinned; figures are local microbenchmarks, not a guarantee for ARM, contention, full-feature configurations or production work.

| Recording mode | Median ns/interval | Observed range ns |
| --- | --- | --- |
| Baseline | 125.20 | 123.43–132.72 |
| New default statistics | 131.55 | 125.01–143.56 |
| Inclusive histogram | 132.43 | 125.33–143.14 |
| Both histograms | 133.27 | 128.53–147.11 |

The default median increased by about 5.1%; both histograms by about 6.4% versus baseline. The new basic statistics add floating-point work and 64 bytes per private path record, without a new lock or system call in recording. Capturing a snapshot adds a clock observation and copies enabled bins, so its cost grows with recorded paths. No native Windows before/after timing or cross-platform throughput claim is made. Reproduce the current modes with `HX_OS_BUILD_BENCHMARKS=ON` and `hx_os_profiler_benchmark 0`, `1` or `3`.

Logs use `profiler-*` in the shared, Win32, sanitizer and minimal build trees. The isolated baseline/current performance builds and `results.json` are ignored artifacts. No new macOS, Win7/8.1, Android, ARM board or musl run is claimed. Remaining parameter/signal, preprocessing, platform, crypto/TLS and archive work is not represented as complete by this record.

## 2026-10-06: Local calendar fields and explicit breakpoints

Baseline `6d0d8a1`. Existing UTC defaults remain intact. Added local display/filename entries, per-instant UTC offset in seconds, fixed-English weekday/month names, 12-hour time, ordinal days, milliseconds and Unix-second fields. Legacy long tokens take precedence. Local conversion uses native runtime calendar fields, with documented Windows/time_t/timezone-data limits; no global TZ/locale changes. `%Z` is a numeric UTC-offset label. Fixed-width fields retain manual digit rendering instead of introducing CRT number formatting into that path.

Windows MSVC x64 shared full CTest passed 150/150; WSL GCC shared passed 151/151. Both hosts' installed consumers passed C++11 34/34 and pure C 24/24. Win32 passed seven related suites; WSL minimal passed calendar, calendar_dst, debug and cpp_utilities 4/4. WSL ASan/UBSan passed ten related suites with leak detection. Added C++ filename-now wrappers were rebuilt and tested separately after the full run. Standalone public-header targets and 16 changed source/script files passed their checks; owned changes are UTF-8 without BOM and LF.

Tests include UTC/east/west/quarter-hour offsets, 2024 US DST gaps/overlaps, pre-epoch flooring, retained old UTC behavior, a POSIX seconds-resolution offset and the independent 1900–2101 day progression. The POSIX breakpoint test installs/restores its own SIGTRAP handler and confirms resumption. Windows DebugBreak is compiled/linked but deliberately not executed by automated tests. No throughput benchmark or new macOS/Win7/Android/board run is claimed.

Logs use `calendar-full-*`, `calendar-install.log` and target-specific `calendar-*` files in the ignored build directories. This batch does not complete the remaining signal/parameter-pack, preprocessing, profiler, native-platform, crypto/TLS or archive adapters.

## 2026-10-06: OBS-style I/O, configuration conveniences and dependency layout

Baseline `a2d1799`, local `dev`. Added snapshot/idle waits without closing a pool, UTF-8 stdio paths, native wide-text conversion, ASCII case-insensitive string operations, bulk bounded byte queues, binary streams/endian I/O and bounded background file writers. Configuration additions provide section/key INI editing, explicit backup save/recovery and per-key settings overlays. C++11 facades use Result and compile without exceptions. Existing public layouts and exports are retained.

| Configuration | Evidence |
| --- | --- |
| Windows MSVC x64 Release shared | Full build and CTest 147/147 |
| WSL Linux GCC x86_64 Release shared | Full build and CTest 146/146; Windows-only fixture accounts for the count difference |
| Installed shared C++11 / pure C consumers | Each host: 33/33 and 23/23, including new C exports and the C++ stream facade |
| Windows MSVC Win32 static | Related text, queue, stream, writer, pool wait and C++ tests passed; later INI, backup and settings tests passed 4/4 with cpp_platform |
| WSL ASan + UBSan static | Related stream/writer/queue and configuration/C++ suites passed; leak detection enabled, no reported errors |
| WSL TSan static | pool_wait and cpp_pool passed 2/2; buffered_file passed separately after stop/retry lifetime checks, with no reported races |
| Minimal WSL build | New stream/writer/queue/configuration/settings stubs and cpp_platform passed; C99/C++11 standalone header targets built |
| Public headers and source rules | Independent C99/C++11 header targets built on Windows/WSL; 66 changed owned source/script files passed the 80-column check with Tab width 8; UTF-8 without BOM/LF and whitespace checks passed |

Tests exercise partial I/O, EOF/truncation, unchanged numeric outputs, native errors, allocation failures, ring wraparound, two producer threads, latched write/flush failures, suffix-only retry, timed-out flush/close and explicit stop/abandonment. Pool tests gate completion/cleanup deterministically, distinguish snapshot and idle waits and reject same-worker waits. INI tests preserve retained old trees, reject unsupported values/collisions and inject every construction allocation failure until success. Backup recovery validates and preserves exact bytes; ordinary load does not restore. Settings overlay preserves prior commits on failure and exercises observer reentry and a one-operation limit.

Measured private object sizes/alignment with MSVC, in bytes (generated probes copy the current private definitions):

| Object | x64 size / alignment | x86 size / alignment |
| --- | --- | --- |
| Binary stream | 64 / 8 | 36 / 4 |
| File writer, excluding queue/worker/native objects | 216 / 8 | 176 / 8 |
| Pool task, with delayed tasks enabled | 112 / 8 (previous 88) | 80 / 8 (previous 56) |
| Pool object | 144 / 8 (previous 104) | 88 / 8 (previous 64) |

The stream uses mutually exclusive backend storage. Snapshot pool waits require acceptance-order links/counters and a separate completion condition; completion takes an additional pool lock. The writer test observed no context allocation in write/flush after creation. These are cost/layout observations, not measured throughput or latency improvements; no new assembly was introduced.

Third-party sources now reside in `deps/`. Seven relocated KCP/uthash/minicoro files were byte-compared with HEAD and are unchanged; minicoro remains unused. OpenSSL 3.5.9, libarchive 3.8.9, zlib 1.3.2, XZ 5.8.4 and zstd 1.5.7 are fixed Git submodules. Their static Release dependency builds succeeded on Windows/WSL. Windows used OpenSSL ASM=OFF. An independent linked probe round-tripped ZIP, AES-256 password ZIP, ordinary 7z, TAR, TAR.GZ, TAR.XZ and TAR.Zstd, and rejected a wrong ZIP password on both hosts. Windows static dependency detection/linking was corrected in first-party CMake, including WIN7 selection; no upstream edits. FetchContent downloaded and checked the zlib commit, using existing source overrides for the other four dependencies. The dependency builder needs CMake 3.20; the core remains 3.16.

Logs: `obs-current-full-build.log`, `obs-current-full-ctest.log`, `obs-install.log` in the two shared build trees; sanitizer and target-specific logs remain in their respective ignored build directories. The installed consumers do not require the OpenSSL/archive stack yet: public crypto/TLS/archive APIs and the optional encrypted-7z backend are still pending. The other requested OBS extension modules not listed above also remain in progress. No new macOS, Windows 7/8.1, Android, ARM board or musl run is claimed.

## 2026-10-05: Error text, IPC reentry and coroutine boundaries

Baseline `13b10ec`, local `dev`. Windows native error text now checks UTF-8 conversion and discards failed or incomplete text while preserving the original numeric error. IPC event allocation and unused-storage release run outside the endpoint mutex, with the existing in-flight caller retaining its lifetime. Receive rechecks queues and closure; allocation failure retains events, and concurrent closure takes precedence. Connection waits preserve native error details. Public declarations, layouts and exported APIs remain unchanged.

| Configuration | Evidence from this run |
| --- | --- |
| Windows MSVC x64 Release shared | Related CTest 10/10: core, error_text, error_text_windows, ipc, ipc_allocator, coroutine, coroutine_sync, format, cpp_coroutine and cpp_format |
| WSL Linux GCC x86_64 Release shared | The corresponding nine tests passed 9/9; the Windows-only native-text fixture is excluded |
| WSL static ASan + UBSan | error_text, ipc_allocator, ipc_wait_error, coroutine, coroutine_sync and format passed 6/6; leak detection enabled, no reported memory errors or leaks |
| WSL static TSan | The same six tests passed 6/6; no reported races |
| Public headers | Existing independent C99/C++11 header targets built on Windows and WSL |
| Source checks | Ten changed C/header/CMake files passed the 80-column checker with Tab width 8; changed files use UTF-8 without BOM and LF; whitespace checks passed |

The Windows fixture compiles the real error formatter with controlled `FormatMessageW` text and the real `WideCharToMultiByte`. It covers valid text, a 685-unit message requiring 2052 UTF-8 bytes, invalid UTF-16, and native-message failure across Win32/Winsock/HRESULT/GAI domains. Failed text falls back to the unchanged status/domain/code; it does not emit a truncated UTF-8 prefix. This is a controlled failure test, not a crash reproduced from an installed long Windows system message.

The IPC allocator fixture uses real loopback connections and checks allocator/deallocator statistics reentry, allocation-failure retry, competing receivers, closure during allocation including allocation failure, close-timeout retention/retry, overflow-event retention and event order. Every fixture releases its context and verifies zero retained allocations. A data-message receive succeeds with allocation failure armed, confirming no new allocation on that receive path. The static wait-error fixture compiles the real IPC code with a controlled native wait failure and checks status, domain, native code and cause code.

The coroutine synchronization test now covers a caller-owned native thread whose condition deadline expires while another host owns its mutex. Runtime/synchronization close leaves it waiting; unlocking lets it return `TIMEOUT` with the mutex reacquired, then all resources are reclaimed. Documentation clarifies abandoned-lock consequences and bare stackless `WOULD_BLOCK` requeue behavior. Scheduling and lock ownership are preserved. The formatting helper's validated-fill/output-limit invariant is documented without adding hot-path checks.

CI actions are pinned to full SHAs resolved from the official checkout v6, setup-python v6 and upload-artifact v4 tags. Updating them requires verifying a new upstream commit and changing the pin and version comment together. No hosted CI run is claimed by this local verification.

Logs are `review-fixes-build-final.log` and `review-fixes-ctest-final.log` under `build-shared-msvc/`, `build-shared-linux/`, `build-parsers-asan/` and `build-tsan-linux/`; shared header builds use `review-fixes-headers.log`. This task ran related suites, not full CTest or install/old-consumer compatibility checks. No new macOS, Win7/8.1, Android, ARM board or throughput/latency benchmark was run. Third-party sources were not edited.

## 2026-10-04: Logical paragraphs in source and examples

Baseline `4c68e88`, local `dev`. Reviewed the owned C/C++ sources, CMake files and Markdown source examples. Distinct preparation, execution/result checks, cleanup and return steps now have one separating blank line; related declarations, checks and data tables remain grouped. Target properties are easier to scan on separate lines. `.clang-format` preserves separation between definitions, and the local `cpp-qt-project-rules` skill records the same rules for future work.

| Configuration | Evidence from this run |
| --- | --- |
| Windows MSVC x64 Release shared | Full build and CTest: 134/134 passed |
| WSL Linux GCC x86_64 Release shared | Full build and CTest: 134/134 passed |
| Installed Windows / WSL shared consumers | Each host: C++11-enabled 26/26, pure C 16/16 passed |
| Source/layout checks | Source nonempty lines, literals and comments preserved; all 14 CMake token streams and embedded arguments preserved; Markdown prose/data preserved apart from these records, style guidance and refreshed test counts |
| Engineering rules | 435 source/script files passed the 80-column checker; the Objective-C adapter was checked separately; changed files are UTF-8 without BOM and LF |
| Third-party and skill scope | Tracked third-party/license bytes unchanged; only the requested skill entrypoint and its two formatting references changed; skill validation passed |

Logs and read-only comparison reports are in ignored `build-formatting/`. This is a readability change, with no API, field-order, lifetime or performance redesign. Source line numbers and diagnostics naturally move. No new sanitizer, Android, macOS, Win7/8.1, ARM board or performance run was performed in this task.

## 2026-10-04: Single-declaration codec macros

Baseline a48b51c, local dev. The standalone hx_codec_macros.h header adds HX_CODEC_STRUCT to declare a normal C99/C++11 value structure, fields and schema from one member list. Scalars, strings, nested objects, dynamic/fixed arrays, NODE, renaming, optional values and XML attributes are supported, with 1–32 fields. Existing codec implementation, public layouts and exported functions remain intact. The C++ facade includes the macro header. No source generator, target runtime dependency, runtime registration or background activity was added.

| Configuration | Evidence from this run |
| --- | --- |
| Windows MSVC x64 Release shared | 11/11 related tests, including C/C++ with both traditional and /Zc:preprocessor modes |
| WSL Linux GCC x86_64 Release shared | 9/9 codec, C/C++ example and format interoperability tests |
| Headers and strict language modes | Standalone C99/C++11 headers; GCC/Clang strict C99 and no-exception C++11 syntax checks |
| WSL ASan + UBSan with leak detection | codec, cpp_codec and macro example, 3/3; no reported memory errors/leaks |
| ARMv7 Linux GNU cross-toolchain | Static library and C/C++ targets built; qemu-arm -L /usr/arm-linux-gnueabihf passed 3/3 |
| AArch64 Linux GNU cross-toolchain | Static library and C/C++ targets built; qemu-aarch64 -L /usr/aarch64-linux-gnu passed 3/3 |
| Installed Windows / WSL consumers | Each: pure C 16/16 and C++11-enabled project 26/26; final installed headers rechecked with 2/2 relevant C/C++ tests |
| Documentation | Both complete C tutorial programs extracted and compiled; execution produced {"host":"localhost","port":8080} and port = 8080 |

Existing codec tests now check generated/manual layout and offsets, one/32-field boundaries and final-field preservation, scalar widths, arrays, nesting, heterogeneous NODE values, composed modifiers and C++ namespaces. GCC C99/C++11 compile probes reject empty lists, trailing commas and 33-field declarations. Expansion stages handle MSVC's traditional preprocessor; only potentially unused readonly metadata is annotated for Clang, without disabling consumer warnings globally.

Build directories contain codec-macros-build.log, codec-macros-ctest.log, codec-macros-install.log and codec-macros-consumer-final.log. Validation was scoped to the changed declaration/consumption paths; no new full desktop, Android, macOS, Win7/8.1 or TSan run. ARM evidence uses glibc cross-toolchains and QEMU, not physical boards, musl, arbitrary SDKs, drivers or system performance.

The macros retain sizeof/offsetof for evaluation by the actual target compiler and preserve declaration order. This layer declares static readonly descriptions, with no runtime reflection or new allocation path; no throughput or memory optimization claim. See [serialization](SERIALIZATION.md) for usage and embedded boundaries.


## 2026-10-04: Nested documents and schema serialization

Baseline `450e42f`, local `dev`. YAML/XML/TOML/ENV output completes the seven format writers. New APIs construct documents, return immutable replacements, convert formats and explicitly parse string subdocuments. The independently trimmable CODEC maps declared C/C++ value fields, nested objects, dynamic/fixed arrays and heterogeneous NODE subtrees. Existing public layouts remain intact; C++11 exceptions remain disabled. No new third-party dependency or vendor modification.

| Configuration | Result in this run |
| --- | --- |
| Windows MSVC x64 Release shared | Full CTest **131/131**, including independent C99/C++11 public header builds |
| WSL Linux GCC x86_64 Release shared | Full CTest **133/133**, including independent header builds |
| WSL static ASan + UBSan | 13/13 relevant configuration/codec/example tests; 3/3 repeated after the final descriptor count guard; leak detection enabled, no reported memory errors/leaks |
| WSL static TSan | 6/6 related tests, no reported races |
| Windows MSVC Win32 | config_tree, codec and cpp_codec, 3/3 |
| Installed shared consumption, Windows / WSL | Each: pure C 16/16 and the C++11-enabled consumer project 26/26; find_package uses installed new headers/symbols |
| Old 450e42f headers/tests with new shared library | 10/10 on each host; compatibility runner now copies old test helper headers and honors their no-exception requirement |
| Public exports | Windows DLL and Linux so each match **844** declared functions |
| Trimming | WSL CODEC=OFF, and CONFIG=OFF with CODEC=ON, each 3/3; missing dependencies make codec explicitly unavailable |
| Android API 21 | NDK r28c full arm64-v8a, armeabi-v7a and x86_64 builds; API 34 emulator: native x86_64 and translated ARM64 each 3/3 plus a portable benchmark execution |
| Documentation programs | Complete tutorial C program and README C++11 fragment extracted, compiled and run with GCC/G++; expected output observed |

New cases cover mixed integer/real/bool/string/null arrays, interleaved objects/arrays, empty collections, 32/64/128 levels and limit rejection, immutable source lifetime, dynamic subtrees, large/narrow integers, fixed-array lengths, XML attribute escaping, input-buffer independence, configured bounds and successive injected decode-allocation failures. Shared-library interoperability exercises 101 seeded JSON trees with independent Python JSON/XML/TOML readers; WSL additionally uses installed PyYAML. Windows lacks PyYAML and explicitly skips that optional reader, while builtin/libyaml cross-checks still pass.

A reproduced pre-existing properties writer bug was repaired: an initial U+FEFF key was read back as a BOM and lost its prefix. Properties now escapes it; INI rejects that unrepresentable initial key. YAML output escapes Unicode separators, C1 controls and BMP noncharacters instead of exposing literal characters that independent readers may reinterpret or reject.

Logs are in the corresponding build directories: serialization-build.log, serialization-ctest.log, serialization-install-cpp.log and serialization-abi.log. Android also has build-android/report.json, build-android-emulator/report.json, serialization-run.log and per-program logs. Emulator follow-up used 8de8da1 with a temporary read-only/no-snapshot instance, closed afterward. Source rules, UTF-8 without BOM/LF and git diff --check were checked.

Encoding allocates a bounded document tree and retains two-pass output; decoding allocates owned values, arrays and text. No new background threads, mutable global registry or implicit locks. This is not a zero-allocation/zero-copy claim or a throughput benchmark. Runtime validation for macOS, Win7/8.1, embedded Linux boards, musl, Android API 21 OS, ARMv7, physical ARM and APK sandboxes remains pending. See [serialization](SERIALIZATION.md) for format subsets, XML mapping and C++ layout constraints.


## 2026-10-04: JSON in all four independent workflows

Starting from `feac68e` on local `dev`, native IPC devices, TCP orders, UDP samples and KCP blocks now use UTF-8 JSON for requests, replies and notifications. Business programs, transports and runners stay separate; only codec helpers in `business_json.h` are shared. Existing settings serialization and built-in JSON parsing are reused. Library implementation, public ABI and third-party files are unchanged.

| Configuration | Evidence |
| --- | --- |
| Windows MSVC x64 Release shared | Eight C99 business programs built; 8/8 business tests passed |
| WSL Linux GCC x86_64 Release shared | Same eight programs; 8/8 passed |
| WSL ASan + UBSan, `detect_leaks=1` | 8/8; no memory error, undefined behavior or leak reported |
| WSL TSan, `halt_on_error=1` | 8/8; no data race reported |
| WSL Release, `HX_OS_ENABLE_IPC=OFF` | Six TCP/UDP/KCP programs built; 7/7 network business tests passed |

The previous seven cases still exercise device state, concurrent communication, split/coalesced TCP frames, unusual UDP sequence numbers and first-datagram KCP loss. All now compare parsed JSON fields independent of key order/whitespace. The C TCP client and independent Python socket peer verify Chinese, quotes, backslashes and supplementary-plane characters round-trip in order notes. Framing counts UTF-8 bytes. Runner output explicitly uses UTF-8 to avoid Windows code-page failures.

The added `tcp_business_json_validation` runs seven independent connections with string quantity, boolean quantity, real quantity, missing quantity, malformed JSON, duplicate key and array root. Each is rejected while the preceding two valid orders still total 11500 cents. Explicit node-kind checks prevent the configuration getters' numeric-string conversions from accepting wrong business types. Example limits are 1023 payload bytes, parsing depth 8, 64 nodes and 64 KiB parsing memory.

JSON examples require default-enabled `CONFIG`, `CONFIG_SERIALIZE` and `SETTINGS`; CMake registers them when these dependencies are satisfied. Documentation covers releasing parsed documents and temporary strings and keeping borrowed nodes/slices within document lifetime. Codecs allocate documents and temporary objects; no throughput or zero-allocation claim is made.

Logs: `build-shared-msvc/json-business-tests.log`, `build-shared-linux/json-business-tests.log`, `build-sanitize-linux/json-business-tests.log`, `build-tsan-linux/json-business-tests.log`, and `build-transport-no-ipc/json-business-tests.log`. Full/install matrices were not rerun this turn. Android, macOS, Win7/8.1 and embedded execution of these new examples remains pending.

## 2026-10-04: Separate IPC, TCP, UDP and KCP workflows

This records the `feac68e` text-message version, superseded by the JSON payloads above.

Starting from `f89b566` on local `dev`, native IPC device programs now accept only named pipes/Unix sockets. TCP has new order programs, UDP samples use raw datagrams, and KCP has independent block-transfer programs. TCP/UDP use `hx_os_socket_*`; KCP uses `hx_os_kcp_*` with UDP. These network workflows never call `hx_ipc_*` or the IPC device business code. Runners, CTest names and bilingual tutorials are separate. Library implementation, public ABI and third-party sources are unchanged.

| Configuration | Evidence |
| --- | --- |
| Windows MSVC x64 Release shared | Eight C99 programs built; 7/7 business cases passed |
| WSL Linux GCC x86_64 Release shared | Same eight programs; 7/7 passed |
| WSL ASan + UBSan, `detect_leaks=1` | 7/7; no memory error, undefined behavior or leak reported |
| WSL TSan, `halt_on_error=1` | 7/7; no data race reported |
| WSL Release, `HX_OS_ENABLE_IPC=OFF` | Six network programs built; TCP/UDP/KCP 6/6 passed, no native IPC case registered |

Cases are `ipc_business_local`, `tcp_business_orders`, `tcp_business_framing`, `udp_business_samples`, `udp_business_gaps`, `kcp_business_transfer` and `kcp_business_loss`. TCP checks two orders totaling 11500 cents, a rejected third order and two shipping notifications. A standard-library Python peer splits the first header/body across writes and combines subsequent requests to exercise TCP framing. UDP verifies latest state and feedback for two devices, including controlled gaps/duplicates/late samples. KCP discards the first outgoing datagram on each side yet delivers three applied-block replies and completion; the server applies only three blocks totaling 60.

Network examples share only setup/diagnostics/cleanup in `network_demo.h` and process/log handling in `business_runner.py`. Protocols, business I/O and assertions stay in their own files. Nonblocking raw socket calls are serialized as required by the existing contract; TCP/UDP retry sleeps occur outside mutexes. One KCP I/O thread drives updates while a producer enqueues bounded messages under short exclusion.

Independence was tested by configuring `build-transport-no-ipc` with `-DHX_OS_ENABLE_IPC=OFF` and running it, not inferred from renamed files. Logs: `build-shared-msvc/transport-business-tests.log`, `build-shared-linux/transport-business-tests.log`, `build-sanitize-linux/transport-business-tests.log`, `build-tsan-linux/transport-business-tests.log`, and `build-transport-no-ipc/transport-business-tests.log`.

This turn ran affected business tests, not the full suite or install-consumer matrix. Android, macOS, Win7/8.1, physical ARM and embedded execution of these new examples remains pending. Their single-session limits are documented separately and do not inherit HXIP session-replacement guarantees. No performance gain or exactly-once guarantee across restarts is claimed.

## 2026-10-04: Threaded communication workflows

This is the historical `f89b566` record. Its shared business entry points are superseded by the independent workflows above.

Starting from `0689517` on local `dev`, this change adds four C99 application programs, a Python standard-library runner and a [bilingual business tutorial](IPC_BUSINESS.md). Library implementation, public interfaces and third-party sources are unchanged.

| Configuration | Evidence |
| --- | --- |
| Windows MSVC x64 Release shared | Four business programs compiled; `ipc_business_*` 5/5 passed |
| WSL Linux GCC x86_64 Release shared | Same four programs compiled; same five workflows 5/5 |
| WSL ASan + UBSan with leak detection | Five workflows 5/5; no memory/undefined-behavior error or leak reported |
| WSL TSan | Five workflows 5/5; no data race reported |

Local IPC, TCP and KCP launch separate device-service and controller processes. Two client request threads operate devices 101/102 while an independent receiver handles unsolicited notifications. Checks cover open, initial state, changed-state readback, rejected parameters preserving the prior value, notification matching, device close and service shutdown. The library routes request replies to their callers; the business receiver obtains independent messages.

Ordinary UDP publishes two devices' samples while a separate thread receives feedback; the dashboard keeps the latest state by sequence. `ipc_business_udp_gaps` deliberately omits 3/7, duplicates 4 and resends 6 after 8. It checks eight accepted updates per device, newest sequence 10, two gaps, two stale/duplicate samples and final temperatures. This is a controlled application-input demonstration, not a measurement of network loss or a reliable-UDP promise.

The runner checks payloads, final business state and successful exits of both processes. Failures print diagnostics and clean up only its own child processes. Manual runs expose device operations, separate receives and UDP summaries. Programs use C99 and existing thread/IPC entry points; C++11 continues to use the existing exception-free facade.

Validation is scoped to the example/CMake changes. This turn did not rerun the full suite, installation matrix or Android emulator. The 2026-10-03 full-suite and target records remain below; running the new examples on macOS, Android, Win7/8.1, physical ARM devices and embedded boards is pending. These results provide neither a performance benchmark nor proof that leaks are impossible.

Logs are under ignored build directories: `build-shared-msvc/ipc-business-tests.log`, `build-shared-linux/ipc-business-tests.log`, `build-sanitize-linux/ipc-business-tests.log`, and `build-tsan-linux/ipc-business-tests.log`. Reproduce with `ctest --test-dir <build-directory> -C Release -R "^ipc_business_" --output-on-failure`; all default modules yield five cases.

## 2026-10-03: Local IPC, KCP and runnable examples

Source baseline `ec51a1e`, developed on local `dev`. This adds C99 IPC entry points, an exception-free C++11 facade and a private KCP v1 adapter. Windows named pipes and POSIX Unix domain sockets share message/session logic; TCP, ordinary UDP and KCP retain distinct delivery contracts. See the [IPC tutorial](IPC.md) for direct calls and the language-independent wire protocol.

| Configuration | Evidence |
| --- | --- |
| Windows MSVC x64 Release shared | Full CTest 113/113; final IPC/KCP/separate-process/TLS-lifetime subset 4/4 |
| WSL Linux GCC x86_64 Release shared | Full CTest 115/115; final related subset 4/4 |
| Standalone C99/C++11 public headers | Compiled on Windows x64/x86, WSL and Android ARM64/ARMv7; C++ exceptions disabled |
| Installed shared consumers on Windows and WSL | Each host: C/C++ 23/23 and pure C 14/14, including IPC/KCP; KCP license installed |
| Windows MSVC x86 Debug static | IPC/KCP 2/2; installed C/C++ 22/22 and pure C 13/13 |
| WSL ASan + UBSan with leak detection | IPC, KCP and separate-process examples 3/3 |
| WSL TSan | Same three suites 3/3, no race reported |
| IPC/KCP disabled | Build and two unsupported-behavior suites pass |
| Minimal profile with only KCP enabled | C99 headers, KCP engine and unsupported IPC pass without NETWORK/LOOP |
| IPC enabled; KCP, PIPE and NET_OPTIONS disabled | Linux build and 2/2 suites; TCP/UDP/Unix remain usable |
| Android NDK r28c, minimum API 21, ARM64 | Full build; 28 suites and portable benchmark on API 34 x86_64 emulator through ARM translation |
| Android NDK r28c, minimum API 21, ARMv7 | Full cross-build; selected emulator has no matching ABI, no run |
| Shared exports | Windows/Linux both match 833 public functions, with no extra HX private exports |
| Old headers with current shared library | `7cf52fc` headers/consumers pass 10/10 on both Windows and WSL |

Android TCP/UDP/KCP subcases pass. The selected emulator's SELinux `sock_file create` policy rejects shell pathname Unix socket creation with EACCES. That subcase is explicitly SKIP, retained in report `test_notes`; security policy and device privileges were not changed. The emulator instance created for this run was stopped. This is not evidence of Android 5.0 hardware, an APK sandbox, ARMv7 execution, macOS, Win7/8.1 or embedded-board execution.

A parallel Windows full run, while the host was building and running an emulator, encountered one existing `coroutine` test's five-second wait failure. Its isolated rerun passed, followed by a complete serial 113/113 run. Neither the test deadline nor coroutine implementation was altered to hide the initial failure. This does not prove coroutine scheduling under every load; the IPC tutorial explicitly uses ordinary threads and makes no coroutine-suspension claim for synchronous IPC calls.

IPC tests cover Chinese/supplementary Unicode, empty and 40 KB binary messages, simultaneous requests in both directions, four shared callers with replies sent in reverse order, multiple-peer isolation, listener restart/new sessions, stale-peer rejection, deadlines/late replies, inbox capacity and recovery, immediate close followed by retry, waking active callers, 32 creation-allocation failure positions and zero retained context allocations. The process driver launches real C servers and C/C++ clients. Its independent Python peer checks split headers, coalesced frames, oversized/truncated bodies and delayed old UDP HELLO/data with a reused business request ID.

KCP synthetic links deliberately lose, duplicate and reorder traffic while delivering 100 messages in each direction across the 32-bit clock rollover. Other checks cover 64 KiB messages, malformed packets, queue bounds and continued operation with further context allocation forbidden after creation. All three upstream SHA256 values match the pin; vendor sources remain unchanged. Windows pipe tests exposed/fixed duplicate-handle IOCP reassociation, separate-process KCP exposed/fixed unsent protocol acknowledgements during immediate client completion, and Linux restart tests drove listener address reuse handling.

Android previously lacked finite native join. The new Bionic adapter polls public `pthread_gettid_np` for a kernel-cleared ID before reclaiming a library-owned, unjoined thread. The existing `lifecycle` test confirms timeout/retention while a TLS destructor blocks, then successful retry after releasing it. It does not substitute a flag at the end of the user function for native thread termination.

### Memory, layout and performance limits

Each endpoint has one I/O worker and separate bounds for peers, business queues, pending calls, control queues and KCP storage. TCP/local endpoints allocate neither the UDP packet buffer nor the KCP reassembly buffer. KCP reserves engine storage at creation; IPC messages and asynchronous operations still allocate. Sends and waits use an endpoint mutex; business threads do not hold it while remote business work executes. KCP adds timer wakeups/retransmission costs. These are correctness/resource-lifetime results, not claims of zero allocations, locks or copies, superiority to TCP/ZeroMQ, or a device-workload throughput/latency threshold.

Measured `hx_os_layout_report` size/alignment, in bytes; array stride equals size:

| Object | MSVC x64 | MSVC x86 | Linux x86_64 |
| --- | --- | --- | --- |
| `hx_ipc_message` | 72 / 8 | 64 / 8 | 72 / 8 |
| `hx_ipc_options` | 72 / 8 | 48 / 4 | 72 / 8 |
| `hx_ipc_statistics` | 56 / 8 | 48 / 8 | 56 / 8 |
| `hx_os_kcp_options` | 32 / 8 | 28 / 4 | 32 / 8 |
| Private IPC endpoint metadata | 504 / 8 | 344 / 8 | 528 / 8 |
| Private per-peer metadata and 4 KiB input buffer | 4440 / 8 | 4376 / 8 | 4440 / 8 |

Existing public structure layouts are preserved. The private Windows pipe adds one association pointer. Passing sanitizers, zero context counts and layout measurements are not a proof of no possible leak or improved speed. Caller-held messages, compiler TLS, native stacks and socket buffers are separate costs. Before plugin unload, retire endpoints and threads that may still execute library code/TLS cleanup.

Ignored logs: `build-shared-{msvc,linux}/ipc-*.log`, `build-win32/ipc-*.log`, `build-sanitize-linux/ipc-final-tests.log`, `build-tsan-linux/ipc-final-tests.log`. Android snapshots: `build-android/report-ipc-arm64.json` and `report-ipc-armv7.json`. The final shared-code Android baseline/KCP/IPC/lifecycle subset and benchmark also pass, recorded in `report-ipc-final-arm64.json`; per-program logs remain in their configuration directories. Changed first-party UTF-8/no-BOM/LF files, 80-column source rules and local documentation links were checked; Markdown prose is not forcibly wrapped into short lines.

## 2026-10-03: Simple go calls and brace formatting

This entry covers the managed coroutine/synchronization additions and the typed C99 formatter with optional host generation. Prior entries retain their historical measurements. Source baseline: `266112b`; local `dev` changes, with generated files and logs kept under ignored build directories.

| Configuration | Evidence |
| --- | --- |
| Windows MSVC x64 Release, shared, no-exception C++ | Full CTest 110/110; final formatter/sync/go checks 4/4; generated-source/tool checks 3/3; Debug generation 1/1 |
| WSL Linux GCC x86_64 Release, shared, no-exception C++ | Full CTest 112/112; final related tests 7/7 |
| Installed shared consumers, Windows and WSL | On each host: C/C++ 21/21 and pure C 12/12, including the installed generator |
| Windows MSVC x86 Debug, static | Four focused runtime suites and C99/C++11 isolated headers; installed C/C++ 20/20 and pure C 11/11 |
| Old headers against the new shared library | Baseline `7cf52fc`: Windows 10/10 and WSL 10/10; public export comparison 802/802 on both hosts |
| ASan + UBSan + leak detection, WSL | Seven affected suites passed; final changed formatter/sync/go subset 5/5 |
| TSan, WSL | Coroutine, coroutine I/O, synchronization and C++11/C++20 go: 5/5 |
| Module disabled | COROUTINES/FORMAT disabled build, explicit unsupported C entries and formatting checks 3/3; C++ module-off wrappers compile and skip work |
| Stackless-only, no coroutine I/O | Existing C scheduler and C++20 frame tests passed; tests requiring stackful support skip that work |
| Android NDK r28c, API 21, ARM64 | Full build; 25 suites plus the existing portable benchmark completed on the API 34 x86_64 emulator using ARM translation |
| Android NDK r28c, API 21, ARMv7 | Full cross build, including public headers; no ARMv7 runtime available on the selected emulator |

The emulator ran read-only with snapshots disabled and was stopped after testing. These runs are not native ARM hardware, Android 5.0 OS, APK sandbox or actual embedded-board verification. macOS, Win7/8.1 and musl target validation remain pending. The helper uses the CMake 3.16 feature baseline; this turn exercised installed host CMake versions rather than installing/running CMake 3.16 itself. Runtime Windows-console Unicode output is implemented; automated output checks exercised redirected UTF-8 bytes.

Tests cover default managed ownership/error clearing, explicit scope nesting, native-thread channel endpoints, owning and over-aligned moved values, CLS cleanup, condition cancellation followed by mutex reacquisition, timeout/retry groups and C++20 frames. The Windows frame test exposed an alignment error: MSVC x64's `max_align_t` is 8 but its coroutine ABI may require default-new alignment. Allocation now honors `__STDCPP_DEFAULT_NEW_ALIGNMENT__`; the original and new C++20 suites pass. ARMv7 Clang also exposed a tautological size-range comparison, now guarded for the target width. Checked contexts finish with zero outstanding allocations on the exercised success/failure paths. Sanitizers do not prove correctness for every possible schedule.

Generator checks use the real host Clang and build tools: Chinese text, expressions, one-time ordered evaluation, custom structures, inherited dependency definitions, missing arguments/fields, wrong specs, header-change invalidation, unchanged-build caching, paths with spaces and original-source retention. The helper is installed and invoked through `find_package` by both pure-C and mixed consumers. Windows/MSVC and WSL/GCC builds use different configuration directories. Generation is optional, does not change original files and adds no target runtime dependency. POSIX closed-pipe output returns an error instead of terminating the test process. No output performance threshold is inferred from these correctness tests.

Measured size/alignment, bytes; array stride equals the listed size:

| Object | MSVC x64 | MSVC x86 | GCC Linux x86_64 |
| --- | --- | --- | --- |
| Coroutine metadata (private) | 320 / 8 | 224 / 8 | 320 / 8 |
| Runtime metadata (private) | 280 / 8 | 176 / 8 | 280 / 8 |
| `hx_format_arg` | 32 / 8 | 16 / 8 | 32 / 16 |
| `hx_format_options` | 24 / 8 | 16 / 4 | 24 / 8 |
| `hx_os_coro_errors` | 32 / 8 | 32 / 8 | 32 / 8 |

The prior coroutine/runtime sizes were 304/240 on x64 and 208/136 on x86. Added identity/wait state and the bounded error report account for the increases; public pre-existing C layouts remain unchanged. Managed submission releases the caller task reference and publishes errors under the existing runtime lock. Uncontended coroutine sync does not allocate a waiter; contention does. Nontrivial channel values add a budgeted box/reference count, with separate payload costs. Formatting uses transactional temporary strings; floating conversion can allocate a locale and perform multiple round-trip attempts. No claim of faster libgo/Go execution, allocation-free formatting, or hx_c_log performance is made; no assembly optimization was added.

Reproduction logs: `build-no-exceptions/current-full-{win,linux}.log`, `current-last-{win,linux}.log`, `current-focused-win.log`, `current-install-*`, `current-abi-*`, `current-asan*.log`, `current-tsan.log`, `current-win32.log`, `current-off-final.log`, `current-stackless-cpp20.log` and `current-layout-*.log`. Android snapshots: `build-android/report-format-arm64.json` and `report-format-armv7.json`, plus per-configuration build/run logs. The Android `tests` object also includes the benchmark, so it contains 26 executable results for 25 suites. Changed-source width checks pass; first-party UTF-8/LF files and documentation links were checked, with vendor sources unchanged.


## Red-black trees, algorithms and self-developed coroutines (2026-10-03)

Added red-black maps/sets, duplicate keys, traversal and sequence algorithms, together with a bounded cooperative scheduler, native stackful contexts, C99 state machines and an optional exception-free C++20 layer. Existing ordered maps remain AVL. Coroutine scheduling, channel/select and I/O integration are first-party implementations; no external coroutine source is compiled, installed or linked. The existing C99/C++11 baseline and public structure layouts remain intact.

| Configuration | Evidence for this change |
| --- | --- |
| Windows MSVC 19.40 x64 shared Release | Full CTest 100/100 |
| WSL Ubuntu GCC 15.2 x86_64 shared Release | Full CTest 102/102 |
| Windows MSVC 19.29 x86 static Debug | 5/5 affected container/coroutine suites |
| WSL ASan + UBSan, leak detection enabled | 6/6: containers, stl, profiler, coroutine, coroutine_io, cpp_coroutine20; no reported defects |
| WSL TSan | 4/4: profiler, coroutine, coroutine_io, cpp_coroutine20; no reported races |
| Public headers | C99/C++11 isolated header targets build; C++20 test/example includes its facade first, with exceptions disabled |
| Installed shared consumers, Windows x64 and WSL | Each 18/18 C/C++ and 10/10 pure C programs |
| Installed static consumers, Windows x86 | 18/18 C/C++ and 10/10 pure C programs |
| Old headers from 7cf52fc with the new shared library | Windows/WSL each 10/10 |
| Declarations against shared-library exports | Windows/WSL each match 761 exported HX functions |
| Stackless only; coroutine I/O and containers OFF | WSL coroutine/C++20 suites 2/2 |
| Coroutine core and containers OFF | WSL disabled stl/coroutine suites 2/2 |
| Pipes and containers OFF, coroutine TCP enabled | WSL coroutine_io 1/1; profiler also OFF: coroutine/coroutine_io 2/2 |
| ARMv7 hard-float Linux and AArch64 Linux, GCC static Release | Each 5/5 affected suites under QEMU user mode with the corresponding glibc sysroot |
| Android API 21 ARM64/ARMv7/x86_64 full shared | All three cross-builds pass |
| API 34 x86_64 emulator, native x86_64 and translated ARM64 | Each 20/20 selected suites plus the portable benchmark |
| Android ARMv7 execution | Explicitly skipped: selected emulator does not provide this ABI |

Container tests compare randomized insert/erase behavior against a model, validate AVL/RB invariants and duplicate-key order, and exercise iterator invalidation, bounds, allocation failure, stable/partial sort, selection, partition, transformation, splice and multiset operations. Coroutine tests cover rendezvous/buffered channels, exactly one selected branch, timeout/cancellation, bounded admission, prepare/close races, cleanup completion, C++ frame destruction, floating-point rounding state, loopback TCP, DNS, pipes and retained resources after close. An additional host ownership regression checks failed copying, disposal of unselected/full-buffer sends, deep-copy independence and final buffered-value cleanup; it runs on Windows x64/x86, WSL, ASan/UBSan and TSan.

The review corrected a prepare/close race that could publish closure before prepared-data cleanup finished, a pipe-disabled close path, dispatcher/cleanup waits that could deadlock, and profiler frames shared between interleaved coroutines. Profiler contexts now follow each coroutine and retire before task completion; tests cover both coroutine kinds, unfinished scopes and the 64-context-per-coroutine limit. Tracked failure paths finish with zero context allocations. Sanitizer success applies to exercised paths, not every possible schedule or proof of universal leak freedom.

### Layout and local performance observations

Measured full-module object size/alignment in bytes; these private objects are not new ABI promises:

| Object | MSVC x64 | MSVC x86 | GCC Linux x86_64 |
| --- | --- | --- | --- |
| Coroutine metadata | 304 / 8 | 208 / 8 | 304 / 8 |
| Worker metadata including root context | 112 / 8 | 60 / 4 | 352 / 8 |
| Native context | 48 / 8 | 28 / 4 | 288 / 8 |
| Runtime metadata | 240 / 8 | 136 / 8 | 240 / 8 |

The Windows native context contains only the fiber fields it uses. Stackless tasks have no separately reserved coroutine stack; they still consume metadata, persistent state and, for C++20, a compiler frame. The default stackful usable stack is 128 KiB; guard pages/native rounding count toward its 64 MiB aggregate reservation limit. Reservations, allocator accounting and resident physical memory are distinct measurements.

The new hx_os_coroutine_benchmark uses the same 20,000 integer keys for AVL/RB, sorted or deterministically permuted insertion, sorted lookup and deletion. These are medians of three local Release runs, in nanoseconds per operation. Windows uses QPC for this benchmark. Builds and emulator runs were stopped before sampling; CPU frequency and unrelated host activity were not pinned.

| Host / tree / insertion order | Insert | Find | Erase |
| --- | ---: | ---: | ---: |
| Windows / AVL / sorted | 171.4 | 80.0 | 122.4 |
| Windows / RB / sorted | 150.2 | 80.3 | 113.9 |
| Windows / AVL / permuted | 233.6 | 104.1 | 125.3 |
| Windows / RB / permuted | 212.4 | 99.9 | 90.3 |
| WSL / AVL / sorted | 185.0 | 61.6 | 54.8 |
| WSL / RB / sorted | 119.3 | 61.5 | 59.8 |
| WSL / AVL / permuted | 166.2 | 69.0 | 67.3 |
| WSL / RB / permuted | 135.1 | 86.6 | 65.3 |

Both trees account for the same allocation bytes after insertion on each host: 3,280,199 on Windows and 3,600,215 on WSL. This includes the container allocator wrapper, not user value allocations or process RSS. RB does not win every operation; AVL remains available.

For one worker and one task performing 100,000 yield rounds, Windows medians are 148.0 ns for the C state machine and 327.6 ns for stackful execution; WSL medians are 693.4 ns and 618.2 ns. The three-run ranges are respectively 137.3–151.1, 291.6–333.1, 691.1–729.9 and 594.5–639.6 ns. These measurements include scheduler/locking and amortized task startup/completion; they are not raw assembly-switch latency or C++20-frame timings. No Go, external coroutine, thread-pool or historical throughput comparison was performed. These microbenchmarks do not establish hx_c_log performance.

Trees introduce no internal lock or background thread. Sequence algorithms document their scratch-space costs; linked-list splice/partition only relink existing nodes. Coroutine resume/yield uses the scheduler mutex and a direct native switch or state callback; channel operations allocate bounded waiter/payload storage, and coroutine I/O uses the existing loop plus a dispatcher. Include these costs when choosing a facility. No assertion of allocation-free channels or lock-free scheduling is made.

### Reproduce and remaining targets

Run from the source root with the actual generator/compiler; add -DCMAKE_BUILD_TYPE=Release for a single-configuration generator:

```sh
cmake -S . -B build -DBUILD_SHARED_LIBS=ON -DHX_OS_BUILD_BENCHMARKS=ON
cmake --build build --config Release --parallel 4
ctest --test-dir build -C Release --output-on-failure
python tools/verify_install.py build --config Release
python tools/verify_abi.py build --config Release
```

Use separate build directories with -DHX_OS_SANITIZER=address,undefined or -DHX_OS_SANITIZER=thread. Select the subset with ctest -R '^(containers|stl|profiler|coroutine|coroutine_io|cpp_coroutine20)$'; set ASAN_OPTIONS=detect_leaks=1 for the address build. Benchmark/layout targets are hx_os_coroutine_benchmark and hx_os_layout_report, under build/bin and build/benchmarks with an additional configuration subdirectory on MSVC. Android commands are in [ANDROID.md](ANDROID.md). QEMU results are not board tests.

Local ignored evidence resides in build-no-exceptions/coro-delivery-*.log, coro-ownership-*.log, coro-final-armv7.log, coro-final-aarch64.log, coro-trim-*.log, coro-no-profiler.log, coro-install-*.log, coro-bench-*-final.log and coro-layout-*.log. Android reports are build-android/report.json and build-android-emulator/report.json. Reports identify the parent Git revision plus a dirty working tree because validation precedes the delivery commit. Build products/logs are not committed.

macOS Intel 10.13/ARM64 11.0, Win7/8.1, Android 5.0 runtime, ARMv7 Android execution, musl/uClibc-ng and physical embedded/Android boards remain pending. POSIX stack switching rejects unsupported CPU ABIs, including x32/AArch64 ILP32 and arm64e; stackless mode remains an option. Active Linux x86 shadow stacks are rejected without disabling host protection; that runtime path was not exercised here. Existing GCC maybe-uninitialized diagnostics in other modules remain; this is not a clean-warning certification. Tests disable exception handling and preserve finite-wait failures/resources. No remote CI or physical ARM performance result is claimed.

## Recursive file search (2026-10-03)

Added C99 streaming and first-match file search with exception-free C++11 convenience methods. The synchronous implementation reuses native directory adapters and the existing UTF-8 glob matcher; the background example submits one task to the existing thread pool. Existing public structure layouts are unchanged; no new dependency or implicit worker is introduced.

| Configuration | Evidence for this change |
| --- | --- |
| Windows MSVC 19.40 x64 shared Release | Full CTest 91/91 |
| WSL Ubuntu GCC 15.2 x86_64 shared Release | Full CTest 93/93 |
| Windows minimal shared, FILE_UTILS OFF | Rebuilt; 3/3 affected tests, including explicit unsupported behavior |
| WSL minimal shared, FILE_UTILS ON | 3/3 affected tests with FILE_INFO, CONTAINERS, CONFIG, SETTINGS and THREAD_POOL disabled |
| MSVC 19.29 x86 static Debug | Standalone C99/C++11 headers; 5/5 search/utility tests and examples |
| WSL ASan + UBSan, leak detection enabled | 3/3 affected tests; no reported defects |
| WSL TSan | C++ utility suite, including worker-thread search, passes with no reported races |
| Installed shared consumers, Windows x64 and WSL | Each 15/15 C/C++ and 9/9 pure C programs |
| Installed static consumers, Windows x86 | 15/15 C/C++ and 9/9 pure C programs |
| Existing old-header consumers against current shared library | Windows/WSL each 10/10 |
| Public declarations against exports | Windows/WSL each match 690 exported HX functions |
| New bilingual documentation programs | Windows/WSL each 6/6 compiled and run with documented output |
| Android API 21 ARM64/ARMv7, full/minimal shared | All four cross-builds pass, including standalone C99/C++11 headers |
| ARM64 through API 34 x86_64 emulator native bridge | Both configurations pass 14/14 suites plus the portable benchmark |
| ARMv7 runtime | Explicitly skipped: selected emulator does not support this ABI |

Controlled fixtures cover 2,052 matching files, 96 nested directories, Unicode names, first match/no match, callback stop/error details, bounds, allocation failures and complete tracked cleanup. POSIX also tests link cycles, links to outside directories and invalid-byte filenames. WSL uses native /tmp for these fixtures because the Windows-mounted filesystem can translate invalid names. C++ verifies that callbacks run inside the selected pool and that copied task arguments remain valid.

For a fixed directory shape, growing from 4 to 2,052 matches leaves search-context allocation calls and peak live bytes unchanged: Windows 23 calls/4,010 bytes; WSL 7 calls/1,866 bytes. These observations include allocations routed through that context, not libc/OS buffers or callback allocations. They do not establish whole-process memory use, a speedup, or a Windows/Linux performance comparison. The implementation uses an explicit depth-bounded stack and reusable buffers without collecting all results; complete scans still visit directory entries.

The default depth is 64, configurable up to 256. Stop/error is observed at matching callbacks; running scans do not automatically poll task cancellation and cannot promise prompt cancellation when no files match. Native directory calls have no hard deadline. Pool wait/close timeout preserves pending work; the application must keep its owner and callback state alive. Directory enumeration is not a snapshot or a security sandbox against concurrent replacement.

Bilingual snippets, commands, links, fences, UTF-8 without BOM and LF pass checks. Both new search Mermaid diagrams render successfully. Existing header builds disable C++ exceptions. macOS, Win7, Android 5.0 runtime, musl and physical embedded/Android ARM remain pending; translated emulator execution is not a physical ARM performance test.

## Variant values (2026-10-03)

The new hx_variant C99 module and exception-free hx::Variant / hx::TypedVariant C++11 facades hold native scalars, owned UTF-8 strings, borrowed pointers and custom structures. The module is independent of CONFIG/SETTINGS/CONTAINERS; the container preset uses existing copy/drop hooks. Existing C public layouts/valid calls are unchanged, exports are additive, and no third-party source/dependency is changed.

| Configuration | Evidence for this change |
| --- | --- |
| Windows MSVC 19.40 x64 shared Release | Full CTest 88/88 |
| WSL Ubuntu GCC 15.2 x86_64 shared Release | Full CTest 90/90 |
| Windows minimal shared, VARIANT OFF | Full CTest 55/55; affected tests rerun after final header checks |
| WSL minimal shared, only VARIANT re-enabled | Full CTest 60/60; CONFIG, SETTINGS and CONTAINERS remain OFF |
| MSVC 19.29 x86 static Debug | C99/C++11 headers compile, 4/4 variant tests/examples run |
| WSL ASan + UBSan with leak detection | 6/6 affected value/container/data tests/examples; no reported defects |
| Installed shared consumers, Windows x64 and WSL | Each 13/13 C/C++ and 8/8 pure C programs |
| Installed static consumers, Windows x86 and WSL | Each 13/13 C/C++ and 8/8 pure C programs |
| Old-header consumers with current shared library | Windows/WSL each 10/10 |
| Public exports vs declarations | Windows/WSL each 686 HX functions match, including variant exports |
| New bilingual documentation programs | Windows/WSL each 7/7 compiled/run with matching displayed output |
| C++ invalid alternatives / qualified types / fallible implicit copying | Three cases rejected with their intended compile-time diagnostics on WSL |
| Android API 21 ARM64/ARMv7, full and minimal shared | Four cross-builds; standalone C99/C++11 headers included |
| ARM64 on API 34 x86_64 emulator, native-bridge translation | Both profiles pass 13/13 selected suites plus portable benchmark |
| ARMv7 runtime | Explicitly skipped; the selected emulator lacks this ABI |

Failure injection covers raw object allocation, nested string copying and container insertion. Failed replacements retain the old value, failed getters retain outputs, callback native errors survive normalization, completed objects are dropped once, all tracked allocator blocks/context leases retire. Tests include aliasing nested values, move/swap across allocators, borrowed views, over-alignment, construction-only objects and C/C++ type identities across translation units. Compiler exceptions are disabled; no exception syntax or new background work is introduced.

The measured MSVC value layout is 48 bytes/alignment 8 on x64 and 24 bytes/alignment 8 on x86; the descriptor is respectively 48/24 bytes. The C++ owner adds no storage. Repeated scalar set/emplace requests zero allocations; custom objects allocate separately and may allocate more in their copy callback. Containers do not include those nested allocations in max_bytes. No global type registry, per-value mutex, implicit conversion or automatic serialization is added.

Portable Release observations, 100,000 iterations per set/get pair: Windows integer 7.68 ns and ordinary custom object 83.80 ns; WSL integer 8.00 ns and custom object 36.57 ns. These are single local observations under the current load, not a throughput improvement, universal performance guarantee or std::variant comparison. Re-run on target hardware before capacity/performance decisions.

Paired code/commands, local links/selected explicit anchors, Markdown fences, UTF-8 without BOM and LF pass checking. The two new variant Mermaid diagrams render successfully. The unchanged previous homepage diagrams also render. Markdown prose retains natural paragraph lines; C/C++ source follows the project's tab/width rules.

This change adds no internal concurrent activity: mutation and destruction require caller serialization; callback/custom state must be independently safe. TSan was not rerun for this change, and previous TSan records below remain historical. macOS, Win7, Android 5.0/API 21 runtime, musl and physical embedded/Android ARM boards remain pending. Emulator translation and cross-builds are explicitly separate from native target acceptance.

## Onboarding documentation (2026-10-01)

The two homepages now use seven numbered sections and six common tasks. Complete programs and step-by-step explanations are in USAGE.md / USAGE.zh-CN.md; the original 32-section content is in REFERENCE.md / REFERENCE.zh-CN.md. All original code, command and Mermaid blocks are retained byte-for-byte in the reference. Library sources, public interfaces and build targets are unchanged.

On Windows MSVC x64 and WSL Linux GCC, each host compiled and ran 11 complete tutorial programs and five homepage excerpts with their documented prerequisites (16/16). Their displayed outputs match the text. Eight failure cases per host cover missing text files, malformed JSON, missing keys and failed integer conversion. The documented C99 and C++11 CMake projects also build and run from source on both hosts (four project builds); the C++ configurations disable exceptions.

The four new localized Mermaid diagrams render successfully. Paired examples/commands, local links/anchors, Markdown fences, UTF-8 without BOM, LF and code-block widths pass inspection. This is a documentation/example check; full application suites, sanitizers, Android and pending target environments were not rerun for this prose change. Their earlier code results remain in the records below.

## Common-call refinement (2026-10-01)

The new C entries and C++ conveniences are additive. Existing C public layouts and effective calls remain; existing C++ facade owner layouts were not reordered. In-place ASCII edits are non-allocating void/reference operations. Settings/parameter cursors apply immediately and preserve prior successes on first failure. Borrowed text avoids copies; settings text instead retains immutable value storage across update/close.

| Configuration | Current evidence |
| --- | --- |
| Windows MSVC 19.40 x64 shared Release | Full CTest 84/84 |
| WSL Ubuntu GCC 15.2 x86_64 shared Release | Full CTest 86/86 |
| Windows minimal shared build | Full CTest 53/53, including disabled settings/catalog/parameter APIs |
| Windows MSVC 19.29 x86 static Debug | Build and 7/7 affected tests |
| ASan + UBSan, leak detection enabled | 11/11 affected tests; no reported memory/undefined-behavior errors |
| TSan | 6/6 affected settings/C++/visitor tests; no reported races |
| Installed shared consumers, Windows x64 and WSL | Each 11/11 C/C++ programs and 7/7 pure C programs |
| Installed static consumers, Windows x86 and WSL | Each 11/11 C/C++ programs and 7/7 pure C programs |
| Existing old-header consumers against current shared library | Windows/WSL each 10/10 |
| Exact bilingual README code, Windows and WSL | Each 27 C/C++ blocks compiled; 25 runnable programs passed |
| C99/C++11 public headers | Isolated compilation in the host/Android builds; C++ exceptions disabled |
| Android API 21 ARM64 full/minimal shared | Both build; each 11/11 selected tests plus benchmark on API 34 emulator with ARM64 translation |
| Android API 21 ARMv7 full/minimal shared | Both build; runtime explicitly skipped for incompatible emulator ABI |

Failure-injected tests verify zero allocator calls for in-place ASCII edits and borrowed/retained string acquisition/copy, first-error preservation, skipped later setters, per-success observer notification/reentrant reads, unchanged outputs on wrong type/missing key, and complete tracked release. A retained old string is copied/released 10,000 times while another thread updates its setting 200 times; it also survives settings close and keeps the context BUSY until released. Visitors cover Unicode, a Windows name longer than 255 UTF-8 bytes, empty/exact-limit/excess-limit traversal, callback stop/native error details and failure cleanup. C++ tests distinguish successful File::read EOF from the preserved readSome EOF status.

This establishes behavior and allocation paths exercised here, not a throughput/latency improvement or proof against every possible leak or schedule. No new global locks, background work, dependency or allocator switch was introduced. Settings leases still use existing atomic references/root locks; visitors reuse a bounded buffer but native adapters may allocate. macOS, Win7, musl, physical embedded/Android ARM targets and Android 5.0 runtime remain unverified for this change. Mermaid bodies are unchanged from the previously rendered eight diagrams.

## C++ Result migration (2026-10-01)

The first-party C++ facade now uses Result<T>/Result<void> with compiler exceptions disabled. C ABI exports and public structure layouts are unchanged. The POD container macro uses explicit void-pointer erasure for pointer elements, resolving MSVC v142 C4090 without changing generated signatures.

| Configuration | Current result |
| --- | --- |
| Windows MSVC 19.40 x64, shared Release | Full CTest 82/82 |
| Windows MSVC 19.29 x86, static Debug | Full CTest 82/82 |
| WSL Ubuntu GCC 15.2 x86_64, shared Release | Full CTest 84/84 |
| Windows minimal shared build | Full CTest 52/52 |
| ASan + UBSan, leak detection enabled | 7/7 affected C++/pool/concurrency suites |
| TSan | 7/7 affected C++/pool/concurrency suites |
| Installed shared consumer, Windows and WSL | Each 10/10 C/C++ programs |
| Installed static consumer, Windows x86 and WSL | Each 10/10 C/C++ and 7/7 pure C programs |
| Old-header consumers with current shared library, Windows and WSL | Each 10/10; includes old C++ code using the unchanged C ABI |
| Exact README examples, Windows and WSL | Each 26 compiled, 24 runnable programs passed |
| Additional bilingual C++ documentation | 10 matching examples compile on MSVC/GCC with exceptions disabled |
| Mermaid | All eight bilingual diagrams rendered successfully |
| Android API 21 ARM64, full/minimal shared | Both compile; each 10/10 selected suites on API 34 emulator via ARM64 translation |
| Android API 21 ARMv7, full/minimal shared | Compile; runtime unavailable on this emulator |

The maintained tests cover explicit native errors, partial I/O/EOF, result moves/references, aligned results (16/32/64/128 bytes), queued/cooperative cancellation, same-pool deadlock, preparation/close races, discarded tasks and early request release. Injected allocation failures verify preserved strings/cursors, callback capture cleanup and balanced allocations. Compiler macro guards reject test builds with exception handling still enabled.

Android finite native join remains NOT_SUPPORTED with resources retained. Tests select an explicit infinite final join only after releasing their controlled work; the library adds no automatic fallback. These emulator results do not establish Android 5.0 runtime support, physical ARM performance or application sandbox behavior. macOS, Win7, musl and physical embedded targets remain pending for this migration.

Read [CPP_RESULTS.md](CPP_RESULTS.md) for the source migration and allocator/callback contracts. Rebuild C++ consumers and plugins together; old/new C++ template objects are not interchangeable. No performance improvement or universal absence of leaks is inferred from this test matrix.

## OBS completion batch 1 (2026-09-30)

Windows MSVC x64 Debug and WSL GCC 15.2 Debug passed all 12 CTest suites. The system-information, C++ platform and existing C++ pool suites passed ASan/UBSan (3/3). SYSINFO=OFF passed its C and C++ contracts on both hosts. Public C headers and all C++ facade headers compiled in isolation. The source-width check uses the repository's explicit 8-column Tab setting. macOS, Win7 runtime and embedded target validation remains pending.

## Object-layout checkpoint

On 2026-09-29, internal layouts were compared before/after on MSVC 19.40 x64, MSVC 19.29 x86, GCC 15.2 x86_64 and Clang 21.1.8 x86_64. Each probe measured 25 C objects, nine C++ result-state types and 216 complete C++ job types. The job matrix varies result kind, callable size and callable alignment. No measured type grew or changed alignment. This is evidence for that matrix, not a guarantee for every user-defined type, standard library or target ABI.

The C++ changes use an empty/result base when its size fits the aligned reference-count boundary, and retain member storage for short nonempty results. This avoids both extra void storage and a 32-bit alignment hole. TLS keys, config documents and object pools place their existing 8-byte aligned atomics before the smaller pointer members on 32-bit builds.

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

The aligned probe results each contain a byte array whose size equals the requested alignment. These are complete job sizes, including result and callable storage, not allocator size classes or process RSS. C atomics keep their original width/alignment. No packing directive, shared bitfield or cache-line padding policy was introduced. Already compact structures and intentional alignment probes were left unchanged.

All 13 public C headers match their pre-change hashes. Windows DLL exports still match the 235 public functions. The C++ facade's internal template layout has changed: rebuild its consumers and plugins together; do not mix objects instantiated from different header versions. Source APIs are intact. See [C++ storage and rebuild rules](CPP_POOL.md#storage-layout-and-rebuilding).

Validation after the layout change:

- Windows x64 static Debug, x64 DLL Release and x86/v142 Release: 10/10 suites per configuration, including isolated public headers.
- WSL GCC ASan/UBSan: 10/10 suites; TSan: the affected C++ task suite passed.
- Clang C++11 task suite passed against the existing compatible installed Linux C library. This is a facade check, not a rebuilt Clang C-library run.
- Windows DLL installation: 5/5 independent C/C++ consumers passed.
- New result-lifetime cases exercise 16/32/64/128-byte alignment, move-only transfer, discarding an unconsumed result and exceptions before a result is constructed. Existing cases cover cancellation, constructor failure, reference/void outcomes, allocator rollback and result retention.
- The five changed source files passed the skill's line-width check.

Temporary comparison sources, baseline copies and per-toolchain measurements are under the local ignored `build-layout/` directory. No throughput gain, cache-line improvement, ARM/Apple layout validation or hx_c_log performance acceptance is inferred from the size reductions.

## Containers and C++ task checkpoint

C containers and the optional C++11 facade were added after configuration parsing. Coroutines are deferred. The following are local compile/run results, including the existing foundation/configuration regression suites:

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

The container suite covers ring wraparound, random map operations against a reference model, ordered AVL insertion/deletion and comparison-count bounds, heap order, vector sort/search, iterator invalidation, byte keys, owned values, hx_string clone/move/rollback, alignment and finite item/byte budgets. Injected allocation failures reach initial hash buckets and later rehashes and verify existing entries and complete release. Reserved POD vector append and deque enqueue/dequeue are checked to cause no additional allocator calls. This is allocation-path evidence, not a throughput benchmark or a hard latency bound.

The C++ suite exercises lambdas, ordinary/member pointers, move-only values, reference/void results, user exceptions, constructor/allocation failures, queue-full rejection before callable copying, queued/cooperative cancellation, independent cancellation sources, same-pool deadlock rejection, close timeout and retry, preparation/close races, and results that outlive pool destruction. No sanitizer error was reported in the exercised paths; that is not proof of all possible thread schedules. Public C headers and the C++ facade compile independently and with repeated inclusion. Native event failure details are preserved on submission rollback.

The 23 changed first-party source/build files passed the skill's line-width check. The workflow passed actionlint and now explicitly tests CONTAINERS=OFF in the minimal job. The pinned uthash header and license SHA256 values match the retained vendor files. No vendor source was modified. The new bilingual container/pool documents spell out ownership and performance limits. UTF-8 without BOM / LF checks passed for 116 first-party files, and checked README/documentation links resolve inside the source tree.

Reproduce the new tests with the normal build and CTest commands in README. For a focused invocation on a built tree use `ctest -R containers` or `ctest -R cpp_pool`, with `-C Debug`/`Release` for multi-configuration builds. Use separate builds with `HX_OS_SANITIZER=address,undefined` and `thread` on GCC/Clang Linux, and `python tools/verify_install.py build --config Release` for the five installed consumers. The CMake 3.16 check used an explicit v142 CXX compiler path after refreshing a cached CXX=NOTFOUND result.

macOS and ARM/musl/uClibc-ng embedded SDK/board runs remain target work. Windows 7 SP1/8.1 remains the intended baseline; compiling with v142 on the current Windows host does not replace old-OS runtime validation. CI is prepared but was not run remotely. No hx_c_log migration or five-times-baseline performance acceptance was performed. Older checkpoint counts below are history and should not be confused with the current ten-suite configuration.

## Configuration-module checkpoint

After the foundation checkpoint below, configuration parsing was added and validated on the same development hosts:

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

The config suite covers UTF-8 paths/values, JSON large integers and embedded NUL, Unicode surrogate errors, exact XML mixed content, duplicate policies, format rejection, line endings, resource budgets, individual allocation failure points in every enabled parser, allocator reentry and four concurrent parser threads. Native-only YAML/TOML features have separate cases. Locale testing runs with a comma decimal locale when available (Windows here); the optional locale case is skipped when neither configured locale is installed. Final syntax fixes were rechecked on Windows and under ASan/UBSan.

The installed consumers include C99, C++11 and the configuration example. Public headers compile independently in both languages. The 29 changed first-party source/script files passed the skill's width checks after fixes; UTF-8 without BOM / LF was checked across 96 first-party files. All 592 regular files in the five parser archives were compared byte-for-byte with the extracted source. The updated workflow passed actionlint, but has not run remotely.

The older foundation numbers below are retained as history, not the final configuration-module suite count. macOS, old Windows releases and embedded SDK/board runs remain pending. No configuration fuzz campaign, complete language-conformance certification, throughput comparison, or hx_c_log integration/performance acceptance is claimed.


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

Public headers compile separately and twice per translation unit in C99 and C++11 without PCH or unity assumptions. First-party targets use the recorded warning profile with warnings as errors. Upstream iconv warnings are not counted as a clean first-party build. The bundled source-width checker passed on the covered first-party source/scripts; workflows were checked with actionlint. UTF-8/BOM/LF and documentation links are checked separately.

Build directories contain CTest's Testing/Temporary/LastTest.log, build logs and installed-consumer logs. They are ignored development artifacts. The verification scripts and CI workflow remain in source. CI has been authored and syntax-checked, not executed on GitHub in this workspace.

## Contract coverage

- core: alignment/context ownership, UTF-8 errors, status-returning clocks, retained events, timeout/retry, thread join and TLS-key lifetime.
- data: string aliasing/copy/move/format, invalid boundaries, embedded NUL, UTF-16 round trips, strict errors/replacement/progress, GBK/Shift-JIS/EUC-KR/ CP1252 vectors when enabled, pool bounds/alignment/reallocation/cache, objects/arena, file append/sync/short read/EOF and directory retry.
- paths: Chinese and supplementary-plane characters, nested paths longer than 260 characters, malformed UTF-8 rejection and cleanup.
- lifecycle: injected allocator failures and rollback, delayed native TLS destructor, concurrent pool allocate/reallocate/collect, and budget retention while an upstream free callback is still running.
- pool: bounded queue rejection, pending cancellation, cleanup exactly once, timeout retention, retry, task handles outliving the pool, self-wait rejection.
- net: local TCP accept/connect/copy-send/read/cancel/deadline, UDP empty and truncated datagrams (direct and asynchronous), localhost DNS, timers, posting, callback reentry checks and completed handles outliving a loop.
- loop_concurrency: four posting threads, 2,000 completions, bounded pending work, capacity rejection, zero-budget close of timers and closed admission.
- options: disabled constructors and legacy encodings report NOT_SUPPORTED.

These tests do not include forced regular-file short-write injection, disk-full/reboot durability, real UNC servers, every codepage mapping, suspend/resume, signal interruption stress, every native provider, delayed external DNS servers or plugin unload inside a running host. Those are target acceptance tasks, not claims hidden inside "cross-platform supported".

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

Android needs explicit bionic API-level and application permission/storage decisions. iOS needs deployment/sandbox/background execution and lifecycle checks; no fork is assumed. BSD needs native thread identifiers and finite join semantics, not a Linux fallback. Only the optional Apple performance activity adapter uses Objective-C/Foundation behind the C interface. Intel 10.13 and ARM64 11.0 deployment targets still require native validation. Embedded acceptance must lock CPU/float ABI, endianness, libc version/configuration, kernel floor, time_t width and atomic support.

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

On Windows use the DLL path and --tool dumpbin for export checks. Use the minimal switches documented in the README for disabled-feature validation. For sanitizers, use separate GCC/Clang build directories and set HX_OS_SANITIZER to address,undefined or thread. Do not combine ASan with TSan.

The workflow plans Windows/Linux/macOS static/shared builds, minimal features, sanitizers, native musl in a container, and ARMv7/AArch64 cross-builds with QEMU. Emulation is useful but does not replace actual board/SDK acceptance. On the actual board, build and run the same C example and applicable CTest suites, test 32-bit/time64 limits and real filesystem/network behaviors, then record compiler/SDK/kernel/libc versions and raw results.

## Performance boundary

The optional benchmark target was built and run on Windows and WSL. Its aggregate timings are development instrumentation, not controlled acceptance data or a before/after performance claim. No baseline subtraction or hx_c_log migration benchmark was performed.

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

No SIMD or hand-written bulk-copy assembly has been introduced without evidence. PAUSE/YIELD and matching atomics are localized. Measure size, throughput, tail latency, allocation count, lock contention and syscalls on the actual target before optimizing. A timer heap, reusable request storage or codec batching should follow a demonstrated workload need. The locked hx_c_log five-times and no-single-thread-regression criteria remain separate future gates.

## OBS completion batch 2 (2026-09-30)

Windows MSVC x64 and WSL GCC passed 14/14 suites; related ASan/UBSan passed 3/3 (library, C++ convenience, version-negotiation example). The LIBRARY=OFF C/C++ checks passed on both hosts. The test plugin is a separate module with a tiny C ABI, avoiding embedding non-PIC static library objects into the sanitizer fixture. macOS and Win7 runtime remain pending.

## OBS completion batch 3 (2026-09-30)

Windows/WSL full CTest passed 16/16; ASan/UBSan passed 4/4 (data, file_utils, cpp_platform, text_file_example). FILE_UTILS=OFF passed C/C++ tests on both hosts; FILE_UTILS=ON with CONTAINERS=OFF passed on Linux. Tests exercised UTF-8 long paths, BOM policy, exact input limits, output preservation, same-file copy rejection, backup collisions, cwd round trips, glob, bounded cleanup and allocator ownership. Windows symlink creation was skipped for insufficient privilege; Linux no-follow removal passed. Other native targets remain pending.

## OBS completion batch 4 (2026-09-30)

Windows/WSL full CTest passed 18/18; related ASan/UBSan passed 4/4 (env, environment_example, cpp_platform, file_utils). ENV=OFF checks passed on both hosts. Coverage includes empty versus missing values, UTF-8, retained outputs, conditional replacement, concurrent library calls and an output allocator that reenters environment reading without deadlock. Windows path regressions also passed after extracting the shared UTF-8/UTF-16 helper.

## OBS completion batch 5 (2026-09-30)

Windows/WSL full CTest passed 20/20; ASan/UBSan passed 6/6 (paths, env, file_utils, C++ wrappers and both new C examples). PATHS=ON with ENV=OFF passed 4/4 and PATHS=OFF passed 3/3 on each host, including isolated C99/C++11 headers. Coverage includes XDG relative/empty fallbacks, UTF-8, root preservation, buffer size negotiation, allocation failure and queries without creation. Windows known folders work in a test process without COM initialization. macOS, Win7 and embedded native validation remain pending.

## OBS completion batch 6 (2026-09-30)

Windows/WSL full CTest passed 22/22; related ASan/UBSan passed 5/5 (core, lifecycle, thread_extra, cpp_platform, thread_options_example). THREAD_EX=OFF passed 3/3 on both hosts with isolated C99/C++11 headers. Tests verify capacity-limited initialization with canaries, invalid stack sizes, actual Linux stack reservation, UTF-8 name truncation, cross-thread naming, semaphore overflow without partial updates, deadline timeout and subsequent successful retry/join. No additional lock/allocation enters default creation. Native Windows 7 naming fallback and Apple behavior still need target runs.

## OBS completion batch 7 (2026-09-30)

Windows/WSL full CTest passed 24/24; ASan/UBSan 7/7 and TSan 5/5. The relevant sets cover pipes, loop concurrency, C++ callbacks and the C transfer example; ASan also covers file and socket regressions. Tests include copied and borrowed writes, EOF/broken peer, preserved pending SIGPIPE, allocation rollback, shared admission limits, cross-thread cancellation versus completion, zero operation deadlines, callback reentry and close/retry without buffer retirement violations. The same C example executes on both platforms.

With NETWORK=OFF on both hosts: LOOP=ON/PIPE=ON passed 4/4; LOOP=ON/PIPE=OFF passed 3/3; LOOP=OFF/PIPE=ON passed 3/3. All build isolated C99/C++11 public headers. One Windows minimal incremental link reported LNK1236 for pipe_native; relinking that target unchanged passed, followed by its 3/3 tests. The root cause of that transient linker failure is not established.

Measured size/alignment (bytes), using the optional `hx_os_layout_report` target with HX_OS_BUILD_BENCHMARKS=ON:

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

The x86 result is a compiled/executed layout probe, not a full new x86 test run. macOS, native Win7, ARMv7/AArch64 and musl execution remain pending. Layout measurements and bounded syscall/allocation review are not throughput evidence.

## OBS completion batch 8 (2026-09-30)

Windows/WSL full CTest passed 26/26; related ASan/UBSan 6/6 and TSan 4/4. NET_OPTIONS=OFF/POSITIONED_IO=OFF passed 3/3 per host with NETWORK enabled. NETWORK=OFF/POSITIONED_IO=ON passed 4/4 per host; Windows also had FILE_UTILS and all unrelated convenience modules disabled. Independent C99/C++11 headers compiled. Tests cover four simultaneous positioned readers/writers and an unchanged shared cursor, first-use handle publication, short reads, EOF, invalid ranges, append/readonly rejection, explicit socket reuse, actual option lengths, peer addresses and post-attachment mutation rejection.

Windows raw TCP_NODELAY returned one byte; the test now zero-initializes storage and checks the actual native length instead of assuming sizeof(int). Native raw results are unchanged. The private file layout probe measured x86 Windows 16/4, x64 Windows 24/8, x64 Linux 12/4 (size/alignment); other reported objects remain unchanged. These are layout/correctness results, not a throughput benchmark.

The pipe byte-count pointer correction follows Microsoft's documented Win7 [ReadFile](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-readfile) and [WriteFile](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-writefile) contracts. Win7/macOS/embedded runtime evidence still remains pending.

## OBS completion batch 9 (2026-09-30)

Windows/WSL full CTest passed 28/28; ASan/UBSan passed 5/5 (data, file_utils, file_info, cpp_platform and file_info_example). FILE_INFO=OFF/BOM=OFF passed 3/3 on each host. All new public headers compiled independently as C99/C++11. Tests cover capacity canaries, metadata failure preservation, native read-only attributes, POSIX owner mode projection and symlink follow modes, directory capacity retries/types and all five Unicode BOMs including overlapping UTF-32LE prefixes. POSIX permission tests use a unique /tmp directory on the native filesystem, avoiding assumptions about Windows-mounted permission emulation. Windows ACL permissions are deliberately marked unknown, not inferred.

The extended info layout probe measured 72/8 bytes (size/alignment) on Windows x86/x64 and Linux x64. Existing public layouts remain unchanged. Native macOS, Win7 and embedded runs, Windows symlink privileges and a filesystem that forces DT_UNKNOWN directory fallback remain separate target validation.

## OBS completion batch 10 (2026-09-30)

Windows/WSL full CTest passed 30/30; related ASan/UBSan and TSan each passed 4/4 (config, config_serialize, cpp_platform, config_serialize_example). Serialization checks cover exact large JSON numbers, escaped NUL/Unicode, INI grouping, properties round trips, incompatible-type rejection, depth/node/work/output/heap limits, allocation failure, concurrent immutable readers and atomic save/reload. All existing enabled parser backends passed after numeric-helper extraction.

Double checks cover signed zero, DBL_MIN/DBL_MAX, round trips, overflow, complete token rejection and canonical infinities/NaNs. Both hosts passed under a comma numeric locale, checking that the process locale and current decimal separator were restored. WSL initially lacked a generated alternate locale; its existing localedef and OS locale sources generated a private de_DE.UTF-8 under build-obs-linux/test-locales. Only the test command used LOCPATH; no system locale or installed dependency was changed. Full/ASan/TSan runs used that private locale.

CONFIG=ON with CONFIG_SERIALIZE/TEXT_NUMBER=OFF passed 3/3 per host, preserving the existing config double getter. CONFIG=OFF/TEXT_NUMBER=ON passed 3/3 per host. Windows CONFIG_SERIALIZE=ON/FILE_UTILS=OFF passed 4/4, including explicit save rejection. Isolated C99/C++11 headers and source rules passed. Existing public layouts were not expanded. macOS, Win7 and embedded runtime remain pending.

## OBS completion batch 11 (2026-09-30)

Windows/WSL full CTest passed 32/32. ASan/UBSan and TSan each passed 6/6 (timer, pipe, loop_concurrency, cpp_platform and the two async examples). Coverage includes persistent request limits, allocator failure/no per-tick allocation, slow callback coalescing, worker-thread stop/wait, cleanup once, self-close rejection, loop destruction ordering, legacy one-shot behavior and C++ exception/capture retirement. Public headers compiled as C99/C++11.

TIMER=OFF/LOOP=OFF passed 4/4 per host. Additional Windows LOOP=ON/TIMER=OFF passed 3/3; Linux NETWORK=OFF/LOOP=ON/TIMER=ON passed 4/4. Layout probes ran on Windows x86/x64 and Linux x64 (timer 40/8, 64/8, 64/8 bytes respectively). macOS, Win7 and embedded runtime remain pending; no hard deadline is claimed.

## OBS completion batch 12 (2026-09-30)

Windows/WSL full CTest passed 32/32; ASan/UBSan and TSan each passed 3/3 (pool, cpp_pool, lifecycle). THREAD_POOL=OFF passed 2/2 per host. Public headers compiled independently as C99/C++11. Identity tests distinguish the host, the own worker, its cleanup and another pool. Self-wait/close reject deadlock, while finished own-pool tasks remain readable. No layout, allocation, lock, syscall or worker-path change; the query reuses existing TLS. Native target runs pending.

## OBS completion batch 13 (2026-09-30)

Windows/WSL full CTest passed 33/33; ASan/UBSan and TSan each passed 3/3 (core, error_text, cpp_platform). NETWORK=OFF passed 3/3 per host, with GAI messages available without network initialization. Tests cover native text, UTF-8 validity, unchanged numeric fallback, out-of-range codes, capacity/NUL/ canary handling, four concurrent callers and the C++ owned message. Public headers compiled as C99/C++11. A Windows SDK macro collision in a test-local variable was corrected before the final Windows build. Mach implementation and its guarded test remain pending on macOS; Win7 and embedded runs are pending.

## OBS completion batch 14 (2026-09-30)

Windows full CTest passed 35/35; Linux 37/37 adds two isolated-process fault tests. ASan/UBSan and TSan passed 7/7 each. Linux seccomp injected ENOSYS and EIO only in the dedicated test executables: the real random-device fallback ran, and failure clearing/native code/UUID preservation passed (neither was skipped). Other checks cover a 1 MiB fill, four concurrent callers, UUID bits/format/ overlap/string output, CRC32 known and incremental vectors, and clearing bounds. Public C99/C++11 headers and RANDOM=OFF combinations passed (3/3 per host). Linux's minimal build also disabled explicit_bzero to exercise portable clearing. No existing object layout or hot path changed. Windows legacy-provider failure, early-boot entropy starvation, macOS, Win7 and embedded runs remain pending.

## OBS completion batch 15 (2026-09-30)

Windows full CTest passed 37/37; Linux 39/39. ASan/UBSan and TSan each passed 4/4 (data, vm, cpp_platform, mapped_file_example). VM=OFF passed 3/3 per host; VM=ON with SYSINFO=OFF passed 4/4 per host, also FILE_UTILS=OFF on Windows. New public C99/C++11 headers compile independently. Windows x86 VM tests and x86/x64 layout probes ran successfully (layouts in PLATFORM_UTILITIES).

Tests cover rounded/zeroed pages, protection transitions, invalid permissions, allocation failures, empty/oversize/unaligned maps, unchanged file cursor, context retention, access after original file close, private-write isolation, shared durable flush/reopen, append/pipe rejection and C++ moves/RAII. Native execute policy rejection is permitted with a native error; no generated code is executed. Truncation faults, network filesystem coherence, native unmap failure, Apple hardened runtime, Win7 and embedded machines remain target validation.

## OBS completion batch 16 (2026-09-30)

Windows full CTest passed 39/39; Linux 41/41. ASan/UBSan and TSan each passed 4/4 (file_info, file_extra, cpp_platform, file_attributes_example). FILE_EXTRA off passed 3/3 per host; on with FILE_INFO off passed 4/4 per host, also with FILE_UTILS off on Windows. Public C99/C++11 headers passed. Windows x86 ran the cross-process/metadata suite; layout probes measured file_times 40/8 on all three tested ABIs.

Own test subprocesses verify shared/exclusive conflicts, elapsed monotonic timeouts, retry after timeout, unrelated ranges and unchanged cursors. The initial Windows helper failed because narrow CRT argv did not preserve UTF-8; it now converts the native Unicode command line explicitly and passes with Chinese filenames. Shell32 is a test-only dependency for this conversion. Metadata tests cover selected timestamps, omitted access time, native errors with and without error output, read-only flags, POSIX owner/group bits and symlink nofollow updates on /tmp. Capacity canaries and C++ lock transfer pass. macOS, Win7, 32-bit POSIX time ranges and remote/limited filesystems are pending.

## OBS completion batch 17 (2026-09-30)

Windows full CTest passed 42/42; Linux 44/44. ASan/UBSan and TSan each passed 7/7 (watch, loop_fd, pipe, timer, loop_concurrency, cpp_platform and the watch example). Both modules off passed 4/4 per host; WATCH on with LOOP off passed 5/5 on Windows. Linux NETWORK off/local LOOP plus both watchers passed 5/5. Public C99/C++11 headers passed. Windows x86 watch/stub tests passed 2/2. Layout probes measured the values recorded in PLATFORM_UTILITIES.

Tests cover injected allocation failures with no transferred cleanup on rejection, Unicode/supplementary-plane names, directory limits/rebuild overflow, invalid POSIX names, callback cleanup once, self-close, timeout retention/retry and C++ exception/capture release. External descriptor tests cover duplicated ownership after original close, readiness I/O, request budgets, unsupported native descriptors, cleanup reentry and loop destruction ordering.

Initial WSL watch tests on the Windows-mounted checkout received no events. A direct native inotify probe, using both paths and /proc/self/fd, confirmed 96 notification bytes per native /tmp case and zero per /mnt/e case. statfs identified tmpfs versus 9p. The adapter now conservatively rejects 9p instead of accepting silent coverage; Linux tests use unique /tmp fixtures, and the rejection is itself tested. No timeout increase was used to hide this behavior. Native kernel-buffer saturation, unregister-failure retry, missing procfs, Apple compilation/runtime, Win7 and embedded hardware remain target validation.

## OBS completion batch 18 (2026-09-30)

Windows MSVC x64 full CTest passed 46/46; WSL Linux passed 48/48. ASan/UBSan and TSan each passed 7/7 (environment, pipe, system, process, C++ platform, Unicode filename CLI and capture example). Public headers compiled independently in C99/C++11. SYSTEM_UTILS/PROCESS off passed 5/5 on each host. Windows enabled process/system with LOOP/NETWORK/ENV/PATHS disabled passed 5/5; Linux local capture with NETWORK/ENV/PATHS disabled passed 7/7. Windows x86/v142 built and passed the four related tests, including the existing file utility test and Unicode CLI example.

Controlled project-built children verify empty/space/quote/backslash/Chinese/ supplementary-plane arguments, complete environment and empty environment, stdin EOF, simultaneous 320 KiB per output, timeout/resource retention/retry, limit raise and explicit discard, raw binary/strict text conversion, repeated wait, nonzero exit and actual Windows exit code 259. Injected allocator failure at 64 call positions verifies zero outstanding context allocations and successful cleanup. Snapshot enumeration reenters environment mutation outside the lock. CPU checks verify AVX2 implies usable AVX. Linux emulation remains unknown.

The x86 v142 format checker rejected an existing non-ASCII snprintf format despite /utf-8. Keeping the format ASCII and passing the same Chinese filename as a %s argument preserves the test and passed; warnings were not disabled. An incremental minimal Windows library reported an invalid COFF object; rebuilding that library target resolved it and the configuration passed.

New size/alignment probes: Windows x86 string_list 8/4, process 24/8, process_options 20/4, result 8/4, capture_options 16/4. Windows x64: 16/8, 32/8, 32/8, 8/4, 24/8. Linux x64: 16/8, 24/8, 32/8, 8/4, 24/8. Existing public objects and loop/request sizes are unchanged.

Release division microbenchmark, one million divisions, single illustrative samples: MSVC x64 helper/native 2509100/1998700 ns; WSL Clang x64 2604750/2521124 ns. The helper checks zero while the bare expression does not; these samples do not establish a speedup or embedded performance claim. No assembly was introduced. Native macOS, Win7 console inheritance, ARM/musl, missing-procfs/OS-failure injection and actual embedded boards remain pending.

## OBS completion batch 19 (2026-09-30)

Windows MSVC x64 full CTest passed 48/48; WSL Linux passed 50/50. Related ASan/UBSan and TSan each passed 7/7 (thread_extra, delayed, pool, cpp_pool, cpp_platform, loop_concurrency, delayed_task_example). Independent C99/C++11 headers passed. THREAD_EX/pool/delayed disabled passed 4/4 per host; pool enabled but delayed disabled passed 5/5 per host; enabled thread extensions/delayed tasks without networking passed 6/6 per host (Windows LOOP also disabled). Windows x86 passed 4/4 related tests.

Tests cover native affinity readback, Linux partial-mask rejection/restoration, valid/invalid scheduling requests and permission status; shared capacity, ready FIFO versus equal-due work, no early execution, future cancellation, drain timeout escalated to cancel-pending, cleanup on workers, 64-task cancel/close/destroy racing, retained tasks after pool destruction, C++ move-only arguments/results and cancellation, delayed allocation failure, and close racing delayed callback preparation. Real-time priority is tested only on a blocked worker and restored before releasing it.

Measured enabled private sizes/alignment: x64 task/pool/delay metadata 88/8, 104/8, 32/8 on Windows and Linux; x86 56/8, 64/8, 24/8. There is no public layout change. Ordinary work adds no allocation/lock count; future work adds one mutex and metadata, sorted insertion/cancel scanning, clock sampling and native timed waits.

The Release pool benchmark submits 16384 no-op tasks to one worker in chunks of 128, including wait/release. Static baseline executables were saved before changing the batch-18 implementation (9ba7624). Each comparison alternates baseline/current order across five processes per version, discards each process's warmup sample and retains 25 timed samples per version.

Final medians: Windows baseline/current 9003000/9140800 ns (+1.53% elapsed), Linux 12575598/12133141 ns (-3.52%). Ranges were 7593800..11159500 versus 7999800..11217300 ns on Windows, and 11517734..13372403 versus 11323393..13491747 ns on Linux. An earlier series had the opposite direction (Windows 10000500/9567700, Linux 11984708/12118367), before placing the empty future-list branch before the scheduler helper. The runs establish noisy local observations, not a stable improvement or universal no-regression claim. No assembly or extra ordinary-task locks/allocations were introduced. Raw results remain ignored build artifacts (batch19-pool*-perf.json). Native macOS, Win7, processor groups, CPU hotplug, ARM/musl and embedded boards remain pending. This is unrelated to the unperformed hx_c_log migration.

## OBS completion batch 20 (2026-09-30)

Windows full CTest passed 51/51; WSL Linux 53/53. ASan/UBSan and TSan each passed 8/8 (local, duplex, pipe, net, loop_concurrency, process, cpp_platform, duplex_message_example). Both native AF_UNIX tests ran; they were not skipped. Public C99/C++11 headers passed. Both new modules off passed 4/4 per host. Standalone duplex with NETWORK/PIPE/SYSTEM_UTILS off passed 7/7 per host (Windows LOOP also off), including the trimmed CLI argument path. Local sockets with DUPLEX_PIPE/PIPE off passed Windows 4/4 (LOOP off) and Linux 6/6 (LOOP on). Windows x86 related tests passed 4/4.

Tests verify UTF-8/supplementary-plane socket names, path bounds, unnamed peers, existing-path preservation, accept/read NOW timeouts, connect continuation, bidirectional stream data, short-read loops, IOCP/epoll read/write and cancel retirement, loop CONNECT/ACCEPT refusal, parent network BUSY, file half-close, EOF, write-after-shutdown, allocation failure cleanup and independent native network initialization. Linux fills a real listen backlog until EAGAIN, then accepts one connection and successfully retries the retained connecting object. Windows process redirection rejects socket-backed file endpoints before spawn.

The first local address query tests found that validation requires a non-NULL error-offset pointer. The local query and the shared trimmed CLI argument adapter now supply it; the existing validation contract was preserved.

Layouts: local_address 108/1 and private local_state 112/4 on all measured targets. With local support enabled, socket grows from 56/8 to 64/8 on x64, 40/8 to 48/8 on Windows x86. The same allocation appends state only for local sockets. Windows duplex_file is 32/8 on x64 and 20/4 on x86; POSIX uses the unchanged 12/4 file object. Public pre-existing layouts and operation/request sizes did not change. No assembly or new third-party dependency was added.

Native macOS, Win7 fallback/provider absence, embedded/musl, firewall rejection, wrong-peer accept flooding and native close-failure injection remain pending. No Windows/WSL interop or TCP performance equivalence is claimed.


## OBS completion batch 21 (2026-09-30)

Windows full CTest passed 57/57; WSL Linux 59/59. ASan/UBSan related tests passed 8/8. Public headers compiled independently as C99/C++11. All four switches off passed 5/5 on each host. Windows x86 related tests passed 5/5. Windows registry and performance enabled with RANDOM and SLEEP_INHIBIT off passed 6/6, validating independent native linking and trimming.

The Windows suites exercised exclusive temporary HKCU keys, Unicode values, REG_EXPAND_SZ literal reads, malformed native UTF-16, type mismatch, capacity preservation, readonly access, two independent sleep tokens, allocation failure rollback, idempotent activation and ownership transfer to a joined thread. Registry fixtures delete only the exclusive leaf created by the current test; exit cleanup is registered. Generated encoded C data was compiled and decoded, including non-ASCII UTF-8 and C++11 constexpr/empty literals.

Linux power and registry results validate NOT_SUPPORTED paths only. ASan on Linux does not validate the native Windows implementations. Native macOS, Win7 rejection of execution requests, actual sleep policy behavior, permission injection and embedded targets remain pending. Power requests are not a CPU frequency guarantee. Apple implementation source is uncompiled on this host. No TSan claim is made for Windows-only power ownership tests.

Measured private power state is 56/8 on Windows/Linux x64 and 32/4 on Windows x86; obfuscated borrowed view is 24/8 and 12/4 respectively. Existing public layouts remain unchanged. Objects allocate only when explicitly created; no background thread, global allocator, hot-path hook or assembly was added. The formatting configuration now also accepts the isolated Objective-C file; C/C++ formatting rules otherwise remain the same. Third-party sources were unchanged. Source checks and diff whitespace checks passed.


## OBS completion batch 22 (2026-09-30)

Windows full CTest 63/63; WSL Linux 65/65. Related ASan/UBSan 8/8 and TSan 6/6 passed. All three features off passed 4/4 on each host; Windows x86 related suites passed 7/7. Public C99/C++11 isolated headers, source rules and diff checks passed. No vendor sources changed.

Calendar tests walk every day from 1900 through 2101, include 0001/9999 bounds, negative milliseconds, leap centuries, invalid tokens, capacity canaries and spec/output aliasing. Separate child environments test UTC and east-of-UTC signs. German locale probes passed with Windows German_Germany.1252 and the existing private Linux de_DE.UTF-8 locale. Only test processes set locale/TZ. Translation tests exercise copied output, empty translation, missing fallback, capacity limits, allocation failure in entry/array/output, preserved old values, context ownership and four simultaneous read-only threads. Bit tests check known bytes plus all 0..64 widths at offsets 0..7, failure preservation, 64-bit values, zero width, overflow, padding and EOF. C++ wrappers and the combined C example ran.

Reader/writer layouts are 24/8 on Windows/Linux x64 and 12/4 on Windows x86; lookup options are 24/8 and 16/4. Existing public layouts remain unchanged. No existing hot path gained locks or allocations. Native Apple timezone rules, Win7, ARM/musl and embedded board runs remain pending; Linux locale tests do not count as Apple validation.


## OBS completion batch 23a: signals (2026-09-30)

Windows full CTest 66/66; WSL Linux 68/68. ASan/UBSan and TSan each passed 5/5 (data, calldata, signal, cpp_extensions, signal_notifications_example). Feature-off suites passed 3/3 per host and Windows x86 related suites 4/4. C99/C++11 isolated headers, source rules and diff checks passed.

Coverage includes generation-safe slot reuse, registration order, connect and disconnect from callbacks, snapshot skipping, callback native error/cause preservation, C++ exception containment, self/ancestor wait prevention, connection/emission/nesting limits, rejection without cleanup, and allocation failure through creation and emission. A custom allocator re-enters disconnect during snapshot allocation, proving that user allocation runs outside the handler lock. Four concurrent emitters are held in callbacks while another thread disconnects; after retirement no later callback starts and cleanup runs once. Separate barriers verify NOW close/wait failure while either callback or cleanup is still running, followed by successful retry. Stack parameters test all types, copied text including NUL, wrong/missing types, capacity exhaustion, replacement without arena growth, copied-pack independence and failed-set preservation. No network or external service is used.

Measured calldata is 776/8 on Windows/Linux x64 and Windows x86. Signal options are 32/8 (x64) and 20/4 (Windows x86); private slots 168/8 and 160/8; handlers 88/8 and 56/8. Default slot storage is 43008 bytes on x64; matching-ID snapshots cost 8 bytes per selected connection plus allocator overhead. No performance speedup is claimed. Native Apple, Win7 and embedded/musl runs remain pending. The settings and profiler portions of batch 23 are still pending at this commit.


## OBS completion batch 23b: settings (2026-09-30)

Windows full CTest passed 70/70; WSL Linux 72/72. Related ASan/UBSan and TSan passed 8/8 each (data, config, config_serialize, settings, settings_concurrency, settings_config, cpp_extensions, settings_example). Feature-off tests passed 4/4 per host; Windows x86 related tests 5/5. Windows SETTINGS on with CONFIG_SERIALIZE/TEXT_NUMBER/SIGNALS off passed 8/8, including double export through the shared private formatter. Linux SETTINGS on with CONFIG/TEXT_NUMBER/SIGNALS/CONTAINERS off passed 8/8. German locale probes passed on Windows and with the existing private Linux locale. Core code does not change the process locale. Public C99/C++11 headers, source rules and whitespace checks passed; vendor files remain unchanged.

Tests cover every value category, NUL and supplementary-plane keys/strings, duplicate keys, 64-level construction, finite-number rejection, default-only fallback and strict type errors. Snapshots remain unchanged and survive owner close; a deep copy into the default allocator stays valid after the source's custom context is destroyed. Creation, replacement and copy allocation failures roll back. Import and serialization inject failure at 32 allocation positions each, preserving old settings/output and freeing temporary trees. Allocation re-enters a getter outside locks; destruction-time deallocation re-enters and receives CLOSED safely. Observers re-enter reads/writes, hit their nesting bound, see immutable event values and detect self-close without stopping the object. A blocked observer proves timeout retention and retry; four writers publish 600 updates without losing distinct keys while holding snapshots concurrently.

JSON/INI/properties roundtrips, immutable source documents, explicit-only save, default preservation, pointer rejection, unsupported root/integer conversion, Unicode paths, corrupt-file rollback and native file-not-found detail were verified. C++ wrappers and the direct C example ran. Initial compilation found a test fixture missing directory_create's parents argument; the fixture now uses explicit nonrecursive creation. No existing directory is reused.

Size/alignment on Windows/Linux x64: options 80/8, change event 48/8, value 56/8, entry 24/8, owner 144/8. Windows x86: 44/4, 32/8, 40/8, 12/4, 80/8. These are new types; existing public layouts did not change. Updates copy a packed root/key block and retain unchanged child nodes; no performance speedup is claimed. Native Apple, Win7, ARM/musl and board runs remain pending. Profiler (23c) and atomic completion (24) remain pending at this commit.

## OBS completion batch 23c: profiler (2026-09-30)

Windows full CTest passed 72/72; WSL Linux 74/74. ASan/UBSan and TSan each passed 4/4 related suites (profiler, lifecycle, C++ extensions, C example). Windows x86 passed the same four suites. Independent public C99/C++11 headers compiled. PROFILER=OFF passed 3/3 on both hosts; the Linux profiler also passed 4/4 with config, signals and containers disabled.

Tests cover 201 merged completed paths, four concurrent recording threads, inclusive/exclusive consistency, native TLS exit aborts, explicit thread restart, UTF-8/escaped names, output/record/depth limits, atomic save, allocator failure/reentry, caller-scope deadlock, foreign idle TLS close timeout and retry. A warmed path repeated 1000 times made zero context allocations; this does not measure all native runtime activity or claim a speedup.

Measured size/alignment on Windows and Linux x64: options 56/8, owner 656/8, record 96/8, frame 24/8, thread cache header 16/8. Windows x86: 32/4, 336/4, 80/8, 24/8, 8/4 respectively. Names and bounded frame arrays are additional. Hot entries/leaves retain mutex sections, clock reads and TLS lookups. Apple, Win7, ARM/musl and board runs remain pending. Batch 24 is next.

## OBS completion batch 24: atomics (2026-09-30)

Windows full CTest 74/74; WSL Linux 76/76. Related ASan/UBSan and TSan each passed 10/10 (atomic, publication example, core, lifecycle, pool, loop concurrency, watch, settings concurrency, profiler, C++ extensions). Windows x86 passed 5/5; trimmed builds passed 5/5 on each host. Public C99 and C++11 headers compiled independently. Size/alignment is 8/8 for the old 64-bit type, 4/4 for atomic32; pointers are 8/8 on x64 and 4/4 on x86.

Tests exercise all legal orders plus conservative invalid-order fallback, success/failed/weak CAS expected values, signed-boundary wrapping, bitwise operations, 80,000 contended increments and 10,000 payload publications observed through failed acquire pointer CAS. Existing claim/publication call sites were reviewed; no stronger hidden order was required.

GCC TSan emitted its documented unsupported-fence diagnostic. Only this diagnostic is nonfatal for atomic.c under GCC TSan; the actual implementation is unchanged. TSan tests do not use fences as their publication mechanism. Fences were compiled/exercised, not certified by TSan. No assembly speedup or target ARM memory-order validation is claimed. See [contracts](ATOMICS.md). All 24 implementation batches are now present; final package/ABI matrix verification follows separately.

## Final delivery matrix (2026-09-30)

All 24 approved implementation batches are present. Batch 23 has separate signal, settings and profiler commits. No remote push, hx_c_log migration, coroutine implementation, new third-party dependency or vendor edit occurred.

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

The full ARMv7 run and the split AArch64 run retain failures rather than claiming native board acceptance. QEMU's two random fault-injection cases return the pre-existing skip code 77. A native-API diagnostic probe reproduced both remaining differences outside the library: 32-bit sysinfo returned 3696713728 bytes with unit 1 while host procfs and native x64 reported 20876582912 total bytes; available bytes were about 20 GB. Both emulated architectures returned success for posix_spawn of a nonexistent file, followed by child status 127. Native x64 returned ENOENT as expected. The library preserved those native results. Do not clamp memory metrics or fabricate spawn errors to hide the emulator behavior. CI explicitly excludes that process test and ARMv7 sysinfo in its QEMU-only job; board acceptance must include them. Other process-capture examples and C++ process tests passed under binfmt with QEMU_LD_PREFIX.

Windows shared validation found and fixed the generated-obfuscation test's DLL search directory. CMake 3.16/NMake validation found supplementary-plane command-line text becoming question marks; its generator now reads an actual UTF-8 input file. ARMv7 compilation found a size_t versus INT64_MAX check that was always false; the check now exists only at applicable widths. Optional C++ discovery moved to the tests/examples' common parent, preserving a C-only core. Package validation explicitly builds a separate LANGUAGES C consumer.

The 22 pre-existing public struct/union declarations were compared with the baseline and are unchanged (ignoring comments/whitespace); original operation and error layouts remain intact. The updated symbol checker includes the 14 macro-declared path functions and hx_settings exports. Full and minimal shared libraries have the same public symbol set. All tested public headers compile independently and twice in C99/C++11. First-party UTF-8/no-BOM/LF, changed source rules, documentation links and actionlint pass. Generated outputs/dependency caches remain ignored; vendor files match the Git baseline.

### Cost and performance observations

| Path | Cost retained or added |
| --- | --- |
| Existing atomics | Direct calls, constant-order dispatch; no allocator/callback; MSVC may be stronger |
| Ordinary pool tasks | No added ordinary-task allocation/lock count; delayed tasks share capacity and worker threads |
| Async pipe/timer/process composition | Bounded request/stream storage and native I/O; no hidden unlimited queue or automatic kill |
| Signals | Bounded callback snapshot per emit; callbacks outside lock; explicit in-flight retirement |
| Settings | Copied root/key storage plus retained immutable children; allocation and observer calls outside publication lock |
| Profiler | Warm paths allocate no context storage; entry/leave still uses mutex sections, TLS and monotonic clock reads |

The final Release pool comparison repeats batch 19's fixed workload against the saved static batch-18 binary: 16384 no-op tasks, one worker, chunks of 128, five alternating processes per version, five retained samples after each warmup. Windows baseline/current median was 9506100/9505400 ns (-0.01% elapsed); ranges 8128300..11529500 / 7505200..11280200. Linux was 12574817/12832931 ns (+2.05% elapsed); ranges 11615759..14325685 / 11078454..17594985. The overlapping/noisy observations do not prove a speedup or universal nonregression. Raw measurements are final-pool-perf.json in the corresponding ignored build directories. The hx_c_log five-times-baseline and single-producer nonregression gates remain future migration acceptance, not evidence from this pool benchmark.

### Reproduce and outstanding target work

Use the existing per-batch logs plus final-build.log/final-test.log, final-install.log and final-abi.log in the named build directories. Run tools/verify_install.py before tools/verify_abi.py for a shared configuration. The latter reads old headers/test sources through Git into its own ignored build directory; it does not check out or modify baseline/vendor sources. tools/check_exports.py accepts nm on Linux or --tool dumpbin on Windows. The CMake 3.16 run uses ctest from inside the build directory.

cmake/minimal.cmake is a 3.16-compatible initial-cache example, not a new runtime configuration layer. Override selected switches after -C and use the actual SDK toolchain/sysroot. Native Apple Intel 10.13 / ARM64 11.0, Win7/8.1, musl/uClibc-ng and physical embedded boards remain unverified. Apple source and target-local Objective-C warnings are implemented but uncompiled here. Cross-build warnings allowed by the existing GCC maybe-uninitialized policy remain diagnostics, not a clean-warning claim. No remote CI was executed. First-party distribution licensing is still undecided.

Failed batch-3 fixture directories under ignored build outputs remain: automatic approval review rejected their requested recursive cleanup with "blocked by policy". No alternative deletion bypass was attempted.

## 2026-10-01: common utilities and Android baseline

This extends the completed 24 batches; earlier counts and evidence stay intact. See [common contracts](COMMON_UTILITIES.md) and [Android usage](ANDROID.md).

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

Eight Android cross-builds passed using existing NDK 28.2.13676358 / Clang 19.0.1: both ARM ABIs at API 21/28 with default shared modules, both at API 21 with minimal shared modules, and both at API 21 with default static modules. Each includes C99/C++11 public headers, tests, examples and benchmarks. adb found no device: target execution, performance and sandbox acceptance remain pending. CI configuration passes actionlint; no remote CI run occurred.

New coverage includes UTF-8/NUL/aliasing, transactional failure, allocation faults, catalog overlays, lexer position/sticky errors, 48 independent BigInt wide-arithmetic reference vectors, rounding/overflow, monotonic waits/repeat, procedure self-wait/in-flight close/retry, allocator reentry, warm allocation counts and C++ exception conversion. Win32 also exercises portable arithmetic. Affected Windows suites passed again after adding the QPC spin-budget guard.

### Layout and performance observations

The 47 pre-existing named public struct/union declarations in 298bf51 are unchanged after removing whitespace/comments. New size/alignment in bytes:

| Type | MSVC x64 / Linux x64 | MSVC Win32 |
| --- | --- | --- |
| hx_os_uint128 | 16/8 | 16/8 |
| hx_os_token | 48/8 | 24/4 |
| hx_os_lexer_options | 40/8 | 24/4 |
| hx_os_proc_options | 32/8 | 20/4 |

Natural alignment, no packing. ASCII edits/wide arithmetic add no allocation or lock. Registered procedure calls allocate no context storage, but still scan slots, lock and invoke indirectly. Signals share the existing retirement implementation. Ordinary pool task paths are unchanged.

Release shared, 100000 iterations, median of three local observations (ns/op):

| Operation | Windows x64 | WSL Linux x64 |
| --- | --- | --- |
| Relaxed atomic increment | 8.58 | 8.36 |
| ASCII lower/upper pair | 24.91 | 22.20 |
| 64*64/64 | 198.37 | 6.24 |
| Single named procedure | 341.46 | 388.18 |

Windows uses portable wide arithmetic; Linux x64 uses compiler 128-bit integers. No cross-platform parity claim. Windows benchmark intervals use QPC so short operations are not quantized to zero by the deadline clock. Raw outputs: build-shared-msvc/util-perf.log and build-shared-linux/util-perf.log. There is no equivalent old workload baseline, so these are not speedup claims.

Default 200 us spin, 2 ms request, 30 waits per run: Windows mean lateness was about 13.1..13.5 ms; WSL about 0.31..0.72 us. These are local clock/scheduler observations, never precision guarantees. Windows keeps its existing deadline domain and bounds spinning independently with QPC before falling back to sleep. No global timer-resolution change. hx_c_log performance gates remain future migration acceptance.

### History and reproduction

27 of 28 historical commit messages were shortened while retaining every source tree, author and timestamp. Root 7cf52fc remains; old history is kept on backup/pre-message-cleanup-20261001. Old tip 622ee2e maps to 298bf51; historical document hashes remain accessible through the backup. No push. New commits use short Chinese subjects.

Build directories contain util-build/test/install/abi logs; final focused checks use util-final/util-budget prefixes. Android records are report.json and report-minimal.json. Outputs/SDK/dependency caches remain untracked and vendor sources unchanged. Existing macOS, Win7/8.1, musl and physical-board validation gaps remain.

## 2026-10-01: Android emulator execution

The earlier empty adb list overlooked the stopped Qt_Android_34 AVD already installed on this host. It was booted with -read-only, -no-snapshot and -no-window, without wiping user data. Runtime: Android 14 / API 34, native x86_64 with libndk_translation. Fingerprint: google/sdk_gphone64_x86_64/emu64xa:14/UE1A.230829.036.A4/12096271:user/release-keys.

| Build minimum API | ABI | Configuration | API 34 emulator result |
| --- | --- | --- | --- |
| 21 | x86_64 | Default modules, shared | Native ISA, 8/8 |
| 28 | x86_64 | Default modules, shared | Native ISA, 8/8 |
| 21 | arm64-v8a | Default modules, shared | Translated, 8/8 |
| 28 | arm64-v8a | Default modules, shared | Translated, 8/8 |
| 21 | arm64-v8a | Default modules, static | Translated, 8/8 |
| 21 | arm64-v8a | Minimal modules, shared | Translated, 8/8 |

Six configurations, 48 successful suite executions, plus six completed benchmark runs. The eight-suite set is listed in [Android validation](ANDROID.md), not the entire desktop CTest collection. Minimal verifies unsupported/disabled behavior. Benchmark values include virtualization, translation and concurrent load; completion is recorded without a hardware performance claim.

Actual execution exposed ARM64 shared-library lookup failure with only LD_LIBRARY_PATH. Adding $ORIGIN RUNPATH to Android shared validation executables fixed default and minimal runs. Core library, consumer link options and system library paths remain unchanged. The runner now supports supplementary x86_64, records native machine/execution kind and per-program results, and no longer leaves a failed run labeled unrun. ARM defaults remain.

Reports: build-android-emulator/report.json, build-android/report.json, build-android/report-minimal.json, build-android-static/report.json. Configuration run-*.log files contain raw stdout/stderr. build-android-emulator/armv7-device-check.json records the unsupported ABI before deployment. build-android-emulator/arm64-execution-probe.json preserves the initial loader failure; arm-cross-build-report.json preserves the earlier cross-only report in the same directory.

Android 5.0/API 21 OS, ARMv7, native ARM hardware and APK application sandbox remain unverified. API 21/28 are build minima; all execution above used API 34. No remote CI execution is claimed.
