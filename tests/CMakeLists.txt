add_executable(hx_os_test_core test_core.c)
target_link_libraries(hx_os_test_core PRIVATE hx_c_platform)
set_target_properties(hx_os_test_core PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin"
	C_STANDARD 99 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
hx_apply_warnings(hx_os_test_core)
add_test(NAME core COMMAND hx_os_test_core)
set_tests_properties(core PROPERTIES TIMEOUT 20)

add_library(hx_os_test_library_module MODULE library_fixture.c)
target_include_directories(hx_os_test_library_module PRIVATE
	"${PROJECT_SOURCE_DIR}/include")
target_compile_definitions(hx_os_test_library_module PRIVATE
	HX_PLUGIN_VERSION="${PROJECT_VERSION}")
set_target_properties(hx_os_test_library_module PROPERTIES
	C_STANDARD 99 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
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
	C_STANDARD 99 C_STANDARD_REQUIRED YES C_EXTENSIONS NO
	RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
hx_apply_warnings(hx_os_test_library)
add_test(NAME library COMMAND hx_os_test_library
	$<TARGET_FILE:hx_os_test_library_module>)
set_tests_properties(library PROPERTIES TIMEOUT 20)

set(hx_test_suites data options lifecycle config containers sysinfo file_utils
	env paths thread_extra pipe io_options file_info config_serialize timer
	error_text util vm file_extra watch loop_fd)
if(HX_OS_ENABLE_THREAD_POOL)
	list(APPEND hx_test_suites pool)
endif()
if(HX_OS_ENABLE_NETWORK AND HX_OS_ENABLE_LOOP)
	list(APPEND hx_test_suites net loop_concurrency)
endif()
foreach(suite IN LISTS hx_test_suites)
	add_executable(hx_os_test_${suite} test_${suite}.c)
	target_link_libraries(hx_os_test_${suite} PRIVATE hx_c_platform)
	set_target_properties(hx_os_test_${suite} PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin"
		C_STANDARD 99 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
	hx_apply_warnings(hx_os_test_${suite})
	add_test(NAME ${suite} COMMAND hx_os_test_${suite})
	set_tests_properties(${suite} PROPERTIES TIMEOUT 20)
endforeach()

if(WIN32)
	target_link_libraries(hx_os_test_file_extra PRIVATE shell32)
endif()
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
	C_STANDARD 99 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
hx_apply_warnings(hx_os_header_c)

include(CheckLanguage)
check_language(CXX)
if(CMAKE_CXX_COMPILER)
	enable_language(CXX)
	file(GLOB facade_headers "${PROJECT_SOURCE_DIR}/include/hx/*.hpp")
	foreach(header IN LISTS facade_headers)
		get_filename_component(name "${header}" NAME)
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
		CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
	hx_apply_warnings(hx_os_header_cpp)
	add_library(hx_os_cpp_pool_header OBJECT cpp_pool_header.cpp)
	target_link_libraries(hx_os_cpp_pool_header PRIVATE hx_c_platform)
	set_target_properties(hx_os_cpp_pool_header PROPERTIES
		CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
	hx_apply_warnings(hx_os_cpp_pool_header)
	add_executable(hx_os_test_cpp_pool test_cpp_pool.cpp)
	target_link_libraries(hx_os_test_cpp_pool PRIVATE hx_c_platform)
	set_target_properties(hx_os_test_cpp_pool PROPERTIES
		CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO
		RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
	hx_apply_warnings(hx_os_test_cpp_pool)
	add_test(NAME cpp_pool COMMAND hx_os_test_cpp_pool)
	set_tests_properties(cpp_pool PROPERTIES TIMEOUT 20)
	add_executable(hx_os_test_cpp_platform test_cpp_platform.cpp)
	target_link_libraries(hx_os_test_cpp_platform PRIVATE
		hx_c_platform::cpp)
	set_target_properties(hx_os_test_cpp_platform PROPERTIES
		CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO
		RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
	hx_apply_warnings(hx_os_test_cpp_platform)
	add_test(NAME cpp_platform COMMAND hx_os_test_cpp_platform
		$<TARGET_FILE:hx_os_test_library_module>)
	set_tests_properties(cpp_platform PROPERTIES TIMEOUT 20)
endif()
