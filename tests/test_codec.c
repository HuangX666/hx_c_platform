#include <hx_os.h>
#include "../examples/serialization_schema.h"
#include "codec_macros_fixture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(v)                                                               \
	do {                                                                   \
		if (!(v)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #v);        \
			exit(1);                                               \
		}                                                              \
	} while (0)

typedef struct flat {
	hx_string_view name;
	uint64_t serial;
	int32_t port;
	int enabled;
} flat;

static const hx_os_codec_field flat_fields[] = {
	HX_OS_CODEC_FIELD(flat, name, HX_OS_CODEC_STRING),
	HX_OS_CODEC_FIELD(flat, serial, HX_OS_CODEC_U64),
	HX_OS_CODEC_FIELD(flat, port, HX_OS_CODEC_I32),
	HX_OS_CODEC_FIELD(flat, enabled, HX_OS_CODEC_BOOL)};
static const hx_os_codec_schema flat_schema =
	HX_OS_CODEC_SCHEMA(flat, flat_fields, "connection");

typedef struct flexible {
	const hx_os_config_node *payload;
	int32_t id;
} flexible;

static const hx_os_codec_field flexible_fields[] = {
	HX_OS_CODEC_FIELD(flexible, payload, HX_OS_CODEC_NODE),
	HX_OS_CODEC_FIELD(flexible, id, HX_OS_CODEC_I32)};
static const hx_os_codec_schema flexible_schema =
	HX_OS_CODEC_SCHEMA(flexible, flexible_fields, "message");

static void test_nested(void)
{
	sample samples[] = {{{"C", 1}, 23.5, 1}, {{"%", 1}, 60, 2}};
	device value = {
		{"温度", sizeof("温度") - 1}, {samples, 2}, {-10, 80}, 1};
	const int formats[] = {HX_OS_CONFIG_JSON, HX_OS_CONFIG_YAML,
		HX_OS_CONFIG_XML, HX_OS_CONFIG_TOML};
	hx_string text = HX_STRING_INIT;
	size_t i;

	for (i = 0; i < sizeof(formats) / sizeof(formats[0]); ++i) {
		hx_os_codec_result *owner = NULL;
		const device *decoded;
		const sample *items;
		hx_os_config_error error;

		hx_os_status status = hx_os_codec_encode(&device_schema, &value,
			formats[i], &text, NULL, &error);
		if (status != HX_OS_OK)
			fprintf(stderr, "encode %s %s\n",
				hx_os_status_name(status), error.message);

		CHECK(status == HX_OS_OK);

		status = hx_os_codec_decode(NULL, &device_schema, formats[i],
			text.data, text.size, NULL, &owner, &error);
		if (status != HX_OS_OK)
			fprintf(stderr, "decode %s %s\n%s\n",
				hx_os_status_name(status), error.message,
				text.data);

		CHECK(status == HX_OS_OK);

		hx_string_clear(&text);
		decoded = hx_os_codec_data(owner);
		items = decoded->samples.data;
		CHECK(decoded->samples.count == 2 && decoded->enabled == 1);
		CHECK(decoded->name.size == value.name.size &&
			!memcmp(decoded->name.data, value.name.data,
				value.name.size));
		CHECK(items[0].channel == 1 && items[0].value == 23.5 &&
			items[1].value == 60);
		CHECK(decoded->range.low == -10 && decoded->range.high == 80);

		hx_os_codec_release(owner);
	}

	CHECK(hx_string_assign_cstr(&text, "kept", NULL) == HX_OS_OK);
	CHECK(hx_os_codec_encode(&device_schema, &value, HX_OS_CONFIG_INI,
		      &text, NULL, NULL) == HX_OS_UNREPRESENTABLE);
	CHECK(!strcmp(text.data, "kept"));

	value.samples.count = SIZE_MAX;
	CHECK(hx_os_codec_encode(&device_schema, &value, HX_OS_CONFIG_JSON,
		      &text, NULL, NULL) == HX_OS_LIMIT);
	CHECK(!strcmp(text.data, "kept"));

	hx_string_destroy(&text);
}

static void test_flat(void)
{
	flat value = {{" sensor ", 8}, UINT64_C(9007199254740993), 8080, 1};

	hx_os_codec_result *owner;
	hx_string text = HX_STRING_INIT;
	int format;

	for (format = HX_OS_CONFIG_JSON; format <= HX_OS_CONFIG_PROPERTIES;
		++format) {
		const flat *decoded;

		if (format == HX_OS_CONFIG_INI) {
			value.name.data = "sensor";
			value.name.size = 6;
		}

		CHECK(hx_os_codec_encode(&flat_schema, &value, format, &text,
			      NULL, NULL) == HX_OS_OK);
		CHECK(hx_os_codec_decode(NULL, &flat_schema, format, text.data,
			      text.size, NULL, &owner, NULL) == HX_OS_OK);

		decoded = hx_os_codec_data(owner);
		CHECK(decoded->serial == value.serial &&
			decoded->port == 8080 && decoded->enabled);
		CHECK(decoded->name.size == value.name.size);

		hx_os_codec_release(owner);
	}

	value.serial = UINT64_MAX;
	CHECK(hx_os_codec_encode(&flat_schema, &value, HX_OS_CONFIG_JSON, &text,
		      NULL, NULL) == HX_OS_OK);
	CHECK(hx_os_codec_decode(NULL, &flat_schema, HX_OS_CONFIG_JSON,
		      text.data, text.size, NULL, &owner, NULL) == HX_OS_OK);
	CHECK(((const flat *)hx_os_codec_data(owner))->serial == UINT64_MAX);

	hx_os_codec_release(owner);
	CHECK(hx_os_codec_encode(&flat_schema, &value, HX_OS_CONFIG_TOML, &text,
		      NULL, NULL) == HX_OS_UNREPRESENTABLE);
	if (hx_os_capabilities() & HX_OS_CAP_FILE_UTILS) {
		char path[96];

		(void)snprintf(path, sizeof(path), "hx-codec-%llx.xml",
			(unsigned long long)hx_os_monotonic_ns());
		CHECK(hx_os_codec_save(&flat_schema, &value, HX_OS_CONFIG_XML,
			      path, NULL, NULL) == HX_OS_OK);
		CHECK(hx_os_codec_load(NULL, &flat_schema, HX_OS_CONFIG_XML,
			      path, NULL, &owner, NULL) == HX_OS_OK);
		CHECK(((const flat *)hx_os_codec_data(owner))->serial ==
			UINT64_MAX);

		hx_os_codec_release(owner);
		CHECK(hx_os_file_remove(path, NULL) == HX_OS_OK);
	}

	hx_string_destroy(&text);
}

static void test_flexible(void)
{
	const char *json = "{\"id\":4,\"payload\":[1,\"text\",null,{"
			   "\"children\":[true,2.5]}]}";
	hx_os_codec_result *owner;
	const flexible *value;
	const hx_os_config_node *node;
	hx_string text = HX_STRING_INIT;

	CHECK(hx_os_codec_decode(NULL, &flexible_schema, HX_OS_CONFIG_JSON,
		      json, strlen(json), NULL, &owner, NULL) == HX_OS_OK);

	value = hx_os_codec_data(owner);
	CHECK(value->id == 4 &&
		hx_os_config_kind(value->payload) == HX_OS_CONFIG_ARRAY);
	CHECK(hx_os_config_pointer(value->payload, "/3/children/0", &node) ==
		HX_OS_OK);
	CHECK(hx_os_config_kind(node) == HX_OS_CONFIG_BOOL);
	CHECK(hx_os_config_kind(hx_os_config_at(value->payload, 2)) ==
		HX_OS_CONFIG_NULL);
	CHECK(hx_os_codec_encode(&flexible_schema, value, HX_OS_CONFIG_JSON,
		      &text, NULL, NULL) == HX_OS_OK);
	CHECK(strstr(text.data,
		"\"payload\":[1,\"text\",null,{\"children\":[true,2.5]}]"));

	hx_string_destroy(&text);
	hx_os_codec_release(owner);
}

static void expect_failure(const char *input, hx_os_status expected)
{
	hx_os_codec_result *owner = NULL;
	hx_os_config_error error;

	CHECK(hx_os_codec_decode(NULL, &flat_schema, HX_OS_CONFIG_JSON, input,
		      strlen(input), NULL, &owner, &error) == expected);
	CHECK(!owner && error.detail.status == expected && error.message[0]);
}

static void test_invalid(void)
{
	flat value = {{"x", 1}, 1, 1, 1};
	hx_os_codec_schema bad = flat_schema;
	hx_os_codec_field fields[4];
	hx_os_codec_result *owner = NULL;
	hx_string text = HX_STRING_INIT;

	expect_failure("{}", HX_OS_NOT_FOUND);
	expect_failure("{\"name\":\"x\",\"serial\":1,\"port\":\"80\","
		       "\"enabled\":true}",
		HX_OS_TYPE_MISMATCH);
	expect_failure(
		"{\"name\":\"x\",\"serial\":-1,\"port\":80,\"enabled\":true}",
		HX_OS_RANGE);
	expect_failure("{\"name\":\"x\",\"serial\":1,\"port\":2147483648,"
		       "\"enabled\":true}",
		HX_OS_RANGE);
	expect_failure(
		"{\"name\":null,\"serial\":1,\"port\":80,\"enabled\":true}",
		HX_OS_TYPE_MISMATCH);
	memcpy(fields, flat_fields, sizeof(fields));
	bad.fields = fields;
	fields[0].offset = sizeof(flat);
	CHECK(hx_os_codec_encode(&bad, &value, HX_OS_CONFIG_JSON, &text, NULL,
		      NULL) == HX_OS_INVALID);

	fields[0] = flat_fields[0];
	fields[1].offset = fields[0].offset;
	CHECK(hx_os_codec_encode(&bad, &value, HX_OS_CONFIG_JSON, &text, NULL,
		      NULL) == HX_OS_INVALID);

	memcpy(fields, flat_fields, sizeof(fields));
	fields[0].flags = fields[1].flags = fields[2].flags = fields[3].flags =
		HX_OS_CODEC_OPTIONAL;
	CHECK(hx_os_codec_decode(NULL, &bad, HX_OS_CONFIG_JSON, "{}", 2, NULL,
		      &owner, NULL) == HX_OS_OK);
	CHECK(((const flat *)hx_os_codec_data(owner))->port == 0);

	hx_os_codec_release(owner);
	hx_string_destroy(&text);
}

typedef struct allocation_state {
	size_t calls, fail, live;
} allocation_state;

static void *allocate(size_t bytes, void *user)
{
	allocation_state *state = user;
	void *p;

	if (++state->calls == state->fail)
		return NULL;

	p = malloc(bytes);
	if (p)
		++state->live;

	return p;
}

static void deallocate(void *memory, void *user)
{
	allocation_state *state = user;

	if (memory) {
		CHECK(state->live);

		--state->live;

		free(memory);
	}
}

static void test_failures(void)
{
	const char *json = "{\"name\":\"a\",\"samples\":[{\"unit\":\"x\","
			   "\"channel\":1,\"value\":1.5}],\"range\":{\"low\":0,"
			   "\"high\":10},\"enabled\":true}";
	allocation_state state = {0, 0, 0};
	hx_os_context_options options;
	hx_os_context *context = NULL;
	size_t i;
	int success = 0;

	hx_os_context_options_init(&options);
	options.allocator.allocate = allocate;
	options.allocator.deallocate = deallocate;
	options.allocator.user = &state;
	CHECK(hx_os_context_create(&options, &context, NULL) == HX_OS_OK);

	for (i = 1; i < 256; ++i) {
		hx_os_codec_result *result = NULL;
		hx_os_status status;
		size_t baseline = state.live;

		state.fail = state.calls + i;

		status = hx_os_codec_decode(context, &device_schema,
			HX_OS_CONFIG_JSON, json, strlen(json), NULL, &result,
			NULL);
		CHECK(status == HX_OS_OK || status == HX_OS_NO_MEMORY);
		if (status == HX_OS_OK)
			success = 1;
		else
			CHECK(!result);

		hx_os_codec_release(result);
		CHECK(state.live == baseline);
		if (success)
			break;
	}

	CHECK(success);

	state.fail = 0;
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK && !state.live);
}

static void test_limits(void)
{
	flat value = {{"x", 1}, 1, 80, 1};
	hx_os_config_options options;
	hx_string text = HX_STRING_INIT;
	int i;

	CHECK(hx_string_assign_cstr(&text, "kept", NULL) == HX_OS_OK);

	for (i = 0; i < 5; ++i) {
		hx_os_config_options_init(&options);
		if (i == 0)
			options.max_input_bytes = 1;

		if (i == 1)
			options.max_memory_bytes = 1;

		if (i == 2)
			options.max_nodes = 1;

		if (i == 3)
			options.max_depth = 1;

		if (i == 4)
			options.max_work = 1;

		CHECK(hx_os_codec_encode(&flat_schema, &value,
			      HX_OS_CONFIG_JSON, &text, &options,
			      NULL) == HX_OS_LIMIT);
		CHECK(!strcmp(text.data, "kept"));
	}

	hx_string_destroy(&text);
}

static void test_arrays_attributes_and_widths(void)
{
	typedef struct record {
		double precise;
		float ratio;
		uint32_t counter;
		uint16_t port;
		int16_t delta;
		int8_t tiny;
		uint8_t flag;
		int32_t fixed[2];
		char name[32];
	} record;

	static const hx_os_codec_type integer_type =
		HX_OS_CODEC_TYPE(int32_t, HX_OS_CODEC_I32);
	static const hx_os_codec_field fields[] = {
		HX_OS_CODEC_FIELD(record, precise, HX_OS_CODEC_DOUBLE),
		HX_OS_CODEC_FIELD(record, ratio, HX_OS_CODEC_FLOAT),
		HX_OS_CODEC_FIELD(record, counter, HX_OS_CODEC_U32),
		HX_OS_CODEC_FIELD(record, port, HX_OS_CODEC_U16),
		HX_OS_CODEC_FIELD(record, delta, HX_OS_CODEC_I16),
		HX_OS_CODEC_FIELD(record, tiny, HX_OS_CODEC_I8),
		HX_OS_CODEC_FIELD(record, flag, HX_OS_CODEC_U8),
		HX_OS_CODEC_FIXED_FIELD(record, fixed, &integer_type),
		{"name",
			{NULL, NULL, sizeof(((record *)0)->name), 0,
				HX_OS_CODEC_BUFFER},
			offsetof(record, name), HX_OS_CODEC_XML_ATTRIBUTE}};
	static const hx_os_codec_schema schema =
		HX_OS_CODEC_SCHEMA(record, fields, "record");
	record value = {1.25, 0.5f, UINT32_MAX, UINT16_MAX, INT16_MIN, INT8_MIN,
		UINT8_MAX, {INT32_MIN, INT32_MAX}, "a\n\r\t\"&<>"};
	hx_os_codec_result *owner = NULL;
	hx_string text = HX_STRING_INIT;
	const record *back;

	CHECK(hx_os_codec_encode(&schema, &value, HX_OS_CONFIG_XML, &text, NULL,
		      NULL) == HX_OS_OK);
	CHECK(strstr(text.data, "name=\"a&#10;&#13;&#9;&quot;&amp;&lt;&gt;\""));
	CHECK(hx_os_codec_decode(NULL, &schema, HX_OS_CONFIG_XML, text.data,
		      text.size, NULL, &owner, NULL) == HX_OS_OK);

	back = hx_os_codec_data(owner);
	CHECK(back->tiny == INT8_MIN && back->delta == INT16_MIN &&
		back->port == UINT16_MAX && back->flag == UINT8_MAX);
	CHECK(back->counter == UINT32_MAX && back->precise == 1.25 &&
		back->ratio == 0.5f);
	CHECK(back->fixed[0] == INT32_MIN && back->fixed[1] == INT32_MAX &&
		!strcmp(back->name, value.name));

	hx_os_codec_release(owner);
	owner = NULL;
	CHECK(hx_os_codec_encode(&schema, &value, HX_OS_CONFIG_JSON, &text,
		      NULL, NULL) == HX_OS_OK);
	CHECK(hx_os_codec_decode(NULL, &schema, HX_OS_CONFIG_JSON, text.data,
		      text.size, NULL, &owner, NULL) == HX_OS_OK);

	hx_os_codec_release(owner);
	owner = NULL;
	/* The schema, not an unchecked cast, enforces narrowing and array
	 * length. */
	{
		const char *bad = "{\"precise\":1,\"ratio\":0.5,\"counter\":1,"
				  "\"port\":65536,\"delta\":0,\"tiny\":0,"
				  "\"flag\":0,\"fixed\":[1,2],\"name\":\"x\"}";

		CHECK(hx_os_codec_decode(NULL, &schema, HX_OS_CONFIG_JSON, bad,
			      strlen(bad), NULL, &owner, NULL) == HX_OS_RANGE &&
			!owner);

		bad = "{\"precise\":1,\"ratio\":0.5,\"counter\":1,\"port\":1,"
		      "\"delta\":0,\"tiny\":0,\"flag\":0,\"fixed\":[1],"
		      "\"name\":\"x\"}";
		CHECK(hx_os_codec_decode(NULL, &schema, HX_OS_CONFIG_JSON, bad,
			      strlen(bad), NULL, &owner, NULL) == HX_OS_RANGE &&
			!owner);
	}

	hx_string_destroy(&text);
}

static void test_macro_schema(void)
{
	typedef struct manual_connection {
		hx_string_view host;
		int32_t socket_port;
		hx_string_view note;
	} manual_connection;

	hx_os_codec_result *owner = NULL;
	macro_connection value = {{"local", 5}, 8080, {"ready", 5}};
	hx_string text = HX_STRING_INIT;
	const char *missing = "{\"server_host\":\"local\"}";
	int format;

	CHECK(sizeof(macro_connection) == sizeof(manual_connection));
	CHECK(macro_connection_fields[0].offset ==
		offsetof(manual_connection, host));
	CHECK(macro_connection_fields[1].offset ==
		offsetof(manual_connection, socket_port));
	CHECK(macro_connection_fields[2].offset ==
		offsetof(manual_connection, note));
	CHECK(macro_connection_fields[1].flags ==
		(HX_OS_CODEC_OPTIONAL | HX_OS_CODEC_XML_ATTRIBUTE));
	CHECK(macro_one_schema.count == 1 && macro_many_schema.count == 32);
	CHECK(macro_many_fields[31].offset == offsetof(macro_many, field_32));
	CHECK(!strcmp(macro_many_fields[31].name, "field_32"));

	for (format = HX_OS_CONFIG_JSON; format <= HX_OS_CONFIG_PROPERTIES;
		++format) {
		const macro_connection *copy;

		CHECK(hx_os_codec_encode(&macro_connection_schema, &value,
			      format, &text, NULL, NULL) == HX_OS_OK);
		CHECK(hx_os_codec_decode(NULL, &macro_connection_schema, format,
			      text.data, text.size, NULL, &owner,
			      NULL) == HX_OS_OK);

		copy = hx_os_codec_data(owner);
		CHECK(copy->socket_port == 8080 && copy->host.size == 5 &&
			copy->note.size == 5);
		CHECK(!memcmp(copy->host.data, "local", 5));

		hx_os_codec_release(owner);
		owner = NULL;
	}

	CHECK(hx_os_codec_decode(NULL, &macro_connection_schema,
		      HX_OS_CONFIG_JSON, missing, strlen(missing), NULL, &owner,
		      NULL) == HX_OS_OK);
	CHECK(((const macro_connection *)hx_os_codec_data(owner))
			->socket_port == 0);
	CHECK(((const macro_connection *)hx_os_codec_data(owner))->note.size ==
		0);

	hx_os_codec_release(owner);
	{
		macro_many many = {0};

		many.field_01 = 10;
		many.field_32 = 32;
		CHECK(hx_os_codec_encode(&macro_many_schema, &many,
			      HX_OS_CONFIG_JSON, &text, NULL,
			      NULL) == HX_OS_OK);
		CHECK(hx_os_codec_decode(NULL, &macro_many_schema,
			      HX_OS_CONFIG_JSON, text.data, text.size, NULL,
			      &owner, NULL) == HX_OS_OK);
		CHECK(((const macro_many *)hx_os_codec_data(owner))->field_32 ==
			32);

		hx_os_codec_release(owner);
	}

	{
		macro_scalars value_all = {INT8_MIN, UINT8_MAX, INT16_MIN,
			UINT16_MAX, INT32_MIN, UINT32_MAX, INT64_MIN,
			UINT64_MAX, 0.5f, 1.25, 1};
		const macro_scalars *copy;

		CHECK(hx_os_codec_encode(&macro_scalars_schema, &value_all,
			      HX_OS_CONFIG_JSON, &text, NULL,
			      NULL) == HX_OS_OK);
		CHECK(hx_os_codec_decode(NULL, &macro_scalars_schema,
			      HX_OS_CONFIG_JSON, text.data, text.size, NULL,
			      &owner, NULL) == HX_OS_OK);

		copy = hx_os_codec_data(owner);
		CHECK(copy->s8 == INT8_MIN && copy->u8 == UINT8_MAX &&
			copy->s16 == INT16_MIN);
		CHECK(copy->u16 == UINT16_MAX && copy->s32 == INT32_MIN &&
			copy->u32 == UINT32_MAX);
		CHECK(copy->s64 == INT64_MIN && copy->u64 == UINT64_MAX &&
			copy->flag);
		CHECK(copy->real32 == 0.5f && copy->real64 == 1.25);

		hx_os_codec_release(owner);
	}

	hx_string_destroy(&text);
}

static void test_macro_containers(void)
{
	const char *json =
		"{\"values\":[-1,42],\"switches\":[true,false],"
		"\"texts\":[\"a\",\"b\"],\"objects\":[{\"server_host\":"
		"\"first\"}],"
		"\"points\":[1.5,2.5,3.5],\"connections\":[{\"server_host\":"
		"\"one\"},"
		"{\"server_host\":\"two\"}],\"label\":\"sensor\","
		"\"current\":{\"server_host\":\"current\",\"port\":80}}";
	const char *mixed =
		"{\"payload\":{\"mixed\":[1,null,\"x\"]},\"parts\":[true,2.5]}";
	hx_os_codec_result *owner = NULL;
	const macro_container *copy;
	hx_string text = HX_STRING_INIT;

	CHECK(hx_os_codec_decode(NULL, &macro_container_schema,
		      HX_OS_CONFIG_JSON, json, strlen(json), NULL, &owner,
		      NULL) == HX_OS_OK);

	copy = hx_os_codec_data(owner);
	CHECK(copy->values.count == 2 &&
		((const int32_t *)copy->values.data)[1] == 42);
	CHECK(copy->switches.count == 2 &&
		((const hx_codec_bool *)copy->switches.data)[0] == 1);
	CHECK(copy->texts.count == 2 &&
		((const hx_string_view *)copy->texts.data)[1].size == 1);
	CHECK(copy->objects.count == 1 &&
		((const macro_connection *)copy->objects.data)[0].host.size ==
			5);
	CHECK(copy->points[2] == 3.5 && copy->connections[1].host.size == 3);
	CHECK(!strcmp(copy->label, "sensor") &&
		copy->current.socket_port == 80);
	CHECK(hx_os_codec_encode(&macro_container_schema, copy,
		      HX_OS_CONFIG_XML, &text, NULL, NULL) == HX_OS_OK);
	CHECK(strstr(text.data, "<current port=\"80\">") != NULL);

	hx_os_codec_release(owner);
	owner = NULL;
	CHECK(hx_os_codec_decode(NULL, &macro_container_schema,
		      HX_OS_CONFIG_XML, text.data, text.size, NULL, &owner,
		      NULL) == HX_OS_OK);
	CHECK(((const macro_container *)hx_os_codec_data(owner))
			->values.count == 2);

	hx_os_codec_release(owner);
	owner = NULL;
	CHECK(hx_os_codec_decode(NULL, &macro_nodes_schema, HX_OS_CONFIG_JSON,
		      mixed, strlen(mixed), NULL, &owner, NULL) == HX_OS_OK);
	CHECK(((const macro_nodes *)hx_os_codec_data(owner))->parts.count == 2);
	CHECK(hx_os_codec_encode(&macro_nodes_schema, hx_os_codec_data(owner),
		      HX_OS_CONFIG_JSON, &text, NULL, NULL) == HX_OS_OK);
	CHECK(!strcmp(text.data, mixed));

	hx_os_codec_release(owner);
	hx_string_destroy(&text);
}

int main(void)
{
	if (!hx_os_codec_available()) {
		hx_os_codec_result *out = NULL;

		CHECK(!(hx_os_capabilities() & HX_OS_CAP_CODEC));
		CHECK(hx_os_codec_decode(NULL, NULL, 0, NULL, 0, NULL, &out,
			      NULL) == HX_OS_NOT_SUPPORTED);
		CHECK(hx_os_codec_encode(NULL, NULL, 0, NULL, NULL, NULL) ==
			HX_OS_NOT_SUPPORTED);

		return 0;
	}

	CHECK(hx_os_capabilities() & HX_OS_CAP_CODEC);

	test_macro_schema();
	test_macro_containers();
	test_arrays_attributes_and_widths();
	test_nested();
	test_flat();
	test_flexible();
	test_invalid();
	test_limits();
	test_failures();
	puts("schema codec ownership, formats and allocation failures passed");

	return 0;
}
