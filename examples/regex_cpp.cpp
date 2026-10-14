#include <hx/Regex.hpp>
#include <cstdio>

int main()
{
	auto expression = hx::Regex::compile("(?<word>\\w+)\\s+\\k<word>");
	if (!expression.hasValue()) {
		std::fprintf(stderr, "Compile: %s\n",
			hx_os_status_name(expression.status()));
		return 1;
	}

	auto found = expression.value().find("hello hello");
	if (!found.hasValue()) {
		std::fprintf(stderr, "Find: %s\n",
			hx_os_status_name(found.status()));
		return 1;
	}

	auto word = found.value().group(1);
	if (!word.hasValue())
		return 1;
	std::printf("Repeated word: %.*s\n", (int)word.value().size(),
		word.value().data());

	return 0;
}
