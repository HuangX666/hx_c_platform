cmake_minimum_required(VERSION 3.16)
project(hx_platform_consumer LANGUAGES C)

option(HX_CONSUMER_CPP "Also validate the optional C++ interface" ON)
option(HX_CONSUMER_FORMAT_SYNTAX "Validate installed format generator" OFF)

if(WIN32)
	enable_language(RC)
endif()

find_package(hx_c_platform 0.1 CONFIG REQUIRED)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/HxNoExceptions.cmake")

enable_testing()

foreach(suite IN ITEMS ipc kcp config_tree codec stream buffered_file
	crypto tls
	byte_queue text_interop settings_apply config_backup ini calendar
	params parser preprocess windows_util native_interop power signal_all
	dispatch_params)
	add_executable(consumer_${suite} ../test_${suite}.c)
	target_link_libraries(consumer_${suite} PRIVATE
		hx_c_platform::hx_c_platform)
	set_target_properties(consumer_${suite} PROPERTIES
		C_STANDARD 99
		C_STANDARD_REQUIRED YES
		C_EXTENSIONS NO)

	if(MSVC)
		target_compile_options(consumer_${suite} PRIVATE /utf-8)
	endif()

	add_test(NAME installed_${suite} COMMAND consumer_${suite})
	set_tests_properties(installed_${suite} PROPERTIES TIMEOUT 60)
endforeach()

if(WIN32)
	target_sources(consumer_windows_util PRIVATE ../windows_fixture.rc)
	target_link_libraries(consumer_windows_util PRIVATE user32)
	target_link_libraries(consumer_native_interop PRIVATE ws2_32)
endif()

set(hx_search_fixture "${CMAKE_CURRENT_BINARY_DIR}/find-中文")
file(MAKE_DIRECTORY "${hx_search_fixture}/nested")
file(WRITE "${hx_search_fixture}/nested/😀.txt" "found\n")

foreach(example IN ITEMS hello configuration containers settings
	profile_intervals profile_statistics atomic_publication common_tasks
	variant find_files
	coroutine format)
	add_executable(consumer_${example} ../../examples/${example}.c)
	target_link_libraries(consumer_${example} PRIVATE
		hx_c_platform::hx_c_platform)
	set_target_properties(consumer_${example} PROPERTIES
		C_STANDARD 99
		C_STANDARD_REQUIRED YES
		C_EXTENSIONS NO)

	if(MSVC)
		target_compile_options(consumer_${example} PRIVATE /utf-8)
	endif()

	if(example STREQUAL "find_files")
		add_test(NAME installed_find_files COMMAND consumer_find_files
			"${hx_search_fixture}" "*.txt")
	else()
		add_test(NAME installed_${example} COMMAND consumer_${example})
	endif()
endforeach()

if(HX_CONSUMER_FORMAT_SYNTAX)
	add_executable(consumer_format_syntax ../../examples/format_syntax.c)
	target_link_libraries(consumer_format_syntax PRIVATE
		hx_c_platform::hx_c_platform)
	set_target_properties(consumer_format_syntax PROPERTIES
		C_STANDARD 99
		C_STANDARD_REQUIRED YES
		C_EXTENSIONS NO)

	if(MSVC)
		target_compile_options(consumer_format_syntax PRIVATE /utf-8)
	endif()

	hx_enable_format_syntax(consumer_format_syntax
		SOURCES ../../examples/format_syntax.c)
	add_test(NAME installed_format_syntax COMMAND consumer_format_syntax)
endif()

if(HX_CONSUMER_CPP)
	enable_language(CXX)
	include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/HxCpp20.cmake")

	hx_check_cpp20()

	add_executable(consumer_cpp consumer.cpp)
	add_executable(consumer_codec_cpp ../test_cpp_codec.cpp)
	target_link_libraries(consumer_codec_cpp PRIVATE hx_c_platform::cpp)
	set_target_properties(consumer_codec_cpp PROPERTIES
		CXX_STANDARD 11
		CXX_STANDARD_REQUIRED YES
		CXX_EXTENSIONS NO)

	if(MSVC)
		target_compile_options(consumer_codec_cpp PRIVATE /utf-8)
	endif()

	hx_disable_exceptions(consumer_codec_cpp)
	add_test(NAME installed_codec_cpp COMMAND consumer_codec_cpp)

	add_executable(consumer_signal_cpp ../cpp_signal_macro.cpp)
	target_link_libraries(consumer_signal_cpp PRIVATE hx_c_platform::cpp)
	set_target_properties(consumer_signal_cpp PROPERTIES
		CXX_STANDARD 11
		CXX_STANDARD_REQUIRED YES
		CXX_EXTENSIONS NO)
	hx_disable_exceptions(consumer_signal_cpp)
	add_test(NAME installed_signal_cpp COMMAND consumer_signal_cpp)

	add_executable(consumer_dispatch_cpp ../test_cpp_dispatch.cpp)
	target_link_libraries(consumer_dispatch_cpp PRIVATE hx_c_platform::cpp)
	set_target_properties(consumer_dispatch_cpp PROPERTIES
		CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
	if(MSVC)
		target_compile_options(consumer_dispatch_cpp PRIVATE /utf-8)
	endif()
	hx_disable_exceptions(consumer_dispatch_cpp)
	add_test(NAME installed_dispatch_cpp COMMAND consumer_dispatch_cpp)

	add_executable(consumer_crypto_cpp ../test_cpp_crypto.cpp)
	target_link_libraries(consumer_crypto_cpp PRIVATE hx_c_platform::cpp)
	set_target_properties(consumer_crypto_cpp PROPERTIES
		CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
	hx_disable_exceptions(consumer_crypto_cpp)
	add_test(NAME installed_crypto_cpp COMMAND consumer_crypto_cpp)

	add_executable(consumer_tls_cpp ../test_cpp_tls.cpp)
	target_link_libraries(consumer_tls_cpp PRIVATE hx_c_platform::cpp)
	set_target_properties(consumer_tls_cpp PROPERTIES
		CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
	hx_disable_exceptions(consumer_tls_cpp)
	add_test(NAME installed_tls_cpp COMMAND consumer_tls_cpp)

	add_executable(consumer_coroutine_cpp ../../examples/coroutine.cpp)
	add_executable(consumer_go ../../examples/go.cpp)

	if(HX_OS_HAVE_CPP20_COROUTINES)
		add_executable(consumer_coroutine20
			../../examples/coroutine20.cpp)
		target_link_libraries(consumer_coroutine20 PRIVATE
			hx_c_platform::cpp20)
		hx_disable_exceptions(consumer_coroutine20)
		add_test(NAME installed_coroutine20
			COMMAND consumer_coroutine20)
	endif()

	add_executable(consumer_utilities ../test_cpp_utilities.cpp)
	add_executable(consumer_tasks ../../examples/thread_pool.cpp)
	add_executable(consumer_direct ../../examples/direct_calls.cpp)
	add_executable(consumer_cpp_variant ../../examples/variant.cpp)
	add_executable(consumer_find_async ../../examples/find_files_async.cpp)

	foreach(target IN ITEMS consumer_cpp consumer_tasks consumer_utilities
		consumer_direct consumer_cpp_variant consumer_find_async
		consumer_coroutine_cpp consumer_go)
		hx_disable_exceptions(${target})
		target_link_libraries(${target} PRIVATE hx_c_platform::cpp)
		set_target_properties(${target} PROPERTIES
			CXX_STANDARD 11
			CXX_STANDARD_REQUIRED YES
			CXX_EXTENSIONS NO)

		if(MSVC)
			target_compile_options(${target} PRIVATE /utf-8)
		endif()
	endforeach()

	add_test(NAME installed_cpp COMMAND consumer_cpp)
	add_test(NAME installed_coroutine_cpp COMMAND consumer_coroutine_cpp)
	add_test(NAME installed_go COMMAND consumer_go)
	add_test(NAME installed_utilities COMMAND consumer_utilities)
	add_test(NAME installed_tasks COMMAND consumer_tasks)
	add_test(NAME installed_direct COMMAND consumer_direct)
	add_test(NAME installed_cpp_variant COMMAND consumer_cpp_variant)
	add_test(NAME installed_find_async COMMAND consumer_find_async
		"${hx_search_fixture}" "*.txt")
endif()
