#ifndef HX_C_PLATFORM_VARIANT_H
#define HX_C_PLATFORM_VARIANT_H

#include "hx_string.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
	HX_VARIANT_EMPTY,
	HX_VARIANT_BOOL,
	HX_VARIANT_I32,
	HX_VARIANT_U32,
	HX_VARIANT_I64,
	HX_VARIANT_U64,
	HX_VARIANT_FLOAT,
	HX_VARIANT_DOUBLE,
	HX_VARIANT_STRING,
	HX_VARIANT_POINTER,
	HX_VARIANT_CUSTOM
};

/**
 * \brief Construct into fresh, aligned storage.
 *
 * \details Failure must leave no owned resources in destination. Callbacks
 * return normally and cannot mutate participating variants or their owning
 * container. drop must not fail. They are synchronous.
 */
typedef hx_os_status(HX_OS_CALL *hx_variant_copy_fn)(void *destination,
	const void *source, hx_os_context *context, void *user,
	hx_os_error *error);

typedef void(HX_OS_CALL *hx_variant_drop_fn)(void *object, void *user);

typedef hx_os_status(HX_OS_CALL *hx_variant_construct_fn)(void *destination,
	hx_os_context *context, void *user, hx_os_error *error);
enum { HX_VARIANT_TYPE_NO_COPY = 1 };

/**
 * \brief Fixed layout.
 *
 * \details Identity is this descriptor's address, not its contents/name. Keep
 * the descriptor immutable, and its user/code/module alive until all associated
 * objects are destroyed. Define a shared descriptor once when values cross
 * translation units or modules. alignment=0 selects the default. NULL copy/drop
 * together mean a plain byte copy of a fixed-size C value. NULL copy with
 * non-NULL drop, or NO_COPY, allows emplace but rejects copying. Copy/construct
 * receive the destination context for nested allocations.
 */
typedef struct hx_variant_type {
	size_t size;
	size_t alignment;
	hx_variant_copy_fn copy;
	hx_variant_drop_fn drop;
	void *user;
	uint32_t flags;
} hx_variant_type;

/**
 * \brief Define once at file scope, with a named, complete byte-copyable C
 * type.
 *
 * \details This probe intentionally puts a byte before the value to measure
 * alignment. No ownership/self-pointer fixup is supplied by the trivial
 * declaration.
 */
#define HX_VARIANT_DEFINE_TRIVIAL_TYPE(name, type)                             \
	typedef struct name##_alignment {                                      \
		char prefix;                                                   \
		type value;                                                    \
	} name##_alignment;                                                    \
	const hx_variant_type name = {sizeof(type),                            \
		offsetof(name##_alignment, value), NULL, NULL, NULL, 0}

typedef union hx_variant_data {
	int64_t i64;
	uint64_t u64;
	int32_t i32;
	uint32_t u32;
	int boolean;
	float real32;
	double real64;
	hx_string string;
	void *pointer;

	struct {
		void *object;
		const hx_variant_type *type;
	} custom;
} hx_variant_data;

/**
 * \brief Fixed-layout owner, byte-relocatable but not independently copyable by
 * assignment.
 *
 * \details Fields are storage; use the APIs. Initialize once, then destroy.
 * Scalar values are inline. Custom objects are separately allocated/aligned, so
 * container relocation never relocates their contents. A context lease is
 * retained even while empty. Exclude concurrent mutation/destruction.
 */
typedef struct hx_variant {
	hx_variant_data data;
	hx_os_context *context;
	uint32_t kind;
} hx_variant;
#define HX_VARIANT_INIT                                                        \
	{                                                                      \
		{0}, NULL, HX_VARIANT_EMPTY                                    \
	}

HX_OS_API void HX_OS_CALL hx_variant_init(
	hx_variant *value, hx_os_context *context);

/**
 * \brief Clear preserves the context.
 *
 * \details Destroy resets to default empty; repeated destroy is valid.
 * Move/swap transfer contexts with values and allocate nothing; move resets the
 * source to default empty. NULL clear/destroy are OK. No ownership cycles: an
 * outer owner cannot move into its own child. All other arguments require live
 * initialized storage; exclude concurrent use.
 */
HX_OS_API void HX_OS_CALL hx_variant_clear(hx_variant *value);

HX_OS_API void HX_OS_CALL hx_variant_destroy(hx_variant *value);

HX_OS_API void HX_OS_CALL hx_variant_move(hx_variant *out, hx_variant *source);

HX_OS_API void HX_OS_CALL hx_variant_swap(hx_variant *left, hx_variant *right);

HX_OS_API int HX_OS_CALL hx_variant_kind(const hx_variant *value);

HX_OS_API const hx_variant_type *HX_OS_CALL hx_variant_custom_type(
	const hx_variant *value);

/**
 * \brief Deep copy into out's context.
 *
 * \details All failing setters/copies preserve out. Raw pointers are borrowed,
 * never freed or extended by copying.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_variant_copy(
	hx_variant *out, const hx_variant *source, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_variant_set_bool(
	hx_variant *value, int input, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_variant_get_bool(
	const hx_variant *value, int *out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_variant_set_i32(
	hx_variant *value, int32_t input, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_variant_get_i32(
	const hx_variant *value, int32_t *out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_variant_set_u32(
	hx_variant *value, uint32_t input, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_variant_get_u32(
	const hx_variant *value, uint32_t *out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_variant_set_i64(
	hx_variant *value, int64_t input, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_variant_get_i64(
	const hx_variant *value, int64_t *out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_variant_set_u64(
	hx_variant *value, uint64_t input, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_variant_get_u64(
	const hx_variant *value, uint64_t *out, hx_os_error *error);

/**
 * \brief Float/double retain their native values, including NaN/infinity.
 *
 * \details No casts.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_variant_set_float(
	hx_variant *value, float input, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_variant_get_float(
	const hx_variant *value, float *out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_variant_set_double(
	hx_variant *value, double input, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_variant_get_double(
	const hx_variant *value, double *out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_variant_set_pointer(
	hx_variant *value, void *input, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_variant_get_pointer(
	const hx_variant *value, void **out, hx_os_error *error);

/**
 * \brief Strict UTF-8, byte length and embedded NUL supported.
 *
 * \details Input is call-borrowed. take_string transfers ownership only on
 * success, retaining its originating allocator. The variant's context remains
 * the default for future copies.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_variant_set_string(
	hx_variant *value, hx_string_view input, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_variant_set_cstr(
	hx_variant *value, const char *input, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_variant_take_string(
	hx_variant *value, hx_string *source, hx_os_error *error);

/**
 * \brief view borrows until mutation/clear/destroy.
 *
 * \details get_string copies into out's context. All failed getters preserve
 * their output.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_variant_get_string_view(
	const hx_variant *value, hx_string_view *out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_variant_get_string(
	const hx_variant *value, hx_string *out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_variant_set_custom(hx_variant *value,
	const hx_variant_type *type, const void *source, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_variant_emplace_custom(hx_variant *value,
	const hx_variant_type *type, hx_variant_construct_fn construct,
	void *user, hx_os_error *error);

/**
 * \brief Exact descriptor identity, no structural match.
 *
 * \details Objects borrow the variant; no raw free/destruction. Their address
 * survives byte relocation of the variant, but not its replacement/removal.
 * Mutable access is host-serialized.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_variant_get_custom(const hx_variant *value,
	const hx_variant_type *type, const void **out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_variant_get_custom_mut(hx_variant *value,
	const hx_variant_type *type, void **out, hx_os_error *error);

/**
 * \brief Synchronous visit of one stable value.
 *
 * \details The callback receives a cleared non-NULL error record; any returned
 * status/native details are propagated. No hidden allocation or callback
 * storage; do not mutate value in callback.
 */
typedef hx_os_status(HX_OS_CALL *hx_variant_visit_fn)(
	const hx_variant *value, void *user, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_variant_visit(const hx_variant *value,
	hx_variant_visit_fn visit, void *user, hx_os_error *error);

struct hx_os_container_options;

/**
 * \brief Owned-element preset for vector/queue/map etc.
 *
 * \details context is borrowed for the container lifetime. Nested value
 * allocations are outside max_bytes, as with other container callbacks. Do not
 * use the POD container macros for owners.
 */
HX_OS_API void HX_OS_CALL hx_variant_container_options_init(
	struct hx_os_container_options *options, hx_os_context *context);

#ifdef __cplusplus
}
#endif
#endif
