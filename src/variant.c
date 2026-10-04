#include "internal.h"
#include "hx_variant.h"
#include "hx_container.h"
#include <assert.h>

void hx_variant_init(hx_variant *value, hx_os_context *context)
{
	assert(value);

	memset(value, 0, sizeof(*value));
	value->context = context;
	hx_os_i_context_retain(context);
}

void hx_variant_clear(hx_variant *value)
{
	hx_variant old;

	if (!value)
		return;

	old = *value;
	memset(&value->data, 0, sizeof(value->data));
	value->kind = HX_VARIANT_EMPTY;
	if (old.kind == HX_VARIANT_STRING)
		hx_string_destroy(&old.data.string);
	else if (old.kind == HX_VARIANT_CUSTOM) {
		if (old.data.custom.type->drop)
			old.data.custom.type->drop(old.data.custom.object,
				old.data.custom.type->user);

		hx_os_memory_free(old.data.custom.object);
	}
}

void hx_variant_destroy(hx_variant *value)
{
	if (!value)
		return;

	hx_variant_clear(value);

	hx_os_i_context_release(value->context);
	value->context = NULL;
}

void hx_variant_move(hx_variant *out, hx_variant *source)
{
	assert(out && source);
	if (out != source) {
		hx_variant next = *source;

		memset(source, 0, sizeof(*source));

		hx_variant_destroy(out);
		*out = next;
	}
}

void hx_variant_swap(hx_variant *left, hx_variant *right)
{
	hx_variant temporary;

	assert(left && right);

	temporary = *left;
	*left = *right;
	*right = temporary;
}

int hx_variant_kind(const hx_variant *value)
{
	return value ? (int)value->kind : -1;
}

const hx_variant_type *hx_variant_custom_type(const hx_variant *value)
{
	return value && value->kind == HX_VARIANT_CUSTOM
		? value->data.custom.type
		: NULL;
}

#if HX_OS_ENABLE_VARIANT
static int hx_variant_type_valid(const hx_variant_type *type)
{
	return type && type->size && hx_os_i_alignment_valid(type->alignment) &&
		!(type->flags & ~(uint32_t)HX_VARIANT_TYPE_NO_COPY) &&
		!(type->copy && (type->flags & HX_VARIANT_TYPE_NO_COPY));
}

static hx_os_status hx_variant_check(
	const hx_variant *value, const void *out, int kind, hx_os_error *error)
{
	if (!value || !out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	return hx_os_i_error(error,
		value->kind == (uint32_t)kind ? HX_OS_OK : HX_OS_TYPE_MISMATCH,
		0, 0);
}
#define HX_VARIANT_SCALAR(name, type, field, tag, expression)                  \
	hx_os_status hx_variant_set_##name(                                    \
		hx_variant *value, type input, hx_os_error *error)             \
	{                                                                      \
		if (!value)                                                    \
			return hx_os_i_error(error, HX_OS_INVALID, 0, 0);      \
		hx_variant_clear(value);                                       \
		value->data.field = expression;                                \
		value->kind = tag;                                             \
		return hx_os_i_error(error, HX_OS_OK, 0, 0);                   \
	}                                                                      \
	hx_os_status hx_variant_get_##name(                                    \
		const hx_variant *value, type *out, hx_os_error *error)        \
	{                                                                      \
		hx_os_status status =                                          \
			hx_variant_check(value, out, tag, error);              \
		if (status == HX_OS_OK)                                        \
			*out = value->data.field;                              \
		return status;                                                 \
	}
#else
#define HX_VARIANT_SCALAR(name, type, field, tag, expression)                  \
	hx_os_status hx_variant_set_##name(                                    \
		hx_variant *value, type input, hx_os_error *error)             \
	{                                                                      \
		(void)value;                                                   \
		(void)input;                                                   \
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);        \
	}                                                                      \
	hx_os_status hx_variant_get_##name(                                    \
		const hx_variant *value, type *out, hx_os_error *error)        \
	{                                                                      \
		(void)value;                                                   \
		(void)out;                                                     \
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);        \
	}
#endif
HX_VARIANT_SCALAR(bool, int, boolean, HX_VARIANT_BOOL, input != 0)

HX_VARIANT_SCALAR(i32, int32_t, i32, HX_VARIANT_I32, input)

HX_VARIANT_SCALAR(u32, uint32_t, u32, HX_VARIANT_U32, input)

HX_VARIANT_SCALAR(i64, int64_t, i64, HX_VARIANT_I64, input)

HX_VARIANT_SCALAR(u64, uint64_t, u64, HX_VARIANT_U64, input)

HX_VARIANT_SCALAR(float, float, real32, HX_VARIANT_FLOAT, input)

HX_VARIANT_SCALAR(double, double, real64, HX_VARIANT_DOUBLE, input)

HX_VARIANT_SCALAR(pointer, void *, pointer, HX_VARIANT_POINTER, input)
#undef HX_VARIANT_SCALAR

#if HX_OS_ENABLE_VARIANT
hx_os_status hx_variant_set_string(
	hx_variant *value, hx_string_view input, hx_os_error *error)
{
	hx_string next;
	hx_os_status status;

	if (!value)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	hx_string_init(&next, value->context);

	status = hx_string_assign(&next, input, error);
	if (status == HX_OS_OK) {
		hx_variant_clear(value);
		hx_string_move(&value->data.string, &next);
		value->kind = HX_VARIANT_STRING;
	}

	hx_string_destroy(&next);

	return status;
}

hx_os_status hx_variant_set_cstr(
	hx_variant *value, const char *input, hx_os_error *error)
{
	if (!input)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	return hx_variant_set_string(
		value, hx_string_view_from_cstr(input), error);
}

hx_os_status hx_variant_take_string(
	hx_variant *value, hx_string *source, hx_os_error *error)
{
	hx_string next = HX_STRING_INIT;

	if (!value || !source)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (value->kind == HX_VARIANT_STRING && source == &value->data.string)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);

	hx_string_move(&next, source);
	hx_variant_clear(value);
	hx_string_move(&value->data.string, &next);
	value->kind = HX_VARIANT_STRING;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_variant_get_string_view(
	const hx_variant *value, hx_string_view *out, hx_os_error *error)
{
	hx_os_status status =
		hx_variant_check(value, out, HX_VARIANT_STRING, error);

	if (status == HX_OS_OK)
		*out = hx_string_view_of(&value->data.string);

	return status;
}

hx_os_status hx_variant_get_string(
	const hx_variant *value, hx_string *out, hx_os_error *error)
{
	hx_os_status status =
		hx_variant_check(value, out, HX_VARIANT_STRING, error);

	return status == HX_OS_OK
		? hx_string_copy(out, &value->data.string, error)
		: status;
}

hx_os_status hx_variant_emplace_custom(hx_variant *value,
	const hx_variant_type *type, hx_variant_construct_fn construct,
	void *user, hx_os_error *error)
{
	void *object;
	hx_os_error detail;
	hx_os_status status;

	if (!value || !hx_variant_type_valid(type) || !construct)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (!hx_os_i_allocation_cost(type->size, type->alignment))
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);

	object = hx_os_i_alloc(value->context, type->size, type->alignment);
	if (!object)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

	hx_os_error_clear(&detail);

	status = construct(object, value->context, user, &detail);
	if (status == HX_OS_OK) {
		hx_variant_clear(value);
		value->data.custom.object = object;
		value->data.custom.type = type;
		value->kind = HX_VARIANT_CUSTOM;
	}
	else
		hx_os_memory_free(object);

	if (error) {
		*error = detail;
		error->status = status;
	}

	return status;
}

typedef struct hx_variant_copy_input {
	const hx_variant_type *type;
	const void *source;
} hx_variant_copy_input;

static hx_os_status HX_OS_CALL hx_variant_construct_copy(void *destination,
	hx_os_context *context, void *user, hx_os_error *error)
{
	hx_variant_copy_input *input = user;

	if (input->type->copy)
		return input->type->copy(destination, input->source, context,
			input->type->user, error);

	memcpy(destination, input->source, input->type->size);

	return HX_OS_OK;
}

hx_os_status hx_variant_set_custom(hx_variant *value,
	const hx_variant_type *type, const void *source, hx_os_error *error)
{
	hx_variant_copy_input input;

	if (!value || !hx_variant_type_valid(type) || !source)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if ((type->flags & HX_VARIANT_TYPE_NO_COPY) ||
		(!type->copy && type->drop))
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	input.type = type;
	input.source = source;

	return hx_variant_emplace_custom(
		value, type, hx_variant_construct_copy, &input, error);
}

hx_os_status hx_variant_get_custom(const hx_variant *value,
	const hx_variant_type *type, const void **out, hx_os_error *error)
{
	hx_os_status status;

	if (!type)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status = hx_variant_check(value, out, HX_VARIANT_CUSTOM, error);
	if (status == HX_OS_OK && value->data.custom.type != type)
		status = hx_os_i_error(error, HX_OS_TYPE_MISMATCH, 0, 0);

	if (status == HX_OS_OK)
		*out = value->data.custom.object;

	return status;
}

hx_os_status hx_variant_get_custom_mut(hx_variant *value,
	const hx_variant_type *type, void **out, hx_os_error *error)
{
	const void *object = NULL;
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status = hx_variant_get_custom(value, type, &object, error);
	if (status == HX_OS_OK)
		*out = (void *)object;

	return status;
}

hx_os_status hx_variant_copy(
	hx_variant *out, const hx_variant *source, hx_os_error *error)
{
	hx_variant_data next;
	uint32_t kind;

	if (!out || !source)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (out == source)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);

	if (source->kind == HX_VARIANT_STRING)
		return hx_variant_set_string(
			out, hx_string_view_of(&source->data.string), error);

	if (source->kind == HX_VARIANT_CUSTOM)
		return hx_variant_set_custom(out, source->data.custom.type,
			source->data.custom.object, error);

	next = source->data;
	kind = source->kind;
	hx_variant_clear(out);
	out->data = next;
	out->kind = kind;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_variant_visit(const hx_variant *value,
	hx_variant_visit_fn visit, void *user, hx_os_error *error)
{
	hx_os_error detail;
	hx_os_status status;

	if (!value || !visit)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	hx_os_error_clear(&detail);

	status = visit(value, user, &detail);
	if (error) {
		*error = detail;
		error->status = status;
	}

	return status;
}
#else
#define HX_VARIANT_DISABLED(name, arguments, unused)                           \
	hx_os_status name arguments                                            \
	{                                                                      \
		unused return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0); \
	}
HX_VARIANT_DISABLED(
	hx_variant_set_string,
	(hx_variant * value, hx_string_view input, hx_os_error *error),
	(void)value;
	(void)input;)

HX_VARIANT_DISABLED(hx_variant_set_cstr,
		    (hx_variant * value, const char *input, hx_os_error *error),
		    (void)value;
		    (void)input;)

HX_VARIANT_DISABLED(hx_variant_take_string,
		    (hx_variant * value, hx_string *source, hx_os_error *error),
		    (void)value;
		    (void)source;)

HX_VARIANT_DISABLED(
	hx_variant_get_string_view,
	(const hx_variant *value, hx_string_view *out, hx_os_error *error),
	(void)value;
	(void)out;)

HX_VARIANT_DISABLED(
	hx_variant_get_string,
	(const hx_variant *value, hx_string *out, hx_os_error *error),
	(void)value;
	(void)out;)

HX_VARIANT_DISABLED(hx_variant_set_custom,
		    (hx_variant * value, const hx_variant_type *type,
			    const void *source, hx_os_error *error),
		    (void)value;
		    (void)type; (void)source;)

HX_VARIANT_DISABLED(hx_variant_emplace_custom,
		    (hx_variant * value, const hx_variant_type *type,
			    hx_variant_construct_fn construct, void *user,
			    hx_os_error *error),
		    (void)value;
		    (void)type; (void)construct; (void)user;)

HX_VARIANT_DISABLED(hx_variant_get_custom,
		    (const hx_variant *value, const hx_variant_type *type,
			    const void **out, hx_os_error *error),
		    (void)value;
		    (void)type; (void)out;)

HX_VARIANT_DISABLED(hx_variant_get_custom_mut,
		    (hx_variant * value, const hx_variant_type *type,
			    void **out, hx_os_error *error),
		    (void)value;
		    (void)type; (void)out;)

HX_VARIANT_DISABLED(
	hx_variant_copy,
	(hx_variant * out, const hx_variant *source, hx_os_error *error),
	(void)out;
	(void)source;)

HX_VARIANT_DISABLED(hx_variant_visit,
		    (const hx_variant *value, hx_variant_visit_fn visit,
			    void *user, hx_os_error *error),
		    (void)value;
		    (void)visit; (void)user;)
#undef HX_VARIANT_DISABLED
#endif

static hx_os_status HX_OS_CALL hx_variant_element_copy(
	void *destination, const void *source, void *user)
{
	hx_os_status status;

	hx_variant_init(destination, user);

	status = hx_variant_copy(destination, source, NULL);
	if (status != HX_OS_OK)
		hx_variant_destroy(destination);

	return status;
}

static void HX_OS_CALL hx_variant_element_drop(void *value, void *user)
{
	(void)user;

	hx_variant_destroy(value);
}

void hx_variant_container_options_init(
	hx_os_container_options *options, hx_os_context *context)
{
	struct hx_variant_alignment {
		char prefix;
		hx_variant value;
	};

	if (!options)
		return;

	hx_os_container_options_init(options, sizeof(hx_variant));
	options->element_alignment =
		offsetof(struct hx_variant_alignment, value);
	options->copy = hx_variant_element_copy;
	options->drop = hx_variant_element_drop;
	options->user = context;
}
