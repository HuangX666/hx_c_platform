#ifndef HX_C_PLATFORM_PRINT_H
#define HX_C_PLATFORM_PRINT_H

#if defined(HX_C_PLATFORM_FORMAT_SYNTAX_H)
#error "Do not include hx_print.h and hx_format_syntax.h together"
#else

#include "hx_format.h"

/* C99 convenience calls take hx_format_arg values, including custom values.
 * Each argument is evaluated once; C does not specify their evaluation order.
 * Names, strings and custom objects remain borrowed through the call.
 * Use the array interfaces for more arguments, options or detailed errors.
 */
#define HX_PRINT_MAX_ARGUMENTS 64

/* The trailing sentinel permits zero values without empty variadic arguments.
 * Array sizes are constant: sizeof never evaluates the argument expressions.
 */
#define HX_PRINT_I_COUNT(...)                                                  \
	(sizeof((const hx_format_arg[]){__VA_ARGS__}) /                        \
			sizeof(hx_format_arg) -                                \
		1)
#define HX_PRINT_I_BOUND(...)                                                  \
	(HX_PRINT_I_COUNT(__VA_ARGS__) <= HX_PRINT_MAX_ARGUMENTS ? 1 : -1)
#define HX_PRINT_I_CHECK(...)                                                  \
	((void)sizeof(char[HX_PRINT_I_BOUND(__VA_ARGS__)]))

#define HX_PRINT_I_CALL(stream, newline, pattern, ...)                         \
	(HX_PRINT_I_CHECK(__VA_ARGS__),                                        \
		hx_print_args((stream), (newline),                             \
			hx_string_view_from_cstr((pattern)),                   \
			(const hx_format_arg[]){__VA_ARGS__},                  \
			HX_PRINT_I_COUNT(__VA_ARGS__), NULL))
#define HX_PRINT_I_FORMAT(out, pattern, ...)                                   \
	(HX_PRINT_I_CHECK(__VA_ARGS__),                                        \
		hx_format((out), hx_string_view_from_cstr((pattern)),          \
			(const hx_format_arg[]){__VA_ARGS__},                  \
			HX_PRINT_I_COUNT(__VA_ARGS__), NULL))

#define HX_PRINT_I_EXPAND(arguments) HX_PRINT_I_CALL arguments
#define HX_PRINT_I_FORMAT_EXPAND(arguments) HX_PRINT_I_FORMAT arguments

#define HX_PRINT(...)                                                          \
	HX_PRINT_I_EXPAND((HX_PRINT_STDOUT, 0, __VA_ARGS__, hx_fmt_i64(0)))
#define HX_PRINTLN(...)                                                        \
	HX_PRINT_I_EXPAND((HX_PRINT_STDOUT, 1, __VA_ARGS__, hx_fmt_i64(0)))
#define HX_EPRINTLN(...)                                                       \
	HX_PRINT_I_EXPAND((HX_PRINT_STDERR, 1, __VA_ARGS__, hx_fmt_i64(0)))
#define HX_FORMAT(...) HX_PRINT_I_FORMAT_EXPAND((__VA_ARGS__, hx_fmt_i64(0)))
#define HX_NAMED(name, value) hx_fmt_named((name), (value))

#endif
#endif
