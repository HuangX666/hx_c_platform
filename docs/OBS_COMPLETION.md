# OBS capability completion

[简体中文](OBS_COMPLETION.zh-CN.md)

This page preserves the **historical acceptance of the initial 24 batches**. The original baseline was `7cf52fc` on the former `main` branch; those branch names, test counts and export counts describe that stage. Current development is on `dev`, and the repository has since been published. See [current validation](VALIDATION.md), [Android execution](ANDROID.md) and [dependency builds](DEPENDENCIES.md) for subsequent extensions.

The current acceptance scope is Windows, WSL Linux, Android and ARMv7/AArch64 with the available emulator/QEMU environments. Physical boards are recorded separately; macOS and older Windows runtime checks are deferred. Remote CI is manual-only and has not been run for this update.

| Batch | Capability | State |
| --- | --- | --- |
| 1 | System information and process CPU sampling | Windows/Linux 12/12; ASan 3/3; native targets pending |
| 2 | Dynamic libraries | Windows/Linux 14/14; ASan 3/3; native targets pending |
| 3 | File and path conveniences | Windows/Linux 16/16; ASan 4/4; target limits recorded |
| 4 | Environment variables | Windows/Linux 18/18; ASan 4/4; native targets pending |
| 5 | Special paths | Windows/Linux 20/20; ASan 6/6; native targets pending |
| 6 | Thread names, stack options, semaphore | Windows/Linux 22/22; ASan 5/5; native targets pending |
| 7 | Synchronous and asynchronous pipes | Windows/Linux 24/24; ASan 7/7; TSan 5/5; native targets pending |
| 8 | Socket options and positioned file I/O | Windows/Linux 26/26; ASan 6/6; TSan 4/4; native targets pending |
| 9 | Extended stat, directory kinds, BOM | Windows/Linux 28/28; ASan 5/5; native targets pending |
| 10 | Configuration serialization and floating point | Windows/Linux 30/30; ASan 4/4; TSan 4/4; native targets pending |
| 11 | Periodic timers | Windows/Linux 32/32; ASan 6/6; TSan 6/6; native targets pending |
| 12 | Pool worker identity query | Windows/Linux 32/32; ASan 3/3; TSan 3/3; native targets pending |
| 13 | GAI and Mach error text | Windows/Linux 33/33; ASan 3/3; TSan 3/3; Mach target pending |
| 14 | Random, UUID, CRC32, secure zero | Windows 35/35; Linux 37/37; ASan/TSan 7/7; targets pending |
| 15 | Pages and mappings | Windows 37/37; Linux 39/39; ASan/TSan 4/4; x86 VM passed |
| 16 | File locks, timestamps, permissions | Windows 39/39; Linux 41/41; ASan/TSan 4/4; x86 batch passed |
| 17 | Directory watch and external descriptor integration | Windows 42/42; Linux 44/44; ASan/TSan 7/7; native Apple pending |
| 18 | Processes, capture and system helpers | Windows 46/46; Linux 48/48; ASan/TSan 7/7; x86 batch passed |
| 19 | Priority, affinity and delayed tasks | Windows 48/48; Linux 50/50; ASan/TSan 7/7; x86 batch passed |
| 20 | Local sockets and duplex pipes | Windows 51/51; Linux 53/53; ASan/TSan 8/8; x86 batch passed |
| 21 | Registry, power and obfuscation | Windows 57/57; Linux 59/59; ASan 8/8; x86 5/5; Apple pending |
| 22 | Dates, translation table and bit streams | Windows 63/63; Linux 65/65; ASan 8/8; TSan 6/6; x86 7/7 |
| 23 | Signals, mutable settings and interval profiler | Complete: Windows 72/72, Linux 74/74; profiler ASan/TSan 4/4; native targets pending |
| 24 | Atomics and memory ordering | Windows 74/74; Linux 76/76; ASan/TSan 10/10; x86 5/5 |

The baseline preserves pre-existing whitespace, including vendor licenses. Build products, dependency caches and install outputs are excluded from Git.

At the end of that initial stage, all 24 implementation batches were complete. Its final static/shared installation, 576-symbol export checks and old-header/new-library ABI probes passed on Windows and WSL. See [the final matrix](VALIDATION.md) for the full sanitizer, Clang, x86 and CMake 3.16 results, partial QEMU runs and native target limits. This is not a claim of macOS/Win7/embedded-board acceptance or hx_c_log speedup.
