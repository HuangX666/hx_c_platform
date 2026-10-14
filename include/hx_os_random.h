#ifndef HX_C_PLATFORM_RANDOM_H
#define HX_C_PLATFORM_RANDOM_H

#include "hx_os_util.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \brief xoshiro256** for simulations/shuffle, never passwords/keys/nonces.
 *
 * \details The caller serializes mutable state. Seeded results are
 * reproducible. seed_system uses the OS cryptographic RNG once; next is NOT
 * cryptographic.
 */
typedef struct hx_os_rng {
	uint64_t state[4];
} hx_os_rng;

HX_OS_API void HX_OS_CALL hx_os_rng_seed(hx_os_rng *rng, uint64_t seed);
HX_OS_API hx_os_status HX_OS_CALL hx_os_rng_seed_system(
	hx_os_rng *rng, hx_os_error *error);
HX_OS_API uint64_t HX_OS_CALL hx_os_rng_next(hx_os_rng *rng);
/**
 * \brief Unbiased rejection sampling; bound must be nonzero.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_rng_bounded(
	hx_os_rng *rng, uint64_t bound, uint64_t *out, hx_os_error *error);

#ifdef __cplusplus
}
#endif
#endif
