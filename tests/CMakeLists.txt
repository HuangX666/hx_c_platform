add_executable(hx_os_test_core test_core.c)
target_link_libraries(hx_os_test_core PRIVATE hx_c_platform)
set_target_properties(hx_os_test_core PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin"
	C_STANDARD 99
	C_STANDARD_REQUIRED YES
	C_EXTENSIONS NO)
hx_apply_warnings(hx_os_test_core)
add_test(NAME core COMMAND hx_os_test_core)
set_tests_properties(core PROPERTIES TIMEOUT 20)

add_library(hx_os_test_library_module MODULE library_fixture.c)
target_include_directories(hx_os_test_library_module PRIVATE
	"${PROJECT_SOURCE_DIR}/include")
target_compile_definitions(hx_os_test_library_module PRIVATE
	HX_PLUGIN_VERSION="${PROJECT_VERSION}")
set_target_properties(hx_os_test_library_module PROPERTIES
	C_STANDARD 99
	C_STANDARD_REQUIRED YES
	C_EXTENSIONS NO)

if(HX_OS_SANITIZER AND NOT MSVC)
	target_compile_options(hx_os_test_library_module PRIVATE
		-fsanitize=${HX_OS_SANITIZER} -fno-omit-frame-pointer)
	target_link_options(hx_os_test_library_module PRIVATE
		-fsanitize=${HX_OS_SANITIZER})
endif()

hx_apply_warnings(hx_os_test_library_module)

add_executable(hx_os_test_library test_library.c)
target_link_libraries(hx_os_test_library PRIVATE hx_c_platform)
set_target_properties(hx_os_test_library PROPERTIES
	C_STANDARD 99
	C_STANDARD_REQUIRED YES
	C_EXTENSIONS NO
	RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
hx_apply_warnings(hx_os_test_library)
add_test(NAME library COMMAND hx_os_test_library
	$<TARGET_FILE:hx_os_test_library_module>)
set_tests_properties(library PROPERTIES TIMEOUT 20)

set(hx_test_suites data options lifecycle config containers sysinfo file_utils
	text_interop byte_queue stream buffered_file
	env paths thread_extra pipe io_options file_info
	config_serialize config_tree config_backup ini codec timer
	error_text util vm file_extra watch loop_fd system delayed local duplex
	registry power obfuscate calendar calendar_dst text_lookup bitstream
	calldata signal
	settings settings_concurrency settings_config settings_apply
	profiler atomic
	string_tools lexer wide_wait catalog_filename procedure
	android_baseline convenience variant file_search stl
	coroutine coroutine_io coroutine_sync format kcp ipc ipc_allocator)

if(WIN32)
	list(APPEND hx_test_suites error_text_windows)
else()
	list(APPEND hx_test_suites debug)
endif()

if(NOT BUILD_SHARED_LIBS)
	list(APPEND hx_test_suites ipc_wait_error)
endif()

if(HX_OS_ENABLE_THREAD_POOL)
	list(APPEND hx_test_suites pool pool_wait)
endif()

if(HX_OS_ENABLE_NETWORK AND HX_OS_ENABLE_LOOP)
	list(APPEND hx_test_suites net loop_concurrency)
endif()

foreach(suite IN LISTS hx_test_suites)
	add_executable(hx_os_test_${suite} test_${suite}.c)
	target_link_libraries(hx_os_test_${suite} PRIVATE hx_c_platform)
	set_target_properties(hx_os_test_${suite} PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin"
		C_STANDARD 99
		C_STANDARD_REQUIRED YES
		C_EXTENSIONS NO)
	hx_apply_warnings(hx_os_test_${suite})
	add_test(NAME ${suite} COMMAND hx_os_test_${suite})
	set_tests_properties(${suite} PROPERTIES TIMEOUT 20)
endforeach()

if(HX_OS_ENABLE_THREAD_POOL)
	target_include_directories(hx_os_test_pool_wait PRIVATE
		"${PROJECT_SOURCE_DIR}/src" "${PROJECT_BINARY_DIR}/private")
endif()

if(WIN32)
	target_include_directories(hx_os_test_error_text_windows PRIVATE
		"${PROJECT_BINARY_DIR}/private")
endif()

if(NOT BUILD_SHARED_LIBS)
	target_include_directories(hx_os_test_ipc_wait_error PRIVATE
		"${PROJECT_BINARY_DIR}/private")
endif()

set_tests_properties(ipc PROPERTIES TIMEOUT 60)
set_tests_properties(ipc_allocator PROPERTIES TIMEOUT 30)

target_sources(hx_os_test_variant PRIVATE variant_fixture.c)

if(HX_OS_TEST_FORMAT_SYNTAX)
	add_executable(hx_os_test_format_generated test_format_generated.c)

	add_library(hx_os_format_definitions INTERFACE)
	target_compile_definitions(hx_os_format_definitions INTERFACE
		HX_TEST_FORMAT_NUMBER=double)
	target_link_libraries(hx_os_test_format_generated PRIVATE
		hx_c_platform hx_os_format_definitions)
	set_target_properties(hx_os_test_format_generated PROPERTIES
		C_STANDARD 99
		C_STANDARD_REQUIRED YES
		C_EXTENSIONS NO
		RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
	hx_apply_warnings(hx_os_test_format_generated)
	hx_enable_format_syntax(hx_os_test_format_generated
		SOURCES test_format_generated.c)
	add_test(NAME format_generated COMMAND hx_os_test_format_generated)
	set_tests_properties(format_generated PROPERTIES TIMEOUT 20)
endif()

if(NOT WIN32)
	target_link_libraries(hx_os_test_coroutine PRIVATE m)
endif()

if(WIN32)
	target_link_libraries(hx_os_test_file_extra PRIVATE shell32)
	target_link_libraries(hx_os_test_registry PRIVATE advapi32)
endif()

if(HX_OS_ENABLE_CALENDAR)
	add_test(NAME calendar_offset_utc COMMAND hx_os_test_calendar 0)
	add_test(NAME calendar_offset_east COMMAND hx_os_test_calendar 540)
	add_test(NAME calendar_offset_west COMMAND hx_os_test_calendar -300)
	add_test(NAME calendar_offset_quarter COMMAND hx_os_test_calendar 345)
	set_tests_properties(calendar_offset_utc PROPERTIES
		TIMEOUT 20 ENVIRONMENT "TZ=UTC0")
	set_tests_properties(calendar_offset_east PROPERTIES
		TIMEOUT 20 ENVIRONMENT "TZ=JST-9")
	set_tests_properties(calendar_offset_west PROPERTIES
		TIMEOUT 20 ENVIRONMENT "TZ=EST5")
	set_tests_properties(calendar_offset_quarter PROPERTIES
		TIMEOUT 20 ENVIRONMENT "TZ=NPT-5:45")
	if(NOT WIN32)
		add_test(NAME calendar_offset_seconds
			COMMAND hx_os_test_calendar_dst seconds)
		set_tests_properties(calendar_offset_seconds PROPERTIES
			TIMEOUT 20 ENVIRONMENT "TZ=XXX-0:00:30")
	endif()
endif()

set_tests_properties(local PROPERTIES SKIP_RETURN_CODE 77)

if(WIN32)
	set_tests_properties(calendar_dst PROPERTIES ENVIRONMENT "TZ=EST5EDT")
else()
	set_tests_properties(calendar_dst PROPERTIES
		ENVIRONMENT "TZ=EST5EDT,M3.2.0/2,M11.1.0/2")
endif()

find_package(Python3 QUIET COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND AND HX_OS_ENABLE_FORMAT
		AND NOT CMAKE_CROSSCOMPILING)
	set(format_checks --executable $<TARGET_FILE:hx_os_test_format>)

	if(HX_OS_TEST_FORMAT_SYNTAX)
		get_target_property(format_clang hx_os_test_format_generated
			HX_FORMAT_CLANG)
		set(format_dir "${CMAKE_CURRENT_BINARY_DIR}/hx-format")
		set(format_target "${format_dir}/hx_os_test_format_generated")
		set(format_config "$<IF:$<BOOL:$<CONFIG>>,$<CONFIG>,default>")
		set(format_script "${PROJECT_SOURCE_DIR}/tools")
		list(APPEND format_checks
			--script "${format_script}/hx_format_codegen.py"
			--clang "${format_clang}"
			--config "${format_target}/${format_config}/flags-C.cfg"
			--directory "${CMAKE_CURRENT_BINARY_DIR}/format-checks")
	endif()

	add_test(NAME format_build_tool COMMAND "${Python3_EXECUTABLE}"
		"${CMAKE_CURRENT_SOURCE_DIR}/test_format_codegen.py"
		${format_checks})
	set_tests_properties(format_build_tool PROPERTIES TIMEOUT 120)
endif()

if(Python3_Interpreter_FOUND)
	file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/obfuscated-input.txt"
		"generated 中文😀")
	add_custom_command(
		OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/obfuscated_fixture.h"
		COMMAND "${Python3_EXECUTABLE}"
			"${PROJECT_SOURCE_DIR}/tools/obfuscate_string.py"
			--name hx_generated --input
			"${CMAKE_CURRENT_BINARY_DIR}/obfuscated-input.txt"
			--output
			"${CMAKE_CURRENT_BINARY_DIR}/obfuscated_fixture.h"
		DEPENDS "${PROJECT_SOURCE_DIR}/tools/obfuscate_string.py"
			"${CMAKE_CURRENT_BINARY_DIR}/obfuscated-input.txt"
		VERBATIM)

	add_executable(hx_os_test_obfuscate_generated test_obfuscate_generated.c
		"${CMAKE_CURRENT_BINARY_DIR}/obfuscated_fixture.h")
	target_include_directories(hx_os_test_obfuscate_generated PRIVATE
		"${CMAKE_CURRENT_BINARY_DIR}")
	target_link_libraries(hx_os_test_obfuscate_generated PRIVATE
		hx_c_platform)
	set_target_properties(hx_os_test_obfuscate_generated PROPERTIES
		C_STANDARD 99
		C_STANDARD_REQUIRED YES
		C_EXTENSIONS NO
		RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
	hx_apply_warnings(hx_os_test_obfuscate_generated)
	add_test(NAME obfuscate_generated
		COMMAND hx_os_test_obfuscate_generated)
	set_tests_properties(obfuscate_generated PROPERTIES TIMEOUT 20)
endif()

add_executable(hx_os_process_fixture process_fixture.c)
add_executable(hx_os_test_process test_process.c)

foreach(process_target IN ITEMS hx_os_process_fixture hx_os_test_process)
	target_link_libraries(${process_target} PRIVATE hx_c_platform)
	set_target_properties(${process_target} PROPERTIES
		C_STANDARD 99
		C_STANDARD_REQUIRED YES
		C_EXTENSIONS NO
		RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
	hx_apply_warnings(${process_target})
endforeach()

add_test(NAME process COMMAND hx_os_test_process
	$<TARGET_FILE:hx_os_process_fixture>)
set_tests_properties(process PROPERTIES TIMEOUT 30)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND HX_OS_ENABLE_RANDOM)
	add_test(NAME random_fallback COMMAND hx_os_test_util fallback)
	add_test(NAME random_failure COMMAND hx_os_test_util failure)
	set_tests_properties(random_fallback random_failure PROPERTIES
		TIMEOUT 20 SKIP_RETURN_CODE 77)
endif()

file(GLOB headers "${PROJECT_SOURCE_DIR}/include/*.h")

foreach(header IN LISTS headers)
	get_filename_component(name "${header}" NAME)
	file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/${name}.c"
		"#include <${name}>\n#include <${name}>\n")
	list(APPEND c_headers "${CMAKE_CURRENT_BINARY_DIR}/${name}.c")
endforeach()

add_library(hx_os_header_c OBJECT ${c_headers})
target_link_libraries(hx_os_header_c PRIVATE hx_c_platform)
set_target_properties(hx_os_header_c PROPERTIES
	C_STANDARD 99
	C_STANDARD_REQUIRED YES
	C_EXTENSIONS NO)
hx_apply_warnings(hx_os_header_c)

if(CMAKE_CXX_COMPILER_LOADED)
	foreach(standard IN ITEMS 11 20)
		if(standard EQUAL 20 AND NOT HX_OS_HAVE_CPP20_COROUTINES)
			continue()
		endif()

		add_executable(hx_os_test_cpp_go${standard} test_cpp_go.cpp)
		target_link_libraries(hx_os_test_cpp_go${standard} PRIVATE
			hx_c_platform::cpp)
		set_target_properties(hx_os_test_cpp_go${standard} PROPERTIES
			CXX_STANDARD ${standard}
			CXX_STANDARD_REQUIRED YES
			CXX_EXTENSIONS NO
			RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")

		if(standard EQUAL 20)
			target_compile_definitions(hx_os_test_cpp_go${standard}
				PRIVATE
				HX_TEST_CPP20=1)
		endif()

		hx_apply_warnings(hx_os_test_cpp_go${standard})
		add_test(NAME cpp_go${standard}
			COMMAND hx_os_test_cpp_go${standard})
		set_tests_properties(cpp_go${standard} PROPERTIES TIMEOUT 20)
	endforeach()

	add_executable(hx_os_test_cpp_codec test_cpp_codec.cpp)
	target_link_libraries(hx_os_test_cpp_codec PRIVATE hx_c_platform::cpp)
	set_target_properties(hx_os_test_cpp_codec PROPERTIES
		CXX_STANDARD 11
		CXX_STANDARD_REQUIRED YES
		CXX_EXTENSIONS NO
		RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
	hx_apply_warnings(hx_os_test_cpp_codec)
	add_test(NAME cpp_codec COMMAND hx_os_test_cpp_codec)
	set_tests_properties(cpp_codec PROPERTIES TIMEOUT 20)

	if(CMAKE_C_COMPILER_ID STREQUAL "MSVC")
		include(CheckCCompilerFlag)

		check_c_compiler_flag("/Zc:preprocessor" HX_CODEC_HAVE_MSVC_PP)
		if(HX_CODEC_HAVE_MSVC_PP)
			foreach(language IN ITEMS c cpp)
				if(language STREQUAL "c")
					set(hx_codec_pp_source test_codec.c)
				else()
					set(hx_codec_pp_source
						test_cpp_codec.cpp)
				endif()

				set(hx_codec_pp_target
					hx_os_codec_conforming_${language})

				add_executable(${hx_codec_pp_target}
					${hx_codec_pp_source})
				target_link_libraries(${hx_codec_pp_target}
					PRIVATE hx_c_platform::cpp)
				target_compile_options(${hx_codec_pp_target}
					PRIVATE /Zc:preprocessor)
				set_target_properties(${hx_codec_pp_target}
					PROPERTIES C_STANDARD 99
					C_STANDARD_REQUIRED YES
					C_EXTENSIONS NO
					CXX_STANDARD 11
					CXX_STANDARD_REQUIRED YES
					CXX_EXTENSIONS NO
					RUNTIME_OUTPUT_DIRECTORY
					"${PROJECT_BINARY_DIR}/bin")
				add_test(NAME codec_conforming_${language}
					COMMAND ${hx_codec_pp_target})
				set_tests_properties(
					codec_conforming_${language}
					PROPERTIES TIMEOUT 20)
				hx_apply_warnings(${hx_codec_pp_target})
			endforeach()
		endif()
	endif()

	add_executable(hx_os_test_cpp_format test_cpp_format.cpp)
	target_link_libraries(hx_os_test_cpp_format PRIVATE hx_c_platform::cpp)
	set_target_properties(hx_os_test_cpp_format PROPERTIES
		CXX_STANDARD 11
		CXX_STANDARD_REQUIRED YES
		CXX_EXTENSIONS NO
		RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
	hx_apply_warnings(hx_os_test_cpp_format)
	add_test(NAME cpp_format COMMAND hx_os_test_cpp_format)
	set_tests_properties(cpp_format PROPERTIES TIMEOUT 20)

	add_executable(hx_os_test_cpp_coroutine test_cpp_coroutine.cpp)
	target_link_libraries(hx_os_test_cpp_coroutine PRIVATE
		hx_c_platform::cpp)
	set_target_properties(hx_os_test_cpp_coroutine PROPERTIES
		CXX_STANDARD 11
		CXX_STANDARD_REQUIRED YES
		CXX_EXTENSIONS NO
		RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
	hx_apply_warnings(hx_os_test_cpp_coroutine)
	add_test(NAME cpp_coroutine COMMAND hx_os_test_cpp_coroutine)
	set_tests_properties(cpp_coroutine PROPERTIES TIMEOUT 20)

	if(HX_OS_HAVE_CPP20_COROUTINES)
		add_executable(hx_os_test_cpp_coroutine20
			test_cpp_coroutine.cpp)
		target_link_libraries(hx_os_test_cpp_coroutine20 PRIVATE
			hx_c_platform::cpp)
		target_compile_definitions(hx_os_test_cpp_coroutine20 PRIVATE
			HX_TEST_CPP20=1)
		set_target_properties(hx_os_test_cpp_coroutine20 PROPERTIES
			CXX_STANDARD 20
			CXX_STANDARD_REQUIRED YES
			CXX_EXTENSIONS NO
			RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
		hx_apply_warnings(hx_os_test_cpp_coroutine20)
		add_test(NAME cpp_coroutine20
			COMMAND hx_os_test_cpp_coroutine20)
		set_tests_properties(cpp_coroutine20 PROPERTIES TIMEOUT 20)
	endif()

	add_executable(hx_os_test_cpp_variant test_cpp_variant.cpp
		cpp_variant_fixture.cpp)
	target_link_libraries(hx_os_test_cpp_variant PRIVATE hx_c_platform::cpp)
	set_target_properties(hx_os_test_cpp_variant PROPERTIES
		CXX_STANDARD 11
		CXX_STANDARD_REQUIRED YES
		CXX_EXTENSIONS NO
		RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
	hx_apply_warnings(hx_os_test_cpp_variant)
	add_test(NAME cpp_variant COMMAND hx_os_test_cpp_variant)
	set_tests_properties(cpp_variant PROPERTIES TIMEOUT 20)

	add_executable(hx_os_test_cpp_utilities test_cpp_utilities.cpp)
	target_link_libraries(hx_os_test_cpp_utilities PRIVATE
		hx_c_platform::cpp)
	set_target_properties(hx_os_test_cpp_utilities PROPERTIES
		CXX_STANDARD 11
		CXX_STANDARD_REQUIRED YES
		CXX_EXTENSIONS NO
		RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
	hx_apply_warnings(hx_os_test_cpp_utilities)
	add_test(NAME cpp_utilities COMMAND hx_os_test_cpp_utilities)
	set_tests_properties(cpp_utilities PROPERTIES TIMEOUT 20)

	add_executable(hx_os_test_cpp_extensions test_cpp_extensions.cpp)
	target_link_libraries(hx_os_test_cpp_extensions PRIVATE
		hx_c_platform::cpp)

	if(WIN32)
		target_link_libraries(hx_os_test_cpp_extensions PRIVATE
			advapi32)
	endif()

	set_target_properties(hx_os_test_cpp_extensions PROPERTIES
		CXX_STANDARD 11
		CXX_STANDARD_REQUIRED YES
		CXX_EXTENSIONS NO
		RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
	hx_apply_warnings(hx_os_test_cpp_extensions)
	add_test(NAME cpp_extensions COMMAND hx_os_test_cpp_extensions)
	set_tests_properties(cpp_extensions PROPERTIES TIMEOUT 20)
	file(GLOB facade_headers "${PROJECT_SOURCE_DIR}/include/hx/*.hpp")

	foreach(header IN LISTS facade_headers)
		get_filename_component(name "${header}" NAME)

		if(name STREQUAL "Coroutines20.hpp")
			continue()
		endif()

		file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/${name}.cpp"
			"#include <hx/${name}>\n#include <hx/${name}>\n")
		list(APPEND cpp_headers
			"${CMAKE_CURRENT_BINARY_DIR}/${name}.cpp")
	endforeach()

	foreach(header IN LISTS headers)
		get_filename_component(name "${header}" NAME)
		file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/${name}.cpp"
			"#include <${name}>\n#include <${name}>\n")
		list(APPEND cpp_headers
			"${CMAKE_CURRENT_BINARY_DIR}/${name}.cpp")
	endforeach()

	add_library(hx_os_header_cpp OBJECT ${cpp_headers})
	target_link_libraries(hx_os_header_cpp PRIVATE hx_c_platform)
	set_target_properties(hx_os_header_cpp PROPERTIES
		CXX_STANDARD 11
		CXX_STANDARD_REQUIRED YES
		CXX_EXTENSIONS NO)
	hx_apply_warnings(hx_os_header_cpp)

	add_library(hx_os_cpp_pool_header OBJECT cpp_pool_header.cpp)
	target_link_libraries(hx_os_cpp_pool_header PRIVATE hx_c_platform)
	set_target_properties(hx_os_cpp_pool_header PROPERTIES
		CXX_STANDARD 11
		CXX_STANDARD_REQUIRED YES
		CXX_EXTENSIONS NO)
	hx_apply_warnings(hx_os_cpp_pool_header)

	add_executable(hx_os_test_cpp_pool test_cpp_pool.cpp)
	target_link_libraries(hx_os_test_cpp_pool PRIVATE hx_c_platform)
	set_target_properties(hx_os_test_cpp_pool PROPERTIES
		CXX_STANDARD 11
		CXX_STANDARD_REQUIRED YES
		CXX_EXTENSIONS NO
		RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
	hx_apply_warnings(hx_os_test_cpp_pool)
	add_test(NAME cpp_pool COMMAND hx_os_test_cpp_pool)
	set_tests_properties(cpp_pool PROPERTIES TIMEOUT 20)

	add_executable(hx_os_test_cpp_platform test_cpp_platform.cpp)
	add_dependencies(hx_os_test_cpp_platform
		hx_os_test_library_module hx_os_process_fixture)
	target_link_libraries(hx_os_test_cpp_platform PRIVATE
		hx_c_platform::cpp)
	set_target_properties(hx_os_test_cpp_platform PROPERTIES
		CXX_STANDARD 11
		CXX_STANDARD_REQUIRED YES
		CXX_EXTENSIONS NO
		RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
	hx_apply_warnings(hx_os_test_cpp_platform)
	add_test(NAME cpp_platform COMMAND hx_os_test_cpp_platform
		$<TARGET_FILE:hx_os_test_library_module>
		$<TARGET_FILE:hx_os_process_fixture>)
	set_tests_properties(cpp_platform PROPERTIES TIMEOUT 20)
endif()
