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
	hx_os_config_slice input;
	mxml_options_t *options;
	mxml_node_t *container = NULL, *result = NULL;
	/*
	 * Shared validation/materialization guarantees XML character rules,
	 * attribute normalization and lossless mixed content on both backends.
	 */
	if (!hx_os_cfg_xml(parser, out))
		return 0;
	input = hx_os_cfg_copy(parser, parser->text, parser->length);
	if (!input.data)
		return 0;
	previous = hx_os_cfg_vendor_enter(parser);
	options = mxmlOptionsNew();
	if (options) {
		mxmlOptionsSetErrorCallback(
			options, hx_os_cfg_minixml_error, parser);
		mxmlOptionsSetTypeValue(options, MXML_TYPE_OPAQUE);
		container = mxmlNewElement(NULL, "hx_document");
		if (container)
			result = mxmlLoadString(container, options, input.data);
	}
	if (!result && parser->status == HX_OS_OK)
		(void)hx_os_cfg_fail(parser, HX_OS_SYNTAX, SIZE_MAX,
			"Mini-XML rejected the document");
	mxmlDelete(container);
	mxmlOptionsDelete(options);
	hx_os_cfg_vendor_leave(parser, previous);
	return parser->status == HX_OS_OK;
}
