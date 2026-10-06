#ifndef HX_C_PLATFORM_CONFIGURATION_PRIVATE_H
#define HX_C_PLATFORM_CONFIGURATION_PRIVATE_H

#include "internal.h"
#include "hx_config.h"

typedef struct hx_os_cfg_block hx_os_cfg_block;

struct hx_os_config_node {
	hx_os_config_document *document;
	hx_os_config_slice name;
	hx_os_config_slice value;
	struct hx_os_config_node *first;
	struct hx_os_config_node *last;
	struct hx_os_config_node *next;
	struct hx_os_config_node *attributes;
	size_t count;
	size_t depth;
	int kind;
	int flags;
};

struct hx_os_config_document {
	hx_os_atomic references;
	hx_os_context *context;
	hx_os_config_options options;
	hx_os_config_node *root;
	hx_os_cfg_block *blocks;
	size_t memory;
	size_t nodes;
	size_t work;
};

typedef struct hx_os_cfg_parser {
	hx_os_config_document *document;
	const char *text;
	size_t length;
	size_t position;
	int format;
	hx_os_status status;
	hx_os_config_error *error;
	void *vendor_blocks;
} hx_os_cfg_parser;

void hx_os_cfg_error_init(hx_os_config_error *error, const char *source);

hx_os_config_node *hx_os_cfg_clone(hx_os_cfg_parser *parser,
	const hx_os_config_node *source, size_t depth);

hx_os_config_node *hx_os_cfg_build_value(hx_os_cfg_parser *parser,
	const hx_os_config_value *value, size_t depth);

hx_os_status hx_os_cfg_ini_validate(const hx_os_config_document *document);

/* Requires validated options and an empty parser. Publishes no public tree. */
int hx_os_cfg_document_create(hx_os_cfg_parser *parser, hx_os_context *context,
	const hx_os_config_options *options);

int hx_os_cfg_options(hx_os_cfg_parser *parser,
	const hx_os_config_options *input, hx_os_config_options *options);

enum {
	HX_OS_CFG_JSON_STRING,
	HX_OS_CFG_YAML_STRING,
	HX_OS_CFG_TOML_STRING,
	HX_OS_CFG_ENV_STRING,
	HX_OS_CFG_PROPERTY_STRING
};

hx_os_status hx_os_cfg_fail(hx_os_cfg_parser *parser, hx_os_status status,
	size_t position, const char *message);

int hx_os_cfg_reject(
	hx_os_cfg_parser *parser, hx_os_status status, const char *message);

int hx_os_cfg_work(hx_os_cfg_parser *parser, size_t amount);

void *hx_os_cfg_alloc(hx_os_cfg_parser *parser, size_t bytes);

hx_os_config_slice hx_os_cfg_copy(
	hx_os_cfg_parser *parser, const char *data, size_t bytes);

hx_os_config_node *hx_os_cfg_node(
	hx_os_cfg_parser *parser, int kind, size_t depth);

hx_os_config_node *hx_os_cfg_lookup(hx_os_cfg_parser *parser,
	hx_os_config_node *object, hx_os_config_slice name);

int hx_os_cfg_append(hx_os_cfg_parser *parser, hx_os_config_node *parent,
	hx_os_config_node *child, int duplicates);

int hx_os_cfg_space(unsigned char character);

void hx_os_cfg_skip(hx_os_cfg_parser *parser, int newlines, int comments);

int hx_os_cfg_equal(hx_os_config_slice value, const char *literal);

hx_os_config_slice hx_os_cfg_trim(hx_os_config_slice value);

int hx_os_cfg_quoted(
	hx_os_cfg_parser *parser, int mode, hx_os_config_slice *out);

int hx_os_cfg_unescape(hx_os_cfg_parser *parser, const char *text, size_t bytes,
	int mode, char quote, hx_os_config_slice *out);

int hx_os_cfg_number(const char *text, size_t bytes, int toml, int *kind);

int hx_os_cfg_scalar(hx_os_cfg_parser *parser, hx_os_config_slice text,
	int mode, size_t depth, hx_os_config_node **out);

hx_os_status hx_os_cfg_integer(
	hx_os_config_slice value, uint64_t *magnitude, int *negative);

int hx_os_cfg_json(hx_os_cfg_parser *parser, hx_os_config_node **out);

int hx_os_cfg_yaml(hx_os_cfg_parser *parser, hx_os_config_node **out);

int hx_os_cfg_xml(hx_os_cfg_parser *parser, hx_os_config_node **out);

int hx_os_cfg_xml_valid(hx_os_config_slice text, int name);

int hx_os_cfg_lines(hx_os_cfg_parser *parser, hx_os_config_node **out);

int hx_os_cfg_toml(hx_os_cfg_parser *parser, hx_os_config_node **out);

int hx_os_cfg_cjson(hx_os_cfg_parser *parser, hx_os_config_node **out);

int hx_os_cfg_libyaml(hx_os_cfg_parser *parser, hx_os_config_node **out);

int hx_os_cfg_minixml(hx_os_cfg_parser *parser, hx_os_config_node **out);

int hx_os_cfg_inih(hx_os_cfg_parser *parser, hx_os_config_node **out);

int hx_os_cfg_tomlc(hx_os_cfg_parser *parser, hx_os_config_node **out);

hx_os_cfg_parser *hx_os_cfg_vendor_enter(hx_os_cfg_parser *parser);

void hx_os_cfg_vendor_leave(
	hx_os_cfg_parser *parser, hx_os_cfg_parser *previous);

void *hx_os_cfg_vendor_malloc(size_t bytes);

void *hx_os_cfg_vendor_calloc(size_t count, size_t bytes);

void *hx_os_cfg_vendor_realloc(void *memory, size_t bytes);

void hx_os_cfg_vendor_free(void *memory);

char *hx_os_cfg_vendor_strdup(const char *text);

double hx_os_cfg_vendor_strtod(const char *text, char **end);

int hx_os_cfg_vendor_isspace(int character);

#endif
