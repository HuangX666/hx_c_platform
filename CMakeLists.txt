cmake_minimum_required(VERSION 3.16)
project(hx_c_platform VERSION 0.1.0 LANGUAGES C)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CheckSymbolExists)
include(CheckCSourceCompiles)
include(cmake/HxWarnings.cmake)

option(BUILD_SHARED_LIBS "Build shared library" OFF)
option(HX_OS_BUILD_TESTS "Build contract tests" ON)
option(HX_OS_BUILD_EXAMPLES "Build standalone C examples" ON)
option(HX_OS_BUILD_BENCHMARKS "Build development microbenchmarks" OFF)
option(HX_OS_ENABLE_NETWORK "Build native networking" ON)
option(HX_OS_ENABLE_LOOP "Build asynchronous event loop"
	${HX_OS_ENABLE_NETWORK})
option(HX_OS_ENABLE_THREAD_POOL "Build bounded task pools" ON)
option(HX_OS_ENABLE_MEMORY_POOLS "Build memory pools and arena" ON)
option(HX_OS_ENABLE_ICONV "Build legacy text encoding adapter" OFF)
option(HX_OS_ENABLE_CONFIG "Build configuration file parsers" ON)
option(HX_OS_ENABLE_CONTAINERS "Build generic C containers" ON)
option(HX_OS_ENABLE_SYSINFO "Build native system information" ON)
option(HX_OS_ENABLE_LIBRARY "Build native dynamic library loading" ON)
option(HX_OS_ENABLE_FILE_UTILS "Build file and path conveniences" ON)
option(HX_OS_ENABLE_ENV "Build UTF-8 environment access" ON)
option(HX_OS_ENABLE_PATHS "Build special path queries" ON)
set(HX_OS_ICONV_SOURCE_DIR "" CACHE PATH "Unmodified GNU libiconv source")
set(HX_OS_SANITIZER "" CACHE STRING "address,undefined or thread")

if(HX_OS_ENABLE_LOOP AND NOT HX_OS_ENABLE_NETWORK)
	message(FATAL_ERROR "HX_OS_ENABLE_LOOP requires HX_OS_ENABLE_NETWORK")
endif()

find_package(Threads REQUIRED)
set(HX_OS_PLATFORM_NAME "${CMAKE_SYSTEM_NAME}")
set(hx_platform_sources)
set(hx_private_include)
set(HX_OS_HAVE_PTHREAD_TRYJOIN 0)
set(HX_OS_HAVE_TIMED_JOIN 1)
set(HX_OS_NEED_RT 0)
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
	list(APPEND hx_platform_sources src/windows/thread_native.c
		src/windows/file_native.c)
	if(HX_OS_ENABLE_LOOP)
		list(APPEND hx_platform_sources src/windows/loop_native.c)
	endif()
	set(hx_private_include src/windows)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR
	CMAKE_SYSTEM_NAME STREQUAL "Darwin")
	list(APPEND hx_platform_sources src/posix/thread_native.c
		src/posix/file_native.c)
	if(HX_OS_ENABLE_LOOP)
		list(APPEND hx_platform_sources src/posix/loop_io.c)
		if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
			list(APPEND hx_platform_sources src/linux/loop_native.c)
		else()
			list(APPEND hx_platform_sources src/apple/loop_native.c)
		endif()
	endif()
	set(hx_private_include src/posix)
	set(CMAKE_REQUIRED_LIBRARIES Threads::Threads)
	set(CMAKE_REQUIRED_DEFINITIONS -D_GNU_SOURCE)
	unset(HX_OS_HAVE_PTHREAD_TRYJOIN)
	check_symbol_exists(pthread_tryjoin_np pthread.h
		HX_OS_HAVE_PTHREAD_TRYJOIN)
	if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
		check_symbol_exists(pthread_condattr_setclock pthread.h
			HX_OS_HAVE_COND_CLOCK)
		if(NOT HX_OS_HAVE_COND_CLOCK)
			message(FATAL_ERROR
				"Monotonic pthread conditions required")
		endif()
		if(NOT HX_OS_HAVE_PTHREAD_TRYJOIN)
			set(HX_OS_HAVE_TIMED_JOIN 0)
		endif()
	endif()
	unset(CMAKE_REQUIRED_LIBRARIES)
	unset(CMAKE_REQUIRED_DEFINITIONS)
	set(CMAKE_REQUIRED_DEFINITIONS -D_POSIX_C_SOURCE=200809L)
	check_symbol_exists(clock_gettime time.h HX_OS_CLOCK_IN_LIBC)
	if(NOT HX_OS_CLOCK_IN_LIBC)
		set(CMAKE_REQUIRED_LIBRARIES rt)
		check_symbol_exists(clock_gettime time.h HX_OS_CLOCK_IN_RT)
		if(NOT HX_OS_CLOCK_IN_RT)
			message(FATAL_ERROR
				"Native monotonic clock_gettime is required")
		endif()
		set(HX_OS_NEED_RT 1)
	endif()
	unset(CMAKE_REQUIRED_DEFINITIONS)
	unset(CMAKE_REQUIRED_LIBRARIES)
else()
	message(FATAL_ERROR "Target not adapted: ${CMAKE_SYSTEM_NAME}")
endif()

set(HX_OS_HAVE_ICONV 0)
set(HX_OS_ICONV_SYSTEM 0)
set(HX_OS_ICONV_CONST "")
if(HX_OS_ENABLE_ICONV)
	if(HX_OS_ICONV_SOURCE_DIR)
		include(cmake/GnuIconv.cmake)
		set(HX_OS_ICONV_CONST "const")
	else()
		find_package(Iconv REQUIRED)
		set(HX_OS_ICONV_SYSTEM 1)
		set(CMAKE_REQUIRED_LIBRARIES Iconv::Iconv)
		if(MSVC)
			set(CMAKE_REQUIRED_FLAGS /WX)
		else()
			set(CMAKE_REQUIRED_FLAGS -Werror)
		endif()
		check_c_source_compiles("
			#include <iconv.h>
			int main(void) {
				size_t (*fn)(iconv_t, const char **, size_t *,
					char **, size_t *) = iconv;
				return fn == 0;
			}" HX_OS_ICONV_TAKES_CONST)
		if(HX_OS_ICONV_TAKES_CONST)
			set(HX_OS_ICONV_CONST "const")
		endif()
		unset(CMAKE_REQUIRED_FLAGS)
		unset(CMAKE_REQUIRED_LIBRARIES)
	endif()
	set(HX_OS_HAVE_ICONV 1)
endif()

set(HX_OS_HAVE_LOCAL_STRTOD 1)
if(NOT WIN32)
	set(CMAKE_REQUIRED_DEFINITIONS -D_GNU_SOURCE)
	check_symbol_exists(strtod_l "stdlib.h;locale.h" HX_OS_HAS_STRTOD_L)
	check_symbol_exists(newlocale locale.h HX_OS_HAS_NEWLOCALE)
	check_symbol_exists(freelocale locale.h HX_OS_HAS_FREELOCALE)
	unset(CMAKE_REQUIRED_DEFINITIONS)
	if(NOT HX_OS_HAS_STRTOD_L OR NOT HX_OS_HAS_NEWLOCALE OR
		NOT HX_OS_HAS_FREELOCALE)
		set(HX_OS_HAVE_LOCAL_STRTOD 0)
	endif()
endif()
include(cmake/ConfigParsers.cmake)

configure_file(cmake/hx_os_config.h.in
	${CMAKE_CURRENT_BINARY_DIR}/private/hx_os_config.h)

add_library(hx_c_platform
	src/base.c src/atomic.c src/time.c src/unicode.c src/thread.c
	src/string.c src/file.c src/text.c src/optional.c
	src/config.c src/config_values.c
	src/containers.c
	src/sysinfo.c src/library.c src/path.c src/env.c src/paths.c
	${hx_parser_sources} ${hx_parser_objects}
	${hx_platform_sources})
if(HX_OS_ENABLE_SYSINFO)
	if(WIN32)
		target_sources(hx_c_platform PRIVATE
			src/windows/sysinfo_native.c)
		target_link_libraries(hx_c_platform PRIVATE psapi)
	else()
		target_sources(hx_c_platform PRIVATE src/posix/sysinfo_native.c)
	endif()
endif()
if(HX_OS_ENABLE_LIBRARY OR HX_OS_ENABLE_PATHS)
	target_link_libraries(hx_c_platform PRIVATE ${CMAKE_DL_LIBS})
endif()
if(HX_OS_ENABLE_PATHS)
	if(WIN32)
		target_sources(hx_c_platform PRIVATE src/windows/paths_native.c)
		target_link_libraries(hx_c_platform PRIVATE shell32 ole32 uuid)
	else()
		target_sources(hx_c_platform PRIVATE src/posix/paths_native.c)
	endif()
endif()
if(HX_OS_ENABLE_FILE_UTILS)
	target_sources(hx_c_platform PRIVATE src/file_utils.c src/path_glob.c)
	if(WIN32)
		target_sources(hx_c_platform PRIVATE
			src/windows/file_utils_native.c)
	else()
		target_sources(hx_c_platform PRIVATE
			src/posix/file_utils_native.c)
	endif()
else()
	target_sources(hx_c_platform PRIVATE src/file_utils_disabled.c)
endif()
if(HX_OS_ENABLE_CONTAINERS)
	target_sources(hx_c_platform PRIVATE src/sequence.c src/map.c)
	target_include_directories(hx_c_platform SYSTEM PRIVATE
		third_party/uthash/v2.4.0)
	install(FILES third_party/uthash/v2.4.0/LICENSE
		DESTINATION ${CMAKE_INSTALL_DATADIR}/hx_c_platform/licenses
		RENAME uthash-LICENSE)
else()
	target_sources(hx_c_platform PRIVATE src/containers_disabled.c)
endif()
if(HX_OS_ENABLE_CONFIG)
	target_sources(hx_c_platform PRIVATE src/config_json.c
		src/config_lines.c src/config_toml.c src/config_yaml.c
		src/config_xml.c)
endif()
if(HX_OS_ENABLE_MEMORY_POOLS)
	target_sources(hx_c_platform PRIVATE src/memory_pool.c src/arena.c)
endif()
if(HX_OS_ENABLE_THREAD_POOL)
	target_sources(hx_c_platform PRIVATE src/thread_pool.c)
endif()
if(HX_OS_ENABLE_NETWORK)
	target_sources(hx_c_platform PRIVATE src/net.c)
endif()
if(HX_OS_ENABLE_LOOP)
	target_sources(hx_c_platform PRIVATE src/loop.c)
endif()
add_library(hx_c_platform::hx_c_platform ALIAS hx_c_platform)
add_library(hx_c_platform_cpp INTERFACE)
add_library(hx_c_platform::cpp ALIAS hx_c_platform_cpp)
set_target_properties(hx_c_platform_cpp PROPERTIES EXPORT_NAME cpp)
target_link_libraries(hx_c_platform_cpp INTERFACE hx_c_platform)
target_compile_features(hx_c_platform_cpp INTERFACE cxx_std_11)
set_target_properties(hx_c_platform PROPERTIES
	C_STANDARD 99 C_STANDARD_REQUIRED YES C_EXTENSIONS NO
	C_VISIBILITY_PRESET hidden
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin"
	VERSION ${PROJECT_VERSION} SOVERSION 0)
target_include_directories(hx_c_platform PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
	$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
	PRIVATE src ${hx_private_include}
	${CMAKE_CURRENT_BINARY_DIR}/private ${hx_parser_includes})
target_compile_definitions(hx_c_platform PRIVATE
	${hx_parser_aliases} ${hx_parser_definitions})
target_compile_definitions(hx_c_platform PRIVATE HX_OS_BUILD)
target_link_libraries(hx_c_platform PRIVATE Threads::Threads)
if(HX_OS_NEED_RT)
	target_link_libraries(hx_c_platform PRIVATE rt)
endif()
if(BUILD_SHARED_LIBS)
	target_compile_definitions(hx_c_platform PUBLIC HX_OS_SHARED)
endif()
if(WIN32)
	if(HX_OS_ENABLE_NETWORK)
		target_link_libraries(hx_c_platform PRIVATE ws2_32)
	endif()
	target_compile_definitions(hx_c_platform PRIVATE
		_WIN32_WINNT=0x0601 WINVER=0x0601 WIN32_LEAN_AND_MEAN)
else()
	target_compile_definitions(hx_c_platform PRIVATE
		_GNU_SOURCE _FILE_OFFSET_BITS=64)
	check_c_source_compiles("
		#include <stdint.h>
		int64_t value;
		int main(void) {
			return (int)__atomic_fetch_add(
				&value, 1, __ATOMIC_SEQ_CST);
		}" HX_OS_ATOMICS_WITHOUT_LIBRARY)
	if(NOT HX_OS_ATOMICS_WITHOUT_LIBRARY)
		find_library(HX_OS_ATOMIC_LIBRARY atomic)
		if(NOT HX_OS_ATOMIC_LIBRARY)
			message(FATAL_ERROR "64-bit atomics need libatomic")
		endif()
		target_link_libraries(hx_c_platform PRIVATE
			atomic)
	endif()
endif()
if(HX_OS_HAVE_ICONV)
	if(HX_OS_ICONV_SYSTEM)
		target_link_libraries(hx_c_platform PRIVATE Iconv::Iconv)
	else()
		target_sources(hx_c_platform PRIVATE
			$<TARGET_OBJECTS:hx_iconv_impl>)
		target_include_directories(hx_c_platform PRIVATE
			"${hx_iconv_generated}")
		target_compile_definitions(hx_c_platform PRIVATE
			${hx_iconv_aliases})
	endif()
endif()
hx_apply_warnings(hx_c_platform)
if(HX_OS_SANITIZER AND NOT MSVC)
	target_compile_options(hx_c_platform PUBLIC
		-fsanitize=${HX_OS_SANITIZER} -fno-omit-frame-pointer)
	target_link_options(hx_c_platform PUBLIC -fsanitize=${HX_OS_SANITIZER})
endif()

if(HX_OS_BUILD_TESTS)
	enable_testing()
	add_subdirectory(tests)
endif()
if(HX_OS_BUILD_EXAMPLES)
	add_subdirectory(examples)
endif()
if(HX_OS_BUILD_BENCHMARKS)
	add_subdirectory(benchmarks)
endif()

install(TARGETS hx_c_platform EXPORT hx_c_platformTargets
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
	LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
	ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(TARGETS hx_c_platform_cpp EXPORT hx_c_platformTargets)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES README.md README.zh-CN.md THIRD_PARTY_NOTICES.md
	THIRD_PARTY_NOTICES.zh-CN.md DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(DIRECTORY docs/ DESTINATION ${CMAKE_INSTALL_DOCDIR}/docs)
install(DIRECTORY licenses/ DESTINATION ${CMAKE_INSTALL_DOCDIR}/licenses)
install(EXPORT hx_c_platformTargets NAMESPACE hx_c_platform::
	DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/hx_c_platform)
configure_package_config_file(cmake/hx_c_platformConfig.cmake.in
	${CMAKE_CURRENT_BINARY_DIR}/hx_c_platformConfig.cmake
	INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/hx_c_platform)
write_basic_package_version_file(
	${CMAKE_CURRENT_BINARY_DIR}/hx_c_platformConfigVersion.cmake
	VERSION ${PROJECT_VERSION} COMPATIBILITY SameMinorVersion)
install(FILES
	${CMAKE_CURRENT_BINARY_DIR}/hx_c_platformConfig.cmake
	${CMAKE_CURRENT_BINARY_DIR}/hx_c_platformConfigVersion.cmake
	DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/hx_c_platform)

set(HX_OS_PC_LIBS_PRIVATE "")
if(HX_OS_ENABLE_LIBRARY OR HX_OS_ENABLE_PATHS)
	foreach(library IN LISTS CMAKE_DL_LIBS)
		string(APPEND HX_OS_PC_LIBS_PRIVATE " -l${library}")
	endforeach()
endif()
set(HX_OS_PC_DEFINITIONS "")
if(WIN32)
	if(HX_OS_ENABLE_PATHS)
		string(APPEND HX_OS_PC_LIBS_PRIVATE " -lshell32 -lole32 -luuid")
	endif()
	if(HX_OS_ENABLE_NETWORK)
		string(APPEND HX_OS_PC_LIBS_PRIVATE " -lws2_32")
	endif()
	if(HX_OS_ENABLE_SYSINFO)
		string(APPEND HX_OS_PC_LIBS_PRIVATE " -lpsapi")
	endif()
else()
	string(APPEND HX_OS_PC_LIBS_PRIVATE " -pthread")
endif()
if(HX_OS_ATOMIC_LIBRARY)
	string(APPEND HX_OS_PC_LIBS_PRIVATE " -latomic")
endif()
if(HX_OS_NEED_RT)
	string(APPEND HX_OS_PC_LIBS_PRIVATE " -lrt")
endif()
if(HX_OS_ICONV_SYSTEM AND NOT Iconv_IS_BUILT_IN)
	string(APPEND HX_OS_PC_LIBS_PRIVATE " -liconv")
endif()
if(BUILD_SHARED_LIBS)
	set(HX_OS_PC_DEFINITIONS "-DHX_OS_SHARED")
endif()
file(RELATIVE_PATH HX_OS_PC_PREFIX_FROM_FILE
	"${CMAKE_INSTALL_FULL_LIBDIR}/pkgconfig" "${CMAKE_INSTALL_PREFIX}")
if(IS_ABSOLUTE "${CMAKE_INSTALL_LIBDIR}")
	set(HX_OS_PC_LIBDIR "${CMAKE_INSTALL_LIBDIR}")
else()
	set(HX_OS_PC_LIBDIR "\${prefix}/${CMAKE_INSTALL_LIBDIR}")
endif()
if(IS_ABSOLUTE "${CMAKE_INSTALL_INCLUDEDIR}")
	set(HX_OS_PC_INCLUDEDIR "${CMAKE_INSTALL_INCLUDEDIR}")
else()
	set(HX_OS_PC_INCLUDEDIR "\${prefix}/${CMAKE_INSTALL_INCLUDEDIR}")
endif()

configure_file(cmake/hx_c_platform.pc.in hx_c_platform.pc @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/hx_c_platform.pc"
	DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
