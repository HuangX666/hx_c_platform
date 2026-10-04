#include <hx/Codec.hpp>
#include <hx/Config.hpp>
#include "../examples/serialization_schema.h"
#include <cstdio>
#include <cstdlib>
#define CHECK(v)                                                               \
	do {                                                                   \
		if (!(v)) {                                                    \
			std::fprintf(stderr, "line %d: %s\n", __LINE__, #v);   \
			std::exit(1);                                          \
		}                                                              \
	} while (0)
int main()
{
	if (!hx_os_codec_available())
		return 0;
	hx::Codec<device> codec(device_schema);
	auto result =
		codec.decode("{\"name\":\"sensor\",\"samples\":[{\"unit\":"
			     "\"C\",\"value\":23.5,\"channel\":1}],\"range\":{"
			     "\"low\":-10.0,\"high\":80.0},\"enabled\":true}");
	CHECK(result);
	auto first =
		result.value().arrayItem<sample>(result.value()->samples, 0);
	CHECK(first && first.value().value == 23.5);
	auto moved = std::move(result);
	CHECK(moved && !result);
	auto xml = codec.encode(moved.value().value(), HX_OS_CONFIG_XML);
	CHECK(xml);
	auto back = codec.decode(xml.value(), HX_OS_CONFIG_XML);
	CHECK(back && back.value()->samples.count == 1);
	CHECK(!codec.decode("{\"name\":null}"));
	auto doc = hx::ConfigDocument::parse(
		"{\"items\":[1,\"text\",null,{\"children\":[true,2.5]}]}");
	CHECK(doc);
	hx_os_config_value replacement = {
		{NULL, 0}, {"false", 5}, NULL, NULL, 0, 0, HX_OS_CONFIG_BOOL};
	auto changed =
		doc.value().withValue("/items/3/children/0", replacement);
	CHECK(changed);
	auto json = changed.value().serialize();
	CHECK(json);
	auto yaml = hx::ConfigDocument::convert(
		json.value(), HX_OS_CONFIG_JSON, HX_OS_CONFIG_YAML);
	CHECK(yaml);
	auto again = hx::ConfigDocument::parse(yaml.value(), HX_OS_CONFIG_YAML);
	CHECK(again);
	std::puts("C++11 no-exception codec and document conveniences passed");
	return 0;
}
