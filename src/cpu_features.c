#include "system_private.h"

#if HX_OS_ENABLE_SYSTEM_UTILS && !defined(_M_ARM64EC) &&                       \
	(defined(_M_IX86) || defined(_M_X64) || defined(__i386__) ||           \
		defined(__x86_64__))
#define HX_OS_I_X86_FEATURES 1
#if defined(_MSC_VER)
#include <intrin.h>
static void hx_os_cpuid(unsigned leaf, unsigned subleaf, unsigned values[4])
{
	int native[4];
	__cpuidex(native, (int)leaf, (int)subleaf);
	memcpy(values, native, sizeof(native));
}
static uint64_t hx_os_xgetbv(void)
{
	return _xgetbv(0);
}
#else
#include <cpuid.h>
#include <immintrin.h>
static void hx_os_cpuid(unsigned leaf, unsigned subleaf, unsigned values[4])
{
	__cpuid_count(
		leaf, subleaf, values[0], values[1], values[2], values[3]);
}
__attribute__((target("xsave"))) static uint64_t hx_os_xgetbv(void)
{
	return _xgetbv(0);
}
#endif
#endif

hx_os_status hx_os_cpu_features_query(uint64_t *features, hx_os_error *error)
{
#if HX_OS_ENABLE_SYSTEM_UTILS
	uint64_t value = hx_os_i_cpu_native_features();
	if (!features)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
#if defined(HX_OS_I_X86_FEATURES)
	{
		unsigned registers[4], maximum;
		hx_os_cpuid(0, 0, registers);
		maximum = registers[0];
		if (maximum >= 1) {
			hx_os_cpuid(1, 0, registers);
			if (registers[3] & (1u << 25))
				value |= HX_OS_CPU_SSE;
			if (registers[3] & (1u << 26))
				value |= HX_OS_CPU_SSE2;
			if (registers[2] & (1u << 19))
				value |= HX_OS_CPU_SSE41;
			if ((registers[2] & (7u << 26)) == (7u << 26) &&
				(hx_os_xgetbv() & 6) == 6) {
				value |= HX_OS_CPU_AVX;
				if (maximum >= 7) {
					hx_os_cpuid(7, 0, registers);
					if (registers[1] & (1u << 5))
						value |= HX_OS_CPU_AVX2;
				}
			}
		}
	}
#endif
	*features = value;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
#else
	(void)features;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

uint64_t hx_os_cpu_features(void)
{
	uint64_t features = 0;
	(void)hx_os_cpu_features_query(&features, NULL);
	return features;
}
