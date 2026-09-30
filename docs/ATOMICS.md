# Atomics and memory ordering

[简体中文](ATOMICS.zh-CN.md)

The C99 storage types and functions in hx_os_atomic.h work from C++11 too.
Existing hx_os_atomic remains an 8-byte, 8-aligned signed counter;
hx_os_atomic32 is independently 4-byte, 4-aligned. Pointers use native pointer
alignment. Do not pack these types, copy live storage, access the raw member
concurrently, or reinterpret a std::atomic as hx storage. Initialize before
publication; the containing owner controls lifetime. Pointer operations
provide no reclamation, ownership transfer or ABA protection.

For the usual result-publication pattern use release store and acquire load,
as in [the shared C example](../examples/atomic_publication.c). The same API
works in C++. A C++-only owner may use std::atomic for its own distinct storage.
Long waits should use events or conditions rather than spin on cpu_relax.

| Operation | Valid orders |
| --- | --- |
| load | relaxed, acquire, seq_cst |
| store | relaxed, release, seq_cst |
| fetch add/sub/or/and/xor, exchange | all five |
| CAS success relaxed or release | failure relaxed |
| CAS success acquire or acq_rel | failure relaxed or acquire |
| CAS success seq_cst | failure relaxed, acquire or seq_cst |
| fence | all five; relaxed is a compiler barrier only |

Strong CAS never fails spuriously; weak CAS permits spurious failure and
should normally be retried in a loop. Success returns 1 and preserves expected;
failure returns 0 and overwrites expected with the observed value. expected
must be separate writable, nonconcurrently accessed storage. Fetch/exchange
returns the previous value. Arithmetic wraps modulo the integer width.
Invalid enum values or order combinations use seq_cst conservatively; CAS
uses seq_cst for both orders. There is no implicit process termination.

GCC/Clang branches supply constant memory-order arguments. Passing a runtime
integer directly can make GCC choose seq_cst, so a simple enum conversion is
insufficient. MSVC uses Interlocked operations and is never weaker than the
requested order; weak CAS is strong on this implementation and on native x86.
The MSVC nonrelaxed fence uses a full-barrier Interlocked operation on private
stack storage; relaxed uses _ReadWriteBarrier. GCC/Clang relaxed fence uses an
empty compiler memory barrier and other fences use __atomic_thread_fence.
Fences require appropriate atomic communication; they do not repair ordinary
data races or turn volatile into synchronization.

Compiler references:
[GCC atomic builtins](https://gcc.gnu.org/onlinedocs/gcc/_005f_005fatomic-Builtins.html),
[Microsoft Interlocked CAS](https://learn.microsoft.com/en-us/cpp/intrinsics/interlockedcompareexchange-intrinsic-functions).

is_lock_free queries report a conservative compiler/target guarantee for
correctly aligned storage. GCC targets without native 64-bit operations link
the toolchain's libatomic when detected by CMake. Operations do not allocate
through hx contexts or invoke user callbacks; runtime fallback locks may
exist. Lock-free is not wait-free or universal signal safety. No hand-written
optimization assembly was added; existing architecture wait hints remain.

Batch 24 reviewed current CAS/exchange users: thread joining, pool close and
watch close use acquire claims paired with release relinquishment; joined
results retain their own release/acquire publication. IOCP wake suppression
uses acq_rel CAS, a release reset and the existing queue lock. Apple's watch
run-loop pointer uses release publication and acquire consumption; its later
relaxed access is after joining. No call site requires hidden seq_cst ordering.

The atomic core is always available, independently of optional modules.
GCC TSan does not model standalone fences: its warning remains visible but
is not a compile error for atomic.c in that one configuration. Publication
tests use acquire/release operations, including failed acquire CAS, rather
than relying on unmodeled fences. A TSan pass is not fence verification or
a substitute for ARM target runs.
