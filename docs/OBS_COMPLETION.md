# OBS capability completion

[简体中文](OBS_COMPLETION.zh-CN.md)

The approved scope is all 24 batches, including synchronous and asynchronous
pipes, direct C convenience entry points and optional C++11 wrappers.
The reference source baseline is commit `7cf52fc` on `main`; development uses
`codex/obs-platform-completion`. No remote publication is requested.

Every completed batch requires Windows MSVC x64 and WSL Linux full CTest,
relevant AddressSanitizer tests, isolated public headers, feature-off checks
and bilingual contracts. macOS Intel 10.13 / ARM64 11.0, Windows 7 SP1 and
embedded target runs remain separately identified target validation.

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
| 14 | Random, UUID, CRC32, secure zero | Pending |
| 15 | Pages and mappings | Pending |
| 16 | File locks, timestamps, permissions | Pending |
| 17 | Directory watch and external descriptor integration | Pending |
| 18 | Processes, capture and system helpers | Pending |
| 19 | Priority, affinity and delayed tasks | Pending |
| 20 | Local sockets and duplex pipes | Pending |
| 21 | Registry, power and obfuscation | Pending |
| 22 | Dates, translation table and bit streams | Pending |
| 23 | Signals, mutable settings and interval profiler | Pending |
| 24 | Atomics and memory ordering | Pending |

The baseline preserves pre-existing whitespace, including vendor licenses.
Build products, dependency caches and install outputs are excluded from Git.
