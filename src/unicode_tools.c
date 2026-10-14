#include "internal.h"
#include "hx_unicode.h"

#if HX_OS_HAVE_UNICODE
#include <utf8proc.h>
#endif

int hx_os_unicode_available(void)
{
	return HX_OS_HAVE_UNICODE;
}

static hx_os_status hx_os_unicode_convert(hx_string_view input, unsigned form,
	int fold, hx_string *out, hx_os_error *error)
{
#if HX_OS_HAVE_UNICODE
	utf8proc_option_t flags = UTF8PROC_STABLE;
	utf8proc_ssize_t count, bytes;
	utf8proc_int32_t *scratch;
	hx_os_status status;

	if (!out || (!input.data && input.size) || form < HX_OS_UNICODE_NFC ||
		form > HX_OS_UNICODE_NFKD)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (input.size > 1024 * 1024)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	flags |= form == HX_OS_UNICODE_NFC || form == HX_OS_UNICODE_NFKC
		? UTF8PROC_COMPOSE
		: UTF8PROC_DECOMPOSE;
	if (form == HX_OS_UNICODE_NFKC || form == HX_OS_UNICODE_NFKD)
		flags |= UTF8PROC_COMPAT;
	if (fold)
		flags |= UTF8PROC_CASEFOLD;

	count = utf8proc_decompose((const utf8proc_uint8_t *)input.data,
		(utf8proc_ssize_t)input.size, NULL, 0, flags);
	if (count < 0)
		return hx_os_i_error(error, HX_OS_INVALID_ENCODING, 0, 0);
	if ((uint64_t)count >= 16 * 1024 * 1024 / sizeof(*scratch))
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	scratch = hx_os_i_alloc(
		out->context, ((size_t)count + 1) * sizeof(*scratch), 0);
	if (!scratch)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	bytes = utf8proc_decompose((const utf8proc_uint8_t *)input.data,
		(utf8proc_ssize_t)input.size, scratch, count, flags);
	if (bytes >= 0)
		bytes = utf8proc_reencode(scratch, bytes, flags);
	if (bytes < 0)
		status = hx_os_i_error(error, HX_OS_INVALID_ENCODING, 0, 0);
	else {
		hx_string_view result = {(const char *)scratch, (size_t)bytes};

		status = hx_string_assign(out, result, error);
	}
	hx_os_memory_free(scratch);

	return status;
#else
	(void)input;
	(void)form;
	(void)fold;
	(void)out;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_unicode_normalize(
	hx_string_view input, unsigned form, hx_string *out, hx_os_error *error)
{
	return hx_os_unicode_convert(input, form, 0, out, error);
}

hx_os_status hx_os_unicode_casefold(
	hx_string_view input, hx_string *out, hx_os_error *error)
{
	return hx_os_unicode_convert(input, HX_OS_UNICODE_NFC, 1, out, error);
}
