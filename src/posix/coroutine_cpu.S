/* Private C ABI context switch. Offsets match coroutine_native.h.
 * Save nonvolatile integer registers, stack and floating-point controls.
 * Each bootstrap enters the common C trampoline on an aligned private stack.
 */
#if defined(__APPLE__)
#define HX_NAME(name) _##name
#define HX_PRIVATE(name) .private_extern HX_NAME(name)
#else
#define HX_NAME(name) name
#define HX_PRIVATE(name) .hidden HX_NAME(name)
#endif

.text
.globl HX_NAME(hx_os_i_coro_cpu_switch)
.globl HX_NAME(hx_os_i_coro_cpu_capture)
.globl HX_NAME(hx_os_i_coro_cpu_boot)
HX_PRIVATE(hx_os_i_coro_cpu_switch)
HX_PRIVATE(hx_os_i_coro_cpu_capture)
HX_PRIVATE(hx_os_i_coro_cpu_boot)

#if defined(__x86_64__)
HX_NAME(hx_os_i_coro_cpu_switch):
	.byte 0xf3, 0x0f, 0x1e, 0xfa /* ENDBR64; harmless on pre-CET CPUs. */
	movq %rsp, 0(%rdi)
	movq %rbx, 8(%rdi)
	movq %rbp, 16(%rdi)
	movq %r12, 24(%rdi)
	movq %r13, 32(%rdi)
	movq %r14, 40(%rdi)
	movq %r15, 48(%rdi)
	stmxcsr 128(%rdi)
	fnstcw 136(%rdi)
	ldmxcsr 128(%rsi)
	fldcw 136(%rsi)
	movq 8(%rsi), %rbx
	movq 16(%rsi), %rbp
	movq 24(%rsi), %r12
	movq 32(%rsi), %r13
	movq 40(%rsi), %r14
	movq 48(%rsi), %r15
	movq 0(%rsi), %rsp
	ret
HX_NAME(hx_os_i_coro_cpu_capture):
	.byte 0xf3, 0x0f, 0x1e, 0xfa
	stmxcsr 128(%rdi)
	fnstcw 136(%rdi)
	ret
HX_NAME(hx_os_i_coro_cpu_boot):
	.byte 0xf3, 0x0f, 0x1e, 0xfa
	movq %r12, %rdi
	call HX_NAME(hx_os_i_coro_context_entry)
#elif defined(__i386__)
HX_NAME(hx_os_i_coro_cpu_switch):
	.byte 0xf3, 0x0f, 0x1e, 0xfb /* ENDBR32. */
	movl 4(%esp), %eax
	movl 8(%esp), %edx
	movl %esp, 0(%eax)
	movl %ebx, 4(%eax)
	movl %ebp, 8(%eax)
	movl %esi, 12(%eax)
	movl %edi, 16(%eax)
	stmxcsr 64(%eax)
	fnstcw 72(%eax)
	ldmxcsr 64(%edx)
	fldcw 72(%edx)
	movl 4(%edx), %ebx
	movl 8(%edx), %ebp
	movl 12(%edx), %esi
	movl 16(%edx), %edi
	movl 0(%edx), %esp
	ret
HX_NAME(hx_os_i_coro_cpu_capture):
	.byte 0xf3, 0x0f, 0x1e, 0xfb
	movl 4(%esp), %eax
	stmxcsr 64(%eax)
	fnstcw 72(%eax)
	ret
HX_NAME(hx_os_i_coro_cpu_boot):
	.byte 0xf3, 0x0f, 0x1e, 0xfb
	subl $12, %esp
	pushl %ebx
	call HX_NAME(hx_os_i_coro_context_entry)
#elif defined(__aarch64__)
HX_NAME(hx_os_i_coro_cpu_switch):
	stp x19, x20, [x0, #0]
	stp x21, x22, [x0, #16]
	stp x23, x24, [x0, #32]
	stp x25, x26, [x0, #48]
	stp x27, x28, [x0, #64]
	stp x29, x30, [x0, #80]
	mov x9, sp
	str x9, [x0, #96]
	stp d8, d9, [x0, #128]
	stp d10, d11, [x0, #144]
	stp d12, d13, [x0, #160]
	stp d14, d15, [x0, #176]
	mrs x9, fpcr
	mrs x10, fpsr
	stp x9, x10, [x0, #192]
	ldp x9, x10, [x1, #192]
	msr fpcr, x9
	msr fpsr, x10
	ldp d8, d9, [x1, #128]
	ldp d10, d11, [x1, #144]
	ldp d12, d13, [x1, #160]
	ldp d14, d15, [x1, #176]
	ldp x19, x20, [x1, #0]
	ldp x21, x22, [x1, #16]
	ldp x23, x24, [x1, #32]
	ldp x25, x26, [x1, #48]
	ldp x27, x28, [x1, #64]
	ldp x29, x30, [x1, #80]
	ldr x9, [x1, #96]
	mov sp, x9
	ret
HX_NAME(hx_os_i_coro_cpu_capture):
	mrs x9, fpcr
	mrs x10, fpsr
	stp x9, x10, [x0, #192]
	ret
HX_NAME(hx_os_i_coro_cpu_boot):
	mov x0, x19
	bl HX_NAME(hx_os_i_coro_context_entry)
#elif defined(__arm__)
.syntax unified
.fpu vfpv3-d16
.thumb
.thumb_func
HX_NAME(hx_os_i_coro_cpu_switch):
	stmia r0, {r4-r11}
	mov r2, sp
	str r2, [r0, #32]
	str lr, [r0, #36]
	add r2, r0, #64
	vstmia r2, {d8-d15}
	vmrs r2, fpscr
	str r2, [r0, #128]
	ldr r2, [r1, #128]
	vmsr fpscr, r2
	add r2, r1, #64
	vldmia r2, {d8-d15}
	ldmia r1, {r4-r11}
	ldr r2, [r1, #32]
	mov sp, r2
	ldr lr, [r1, #36]
	bx lr
.thumb_func
HX_NAME(hx_os_i_coro_cpu_capture):
	vmrs r2, fpscr
	str r2, [r0, #128]
	bx lr
.thumb_func
HX_NAME(hx_os_i_coro_cpu_boot):
	mov r0, r4
	bl HX_NAME(hx_os_i_coro_context_entry)
#else
#error Stackful coroutines require an adapted CPU ABI
#endif

#if !defined(__APPLE__)
.section .note.GNU-stack,"",%progbits
#if defined(__x86_64__) || defined(__i386__)
/* Keep caller-selected ELF hardening. The C entry rejects active SHSTK
 * before using these context switches; no security feature is disabled. */
.section .note.gnu.property,"a",%note
#if defined(__x86_64__)
.p2align 3
#else
.p2align 2
#endif
.long 4
.long 1f - 0f
.long 5
.asciz "GNU"
#if defined(__x86_64__)
.p2align 3
#else
.p2align 2
#endif
0:
.long 0xc0000002
.long 4
.long 3
#if defined(__x86_64__)
.p2align 3
#else
.p2align 2
#endif
1:
#endif
#endif
