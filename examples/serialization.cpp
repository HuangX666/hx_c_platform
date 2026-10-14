#include <hx/Codec.hpp>
#include "serialization_schema.h"
#include <cstdio>

int main()
{
	if (!hx_os_codec_available())
		return 0;

	hx::Codec<device> codec(device_schema);

	auto decoded = codec.decode("{\"name\":\"sensor\",\"samples\":[{"
				    "\"unit\":\"C\",\"value\":23.5,"
				    "\"channel\":1}],\"range\":{\"low\":-10.0,"
				    "\"high\":80.0},\"enabled\":true}");
	if (!decoded) {
		std::fprintf(stderr, "decode: %s\n",
			hx_os_status_name(decoded.status()));

		return 1;
	}

	auto text = codec.encode(decoded.value().value(), HX_OS_CONFIG_XML);
	if (!text) {
		std::fprintf(stderr, "encode: %s\n",
			hx_os_status_name(text.status()));

		return 1;
	}

	std::puts(text.value().c_str());

	// decoded owns the strings and arrays; destruction releases them
	// together.
	return 0;
}
