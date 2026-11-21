# Atomics and memory ordering

[简体中文](ATOMICS.zh-CN.md)

Use `<hx_os_atomic.h>` when one thread needs to publish computed data to another without locks, or when maintaining cross-platform atomic counters, flags, and state machines in C99 or C++11. The atomic module is part of the core library and is always available regardless of optional module trimming.

### Quick Start Example: Release-Acquire Publication

Open [atomic_publication.c](../examples/atomic_publication.c) for a complete runnable program: a worker thread writes `42` to a plain variable `answer` and publishes readiness with an `HX_OS_RELEASE` store to `ready`; the main thread observes `ready == 1` using an `HX_OS_ACQUIRE` load, safely reads `answer`, and then joins and destroys the worker thread.

Follow the [example run instructions](../examples/README.md#run) to enable examples and build `hx_os_atomic_publication` (no command-line arguments or input files are required):

```sh
cmake --build build --config Release --target hx_os_atomic_publication
```

Expected output:

```text
answer: 42
```

You can copy [the shared C example](../examples/atomic_publication.c) into your own project using the [minimal project](USAGE.md#c-project) setup.

> [!TIP]
> **When to spin vs. when to block**
> The `hx_os_cpu_relax()` loop in the example illustrates a brief cross-thread handoff. For longer waits—such as disk I/O, network replies, or hardware device responses—use an event (`hx_os_event`) or condition variable (`hx_os_cond`) instead of busy-spinning.

## Storage types and alignment rules

`<hx_os_atomic.h>` provides three atomic storage types usable from both C99 and C++11:
- **`hx_os_atomic`**: 64-bit signed integer (`int64_t`), 8-byte sized and 8-byte aligned (static initializer `HX_OS_ATOMIC_INIT(v)`).
- **`hx_os_atomic32`**: 32-bit signed integer (`int32_t`), 4-byte sized and 4-byte aligned (static initializer `HX_OS_ATOMIC32_INIT(v)`).
- **`hx_os_atomic_ptr`**: Untyped pointer (`void *`), aligned to native pointer alignment (static initializer `HX_OS_ATOMIC_PTR_INIT(v)`).

> [!WARNING]
> **Storage lifetime and aliasing rules**
> - Never pack structs containing atomic types (`#pragma pack`), `memcpy` live atomic storage, or read/write the raw `.value` member concurrently with atomic operations.
> - Never `reinterpret_cast` between C++ `std::atomic<T>` and `hx_os_atomic*` or alias the same memory with both.
> - `hx_os_atomic_ptr` makes pointer loads, stores, and CAS operations atomic, but does not provide automatic memory reclamation, ownership transfer, or ABA protection.

## Memory orders and operation contracts

Five memory-order constants are provided: `HX_OS_RELAXED`, `HX_OS_ACQUIRE`, `HX_OS_RELEASE`, `HX_OS_ACQ_REL`, and `HX_OS_SEQ_CST`. Each operation accepts the following orders:

| Operation | Valid `hx_os_memory_order` arguments |
| --- | --- |
| `load` | `HX_OS_RELAXED`, `HX_OS_ACQUIRE`, `HX_OS_SEQ_CST` |
| `store` | `HX_OS_RELAXED`, `HX_OS_RELEASE`, `HX_OS_SEQ_CST` |
| `fetch_add` / `sub` / `or` / `and` / `xor`, `exchange` | All five memory orders |
| `compare_exchange` (success `HX_OS_RELAXED` or `HX_OS_RELEASE`) | Failure order must be `HX_OS_RELAXED` |
| `compare_exchange` (success `HX_OS_ACQUIRE` or `HX_OS_ACQ_REL`) | Failure order may be `HX_OS_RELAXED` or `HX_OS_ACQUIRE` |
| `compare_exchange` (success `HX_OS_SEQ_CST`) | Failure order may be `HX_OS_RELAXED`, `HX_OS_ACQUIRE`, or `HX_OS_SEQ_CST` |
| `hx_os_atomic_fence` | All five orders (`HX_OS_RELAXED` acts as a compiler barrier only) |

- **Compare-and-swap (CAS)**:
  - `compare_exchange` (strong CAS) never fails spuriously when the value matches `*expected`; `compare_exchange_weak` (weak CAS) may fail spuriously on LL/SC architectures and should be used in a retry loop.
  - Returns `1` on store (leaving `*expected` unchanged), or `0` on mismatch (overwriting `*expected` with the observed value). `expected` must point to distinct, writable thread-local storage that does not overlap the atomic variable.
- **Fetch and exchange return values**: `fetch_*` and `exchange` return the **previous value** held before the modification, and arithmetic wraps modulo $2^{\text{width}}$.
- **Conservative fallback for invalid orders**: Passing an out-of-range enum value or an invalid CAS success/failure order combination never aborts the process; instead, the call conservatively upgrades to `HX_OS_SEQ_CST` (for both CAS orders).

## Compiler backends and lock-free queries

- **GCC / Clang and MSVC mapping**:
  - On GCC and Clang, each memory order dispatches to a compile-time constant `__atomic_*` builtin (avoiding GCC's fallback that promotes runtime integer order variables to `seq_cst`), and non-`HX_OS_RELAXED` fences call `__atomic_thread_fence`.
  - On MSVC, operations map to `Interlocked*` intrinsics that are never weaker than the requested memory order (`compare_exchange_weak` uses strong CAS on MSVC and native x86; non-`HX_OS_RELAXED` fences execute a full-barrier `Interlocked` operation on private stack storage, while `HX_OS_RELAXED` uses `_ReadWriteBarrier`).
  - Compiler documentation: [GCC atomic builtins](https://gcc.gnu.org/onlinedocs/gcc/_005f_005fatomic-Builtins.html), [Microsoft Interlocked CAS](https://learn.microsoft.com/en-us/cpp/intrinsics/interlockedcompareexchange-intrinsic-functions).
- **Lock-free capability queries (`is_lock_free`)**: `hx_os_atomic_is_lock_free()`, `hx_os_atomic32_is_lock_free()`, and `hx_os_atomic_ptr_is_lock_free()` report whether properly aligned operations are lock-free on the current target. On 32-bit GCC targets without native 64-bit atomic instructions, CMake automatically detects and links `libatomic`. Atomic operations never allocate through `hx_os_context` or invoke user callbacks.
