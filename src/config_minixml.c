#include "config_private.h"
#include <mxml.h>

static void hx_os_cfg_minixml_error(void *user, const char *message)
{
	hx_os_cfg_parser *parser = user;

	(void)message;
	(void)hx_os_cfg_fail(parser, HX_OS_SYNTAX, SIZE_MAX,
		"Mini-XML rejected the document");
}

int hx_os_cfg_minixml(hx_os_cfg_parser *parser, hx_os_config_node **out)
{
	hx_os_cfg_parser *previous;
	char *input, *comment;
	mxml_options_t *options;
	mxml_node_t *container = NULL, *result = NULL;

	// Shared validation/materialization guarantees XML character rules,
	// attribute normalization and lossless mixed content on both backends.
	if (!hx_os_cfg_xml(parser, out))
		return 0;

	previous = hx_os_cfg_vendor_enter(parser);
	input = hx_os_cfg_vendor_malloc(parser->length + 1);
	if (!input) {
		hx_os_cfg_vendor_leave(parser, previous);
		return 0;
	}
	memcpy(input, parser->text, parser->length);
	input[parser->length] = 0;
	// Mini-XML rejects empty comments. The shared tree above owns all
	// text, including CDATA; this copy is only for native validation.
	for (comment = input; (comment = strstr(comment, "<!---->")) != NULL;
		comment += 7)
		memset(comment, ' ', 7);
	options = mxmlOptionsNew();
	if (options) {
		mxmlOptionsSetErrorCallback(
			options, hx_os_cfg_minixml_error, parser);
		mxmlOptionsSetTypeValue(options, MXML_TYPE_OPAQUE);
		container = mxmlNewElement(NULL, "hx_document");
		if (container)
			result = mxmlLoadString(container, options, input);
	}

	if (!result && parser->status == HX_OS_OK)
		(void)hx_os_cfg_fail(parser, HX_OS_SYNTAX, SIZE_MAX,
			"Mini-XML rejected the document");

	mxmlDelete(container);
	mxmlOptionsDelete(options);
	hx_os_cfg_vendor_leave(parser, previous);

	return parser->status == HX_OS_OK;
}
