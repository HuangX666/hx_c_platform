cmake_minimum_required(VERSION 3.16)
project(hx_c_platform VERSION 0.1.0 LANGUAGES C)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CheckSymbolExists)
include(CheckCSourceCompiles)
include(cmake/HxWarnings.cmake)
include(cmake/HxFormat.cmake)
include(cmake/HxDependencySources.cmake)

option(BUILD_SHARED_LIBS "Build shared library" OFF)
option(HX_OS_BUILD_TESTS "Build local maintainer tests (not distributed)" OFF)
option(HX_OS_BUILD_EXAMPLES "Build standalone C examples" OFF)
option(HX_OS_BUILD_BENCHMARKS "Build development microbenchmarks" OFF)
option(HX_OS_TEST_FORMAT_SYNTAX "Test optional host format generator" OFF)
mark_as_advanced(HX_OS_BUILD_TESTS HX_OS_TEST_FORMAT_SYNTAX)

if(HX_OS_BUILD_TESTS AND
	NOT EXISTS "${PROJECT_SOURCE_DIR}/tests/CMakeLists.txt")
	message(FATAL_ERROR
		"Local maintainer tests are not distributed. "
		"Use -DHX_OS_BUILD_TESTS=OFF and -DHX_OS_BUILD_EXAMPLES=ON.")
endif()

option(HX_OS_ENABLE_NETWORK "Build native networking" ON)
option(HX_OS_ENABLE_KCP "Build bounded reliable UDP protocol adapter" ON)
option(HX_OS_ENABLE_IPC "Build local full-duplex message endpoints" ON)
option(HX_OS_ENABLE_LOOP "Build asynchronous event loop" ON)

option(HX_OS_ENABLE_THREAD_POOL "Build bounded task pools" ON)
option(HX_OS_ENABLE_DELAYED_TASKS "Build bounded delayed pool tasks" ON)

option(HX_OS_ENABLE_MEMORY_POOLS "Build memory pools and arena" ON)

option(HX_OS_ENABLE_ICONV "Build legacy text encoding adapter" OFF)
option(HX_OS_ICONV_USE_SYSTEM "Use the platform iconv provider" OFF)

option(HX_OS_ENABLE_CONFIG "Build configuration file parsers" ON)

option(HX_OS_ENABLE_CONTAINERS "Build generic C containers" ON)
option(HX_OS_ENABLE_QUEUES "Build bounded thread and concurrent ring queues" ON)

option(HX_OS_ENABLE_COROUTINES "Build bounded cooperative coroutines" ON)
option(HX_OS_ENABLE_STACKFUL_COROUTINES "Build native stack switching" ON)
option(HX_OS_ENABLE_COROUTINE_IO "Build coroutine I/O adapters" ON)

option(HX_OS_ENABLE_VARIANT "Build owned variant values and custom types" ON)

option(HX_OS_ENABLE_FORMAT "Build typed UTF-8 brace formatting and printing" ON)

option(HX_OS_ENABLE_SYSINFO "Build native system information" ON)

option(HX_OS_ENABLE_LIBRARY "Build native dynamic library loading" ON)

option(HX_OS_ENABLE_FILE_UTILS "Build file and path conveniences" ON)
option(HX_OS_ENABLE_STREAM "Build binary streams and numeric I/O" ON)
option(HX_OS_ENABLE_ENCODING "Build Base64 and hexadecimal helpers" ON)
option(HX_OS_ENABLE_LINE_READER "Build bounded UTF-8 line readers" ON)
option(HX_OS_ENABLE_BUFFERED_FILE "Build bounded background file writers" ON)

option(HX_OS_ENABLE_ENV "Build UTF-8 environment access" ON)

option(HX_OS_ENABLE_PATHS "Build special path queries" ON)

option(HX_OS_ENABLE_THREAD_EX "Build thread options, names and semaphore" ON)

option(HX_OS_ENABLE_PIPE "Build synchronous and asynchronous pipes" ON)

option(HX_OS_ENABLE_NET_OPTIONS
	"Build socket options, peer queries and IP interface snapshots" ON)

option(HX_OS_ENABLE_POSITIONED_IO "Build position-independent file I/O" ON)

option(HX_OS_ENABLE_FILE_INFO
	"Build extended file and directory information" ON)
option(HX_OS_ENABLE_BOM "Build Unicode BOM helpers" ON)

option(HX_OS_ENABLE_TEXT_NUMBER "Build public numeric text conversion" ON)

option(HX_OS_ENABLE_CONFIG_SERIALIZE "Build configuration serialization" ON)
option(HX_OS_ENABLE_CODEC "Build schema-based object serialization" ON)

option(HX_OS_ENABLE_TIMER "Build periodic loop timers" ON)

option(HX_OS_ENABLE_RANDOM "Build secure system randomness and UUID v4" ON)
option(HX_OS_ENABLE_CRYPTO "Build OpenSSL data cryptography" ON)
option(HX_OS_ENABLE_PERSISTENT_QUEUE "Build SQLite message storage" ON)
option(HX_OS_ENABLE_TEMP "Build temporary file and directory owners" ON)
option(HX_OS_ENABLE_SERIAL "Build native serial byte I/O" ON)
option(HX_OS_ENABLE_SHARED_MEMORY "Build named shared mappings" ON)
option(HX_OS_ENABLE_CLI "Build command-line option parsing" ON)
option(HX_OS_ENABLE_URL "Build URL and query tools" ON)
option(HX_OS_ENABLE_HTTP "Build libcurl transfer conveniences" ON)
option(HX_OS_ENABLE_UNICODE "Build utf8proc transformations" ON)
option(HX_OS_ENABLE_REGEX "Build the bounded C99 regex engine" ON)
option(HX_OS_ENABLE_ONCE "Build independent one-time initialization" ON)
option(HX_OS_ENABLE_FRAME "Build bounded length-prefixed messages" ON)
option(HX_OS_ENABLE_TLS "Build full-duplex TLS endpoints" ON)
option(HX_OS_ENABLE_COMPRESSION "Build memory compression helpers" ON)
option(HX_OS_ENABLE_ARCHIVE "Build file and directory archives" ON)
option(HX_OS_ENABLE_7ZIP "Build the optional 7-Zip DLL adapter" OFF)

option(HX_OS_ENABLE_VIRTUAL_MEMORY "Build pages and file mappings" ON)

option(HX_OS_ENABLE_FILE_EXTRA "Build range locks, times and permissions" ON)

option(HX_OS_ENABLE_WATCH "Build bounded native directory monitoring" ON)
option(HX_OS_ENABLE_LOOP_FD "Build POSIX descriptor readiness integration" ON)

option(HX_OS_ENABLE_SYSTEM_UTILS "Build process and CPU identity queries" ON)
option(HX_OS_ENABLE_PROCESS "Build explicit child process control" ON)

option(HX_OS_ENABLE_LOCAL_SOCKETS "Build pathname local stream sockets" ON)
option(HX_OS_ENABLE_DUPLEX_PIPE "Build synchronous duplex socket pairs" ON)

option(HX_OS_ENABLE_REGISTRY "Build Windows registry access" ON)

option(HX_OS_ENABLE_WINDOWS_VERSION "Build Windows version resource queries" ON)
option(HX_OS_ENABLE_WINDOWS_DESKTOP "Build Windows window and GPU helpers" ON)

option(HX_OS_ENABLE_NATIVE_INTEROP "Build borrowed native object queries" ON)

option(HX_OS_ENABLE_SLEEP_INHIBIT "Build native idle-sleep requests" ON)
option(HX_OS_ENABLE_LINUX_DESKTOP "Build optional desktop power requests" ON)
option(HX_OS_ENABLE_PERFORMANCE_REQUEST "Build native execution activities" ON)

set(HX_OS_HAVE_LINUX_DESKTOP 0)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT ANDROID
	AND HX_OS_ENABLE_LINUX_DESKTOP AND HX_OS_ENABLE_SLEEP_INHIBIT)
	set(HX_OS_HAVE_LINUX_DESKTOP 1)
endif()

option(HX_OS_ENABLE_OBFUSCATION "Build reversible string obfuscation" ON)

option(HX_OS_ENABLE_CALENDAR "Build locale-independent calendar helpers" ON)
option(HX_OS_ENABLE_TEXT_LOOKUP "Build bounded translation tables" ON)
option(HX_OS_ENABLE_BITSTREAM "Build bounded bit stream cursors" ON)

option(HX_OS_ENABLE_SIGNALS "Build synchronous signals and parameter packs" ON)
option(HX_OS_ENABLE_PARAMETERS "Build bounded growable parameter packs" ON)
option(HX_OS_ENABLE_PREPROCESSOR "Build bounded runtime text preprocessing" ON)

option(HX_OS_ENABLE_SETTINGS
	"Build mutable settings and immutable snapshots" ON)

option(HX_OS_ENABLE_PROFILER "Build bounded interval profiling trees" ON)

option(HX_OS_ENABLE_WIDE_MATH "Build exact wide integer conversion" ON)
option(HX_OS_ENABLE_PRECISE_WAIT "Build explicit bounded spin waits" ON)
option(HX_OS_ENABLE_LEXER "Build bounded UTF-8 tokenization" ON)

option(HX_OS_ENABLE_PROCEDURES "Build named synchronous procedures" ON)

set(HX_OS_ICONV_SOURCE_DIR "" CACHE PATH "Unmodified GNU libiconv source")
set(HX_OS_SANITIZER "" CACHE STRING "address,undefined or thread")

# Enable C++ only for optional validation/examples, at their common parent.
if(HX_OS_BUILD_TESTS OR HX_OS_BUILD_EXAMPLES)
	include(CheckLanguage)

	check_language(CXX)

	if(CMAKE_CXX_COMPILER)
		enable_language(CXX)
		include(cmake/HxCpp20.cmake)

		hx_check_cpp20()
	endif()
endif()

if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
	if("arm64" IN_LIST CMAKE_OSX_ARCHITECTURES OR
		CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
		set(hx_apple_minimum "11.0")
	else()
		set(hx_apple_minimum "10.13")
	endif()

	set(CMAKE_OSX_DEPLOYMENT_TARGET "${hx_apple_minimum}" CACHE STRING
		"Minimum macOS deployment version" FORCE)
endif()

find_package(Threads REQUIRED)

set(HX_OS_PLATFORM_NAME "${CMAKE_SYSTEM_NAME}")
set(hx_platform_sources)
set(hx_private_include)
set(HX_OS_HAVE_PTHREAD_TRYJOIN 0)
set(HX_OS_HAVE_TIMED_JOIN 1)
set(HX_OS_HAVE_NATIVE_PROCESS 1)

if(ANDROID)
	set(hx_android_api "${CMAKE_SYSTEM_VERSION}")

	if(DEFINED ANDROID_PLATFORM_LEVEL)
		set(hx_android_api "${ANDROID_PLATFORM_LEVEL}")
	endif()

	if(hx_android_api LESS 21)
		message(FATAL_ERROR "Android API 21 or newer is required")
	endif()

	if(hx_android_api LESS 28)
		set(HX_OS_HAVE_NATIVE_PROCESS 0)
	endif()
endif()

set(HX_OS_NEED_RT 0)

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
	list(APPEND hx_platform_sources src/windows/thread_native.c
		src/windows/file_native.c)

	if(HX_OS_ENABLE_LOOP)
		list(APPEND hx_platform_sources src/windows/loop_native.c)
	endif()

	set(hx_private_include src/windows)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR ANDROID OR
	CMAKE_SYSTEM_NAME STREQUAL "Darwin")
	list(APPEND hx_platform_sources src/posix/thread_native.c
		src/posix/file_native.c)

	if(HX_OS_ENABLE_LOOP)
		list(APPEND hx_platform_sources src/posix/loop_io.c)

		if(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR ANDROID)
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
	if(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR ANDROID)
		check_symbol_exists(explicit_bzero string.h
			HX_OS_HAVE_EXPLICIT_BZERO)
		check_symbol_exists(getauxval sys/auxv.h HX_OS_HAVE_GETAUXVAL)
		check_symbol_exists(pthread_condattr_setclock pthread.h
			HX_OS_HAVE_COND_CLOCK)
		if(NOT HX_OS_HAVE_COND_CLOCK)
			message(FATAL_ERROR
				"Monotonic pthread conditions required")
		endif()

		if(NOT HX_OS_HAVE_PTHREAD_TRYJOIN AND NOT ANDROID)
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
	if(NOT HX_OS_ICONV_SOURCE_DIR AND NOT HX_OS_ICONV_USE_SYSTEM)
		include(cmake/HxDependencySources.cmake)
		hx_dependency_source(iconv HX_OS_ICONV_SOURCE_DIR)
	endif()
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
set(HX_OS_HAVE_LOCAL_PRINTF 1)

if(NOT WIN32)
	set(CMAKE_REQUIRED_DEFINITIONS -D_GNU_SOURCE)
	check_symbol_exists(strtod_l "stdlib.h;locale.h" HX_OS_HAS_STRTOD_L)
	check_symbol_exists(newlocale locale.h HX_OS_HAS_NEWLOCALE)
	check_symbol_exists(freelocale locale.h HX_OS_HAS_FREELOCALE)
	check_symbol_exists(uselocale locale.h HX_OS_HAS_USELOCALE)
	unset(CMAKE_REQUIRED_DEFINITIONS)

	if(NOT HX_OS_HAS_STRTOD_L OR NOT HX_OS_HAS_NEWLOCALE OR
		NOT HX_OS_HAS_FREELOCALE)
		set(HX_OS_HAVE_LOCAL_STRTOD 0)
	endif()

	if(NOT HX_OS_HAS_USELOCALE OR NOT HX_OS_HAS_NEWLOCALE OR
		NOT HX_OS_HAS_FREELOCALE)
		set(HX_OS_HAVE_LOCAL_PRINTF 0)
	endif()
endif()

include(cmake/ConfigParsers.cmake)
set(HX_OS_HAVE_TLS 0)
if(HX_OS_ENABLE_TLS AND HX_OS_ENABLE_CRYPTO AND HX_OS_ENABLE_NETWORK
		AND HX_OS_ENABLE_LOOP AND HX_OS_ENABLE_FILE_UTILS)
	set(HX_OS_HAVE_TLS 1)
endif()

set(HX_OS_HAVE_TEMP 0)
if(HX_OS_ENABLE_TEMP AND HX_OS_ENABLE_RANDOM AND HX_OS_ENABLE_FILE_UTILS)
	set(HX_OS_HAVE_TEMP 1)
endif()
set(HX_OS_HAVE_SHARED_MEMORY 0)
if(HX_OS_ENABLE_SHARED_MEMORY AND HX_OS_ENABLE_VIRTUAL_MEMORY)
	set(HX_OS_HAVE_SHARED_MEMORY 1)
endif()
set(HX_OS_HAVE_COMPRESSION ${HX_OS_ENABLE_COMPRESSION})
set(HX_OS_HAVE_ARCHIVE 0)
set(HX_OS_HAVE_7ZIP 0)
if(HX_OS_ENABLE_ARCHIVE AND HX_OS_ENABLE_FILE_UTILS
		AND HX_OS_ENABLE_CONTAINERS)
	set(HX_OS_HAVE_ARCHIVE 1)
endif()
if(HX_OS_ENABLE_7ZIP AND HX_OS_HAVE_ARCHIVE AND HX_OS_ENABLE_LIBRARY
		AND HX_OS_ENABLE_PATHS)
	set(HX_OS_HAVE_7ZIP 1)
endif()
set(HX_OS_HAVE_OPENSSL 0)
if(HX_OS_ENABLE_CRYPTO OR HX_OS_HAVE_ARCHIVE OR HX_OS_ENABLE_HTTP
		OR HX_OS_ENABLE_URL)
	set(HX_OS_HAVE_OPENSSL 1)
endif()

include(cmake/HxCrypto.cmake)
include(cmake/HxArchive.cmake)
include(cmake/HxSqlite.cmake)
include(cmake/HxTransferSdk.cmake)

if(NOT HX_OS_ENABLE_COROUTINES)
	set(HX_OS_ENABLE_STACKFUL_COROUTINES OFF)
	set(HX_OS_ENABLE_COROUTINE_IO OFF)
endif()

if(NOT HX_OS_ENABLE_LOOP OR NOT HX_OS_ENABLE_NETWORK)
	set(HX_OS_ENABLE_COROUTINE_IO OFF)
endif()

set(HX_OS_HAVE_IPC 0)

if(HX_OS_ENABLE_IPC AND HX_OS_ENABLE_NETWORK AND HX_OS_ENABLE_LOOP
		AND HX_OS_ENABLE_RANDOM)
	set(HX_OS_HAVE_IPC 1)
endif()

if(NOT HX_OS_ENABLE_STREAM)
	set(HX_OS_ENABLE_LINE_READER OFF)
endif()

configure_file(cmake/hx_os_config.h.in
	${CMAKE_CURRENT_BINARY_DIR}/private/hx_os_config.h)

add_library(hx_c_platform
	src/base.c src/atomic.c src/time.c src/unicode.c src/thread.c
	src/convenience.c src/lexer.c src/parser.c src/preprocess.c
	src/wide.c src/wait.c src/string.c
	src/string_utils.c src/format.c src/file.c src/text.c src/optional.c
	src/bytes.c src/random_tools.c src/algorithm_extra.c src/bloom.c
	src/search.c src/compression.c src/cli.c src/url.c src/http.c
	src/temp.c src/serial.c src/shared_memory.c
	src/regex.c src/regex_parse.c src/regex_compile.c src/regex_match.c
	src/stdio.c src/text_wide.c src/frame.c
	src/stream.c src/stream_number.c src/line_reader.c src/varint.c
	src/encoding.c src/text_utils.c src/unicode_tools.c
	src/control.c src/cancel.c src/coord.c src/once.c
	src/buffered_file.c
	src/config.c src/config_values.c src/config_file.c src/config_ini.c
	src/containers.c src/variant.c src/file_search.c
	src/queue.c src/persistent_queue.c
	src/ring.c src/bitset.c src/cache.c src/algorithm.c
	src/sysinfo.c src/library.c src/path.c src/env.c src/paths.c
	src/thread_extra.c src/pipe.c src/file_at.c src/net_options.c
	src/net_interfaces.c
	src/file_info.c src/bom.c src/number.c src/config_serialize.c
	src/config_build.c src/codec.c
	src/timer.c src/util.c src/uuid.c src/vm.c src/file_extra.c
	src/crypto.c src/crypto_digest.c src/crypto_incremental.c
	src/crypto_cipher.c src/crypto_text.c
	src/crypto_file.c
	src/crypto_key.c
	src/tls_common.c src/tls.c src/tls_wait.c src/tls_io.c
	src/tls_credentials.c src/tls_roots.c src/tls_stub.c
	src/archive.c src/archive_path.c src/archive_io.c
	src/archive_pack.c src/archive_read.c src/archive_decode.c
	src/archive_7zip.c
	src/watch.c src/loop_fd.c
	src/system.c src/cpu_features.c src/time_posix.c
	src/process.c src/process_capture.c src/local.c src/duplex_pipe.c
	src/registry.c src/windows_util.c src/native_interop.c
	src/power.c src/obfuscate.c
	src/calendar.c src/filename.c src/text_lookup.c src/bitstream.c
	src/calldata.c src/params.c src/dispatch.c src/signal.c src/procedure.c
	src/signal_arguments.c
	src/settings.c
	src/profiler.c
	${hx_parser_sources} ${hx_parser_objects}
	${hx_platform_sources})

if(WIN32)
	target_sources(hx_c_platform PRIVATE src/windows/serial_native.c
		src/windows/shared_memory_native.c)
else()
	target_sources(hx_c_platform PRIVATE src/posix/serial_native.c
		src/posix/shared_memory_native.c)
endif()

if(HX_OS_ENABLE_FORMAT)
	target_sources(hx_c_platform PRIVATE src/format_value.c)

	if(WIN32)
		target_sources(hx_c_platform PRIVATE
			src/windows/format_native.c)
	else()
		target_sources(hx_c_platform PRIVATE src/posix/format_native.c)
	endif()
endif()

if(HX_OS_ENABLE_COROUTINES)
	target_sources(hx_c_platform PRIVATE src/coroutine.c
		src/coroutine_channel.c src/coroutine_io.c
		src/coroutine_stream.c src/coroutine_sync.c)

	if(HX_OS_ENABLE_STACKFUL_COROUTINES)
		if(WIN32)
			target_sources(hx_c_platform PRIVATE
				src/windows/coroutine_native.c)
		else()
			enable_language(ASM)
			target_sources(hx_c_platform PRIVATE
				src/posix/coroutine_native.c
				src/posix/coroutine_cpu.S)
		endif()
	endif()
else()
	target_sources(hx_c_platform PRIVATE src/coroutine_disabled.c)
endif()

if(HX_OS_ENABLE_PREPROCESSOR AND HX_OS_ENABLE_LEXER
	AND HX_OS_ENABLE_PARAMETERS)
	target_sources(hx_c_platform PRIVATE
		src/preprocess_engine.c src/preprocess_expr.c)
endif()

if(HX_OS_ENABLE_PROFILER)
	target_sources(hx_c_platform PRIVATE
		src/profiler_dump.c src/profiler_snapshot.c)
	if(NOT WIN32)
		target_link_libraries(hx_c_platform PRIVATE m)
	endif()
else()
	target_sources(hx_c_platform PRIVATE src/profiler_disabled.c)
endif()

if(HX_OS_ENABLE_SETTINGS)
	# Mutable settings are independent of optional config integration.
	target_sources(hx_c_platform PRIVATE src/settings_value.c
		src/settings_config.c)
else()
	target_sources(hx_c_platform PRIVATE src/settings_disabled.c)
endif()

if(HX_OS_ENABLE_CALENDAR)
	# Timezone discovery is selected separately from common formatting.
	if(WIN32)
		target_sources(hx_c_platform PRIVATE
			src/windows/calendar_native.c)
	else()
		target_sources(hx_c_platform PRIVATE
			src/posix/calendar_native.c)
	endif()
endif()

if(HX_OS_ENABLE_PROCESS AND HX_OS_HAVE_NATIVE_PROCESS)
	if(WIN32)
		target_sources(hx_c_platform PRIVATE
			src/windows/process_native.c)
	else()
		target_sources(hx_c_platform PRIVATE src/posix/process_native.c)
	endif()
endif()

if(WIN32 AND HX_OS_ENABLE_REGISTRY)
	target_link_libraries(hx_c_platform PRIVATE advapi32)
endif()

if(WIN32 AND HX_OS_ENABLE_WINDOWS_VERSION)
	target_sources(hx_c_platform PRIVATE src/windows/version_resource.c)
	target_link_libraries(hx_c_platform PRIVATE version)
endif()

if(WIN32 AND HX_OS_ENABLE_WINDOWS_DESKTOP)
	target_sources(hx_c_platform PRIVATE src/windows/desktop_info.c)
	target_link_libraries(hx_c_platform PRIVATE user32 dxgi dxguid)
endif()

if(HX_OS_ENABLE_SLEEP_INHIBIT OR HX_OS_ENABLE_PERFORMANCE_REQUEST)
	if(WIN32)
		target_sources(hx_c_platform PRIVATE src/windows/power_native.c)
	elseif(APPLE)
		target_sources(hx_c_platform PRIVATE src/apple/power_native.c)
		target_link_libraries(hx_c_platform PRIVATE
			"-framework IOKit" "-framework CoreFoundation")

		if(HX_OS_ENABLE_PERFORMANCE_REQUEST)
			enable_language(OBJC)
			target_sources(hx_c_platform PRIVATE
				src/apple/power_activity.m)
			set_source_files_properties(src/apple/power_activity.m
				PROPERTIES COMPILE_OPTIONS
				"-fno-objc-arc;-fobjc-exceptions")
			target_compile_options(hx_c_platform PRIVATE
				"$<$<COMPILE_LANGUAGE:OBJC>:-Wall;-Wextra>")

			if(NOT DEFINED CMAKE_COMPILE_WARNING_AS_ERROR OR
				CMAKE_COMPILE_WARNING_AS_ERROR)
				target_compile_options(hx_c_platform PRIVATE
					"$<$<COMPILE_LANGUAGE:OBJC>:-Werror>")
			endif()

			set_target_properties(hx_c_platform PROPERTIES
				OBJC_STANDARD 99 OBJC_STANDARD_REQUIRED YES
				OBJC_EXTENSIONS NO)
			target_link_libraries(hx_c_platform PRIVATE
				"-framework Foundation" objc)
		endif()
	elseif(HX_OS_HAVE_LINUX_DESKTOP)
		target_sources(hx_c_platform PRIVATE src/linux/power_native.c)
	endif()
endif()

if(HX_OS_ENABLE_SYSTEM_UTILS)
	if(WIN32)
		target_sources(hx_c_platform PRIVATE
			src/windows/system_native.c)
		target_link_libraries(hx_c_platform PRIVATE shell32)
	else()
		target_sources(hx_c_platform PRIVATE src/posix/system_native.c)
	endif()
endif()

if(HX_OS_ENABLE_WATCH)
	if(WIN32)
		target_sources(hx_c_platform PRIVATE src/windows/watch_native.c)
	elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR ANDROID)
		target_sources(hx_c_platform PRIVATE src/linux/watch_native.c)
	else()
		target_sources(hx_c_platform PRIVATE src/apple/watch_native.c)
		target_link_libraries(hx_c_platform PRIVATE
			"-framework CoreServices" "-framework CoreFoundation")
	endif()
endif()

if(HX_OS_ENABLE_RANDOM)
	if(WIN32)
		target_sources(hx_c_platform PRIVATE
			src/windows/random_native.c)
		target_link_libraries(hx_c_platform PRIVATE advapi32)
	else()
		target_sources(hx_c_platform PRIVATE src/posix/random_native.c)
	endif()
endif()

if(HX_OS_ENABLE_VIRTUAL_MEMORY)
	if(WIN32)
		target_sources(hx_c_platform PRIVATE src/windows/vm_native.c)
	else()
		target_sources(hx_c_platform PRIVATE src/posix/vm_native.c)
	endif()
endif()

if(HX_OS_ENABLE_FILE_EXTRA)
	if(WIN32)
		target_sources(hx_c_platform PRIVATE
			src/windows/file_extra_native.c)
	else()
		target_sources(hx_c_platform PRIVATE
			src/posix/file_extra_native.c)
	endif()
endif()

if(HX_OS_ENABLE_SYSINFO)
	if(WIN32)
		target_sources(hx_c_platform PRIVATE
			src/windows/sysinfo_native.c)
		target_link_libraries(hx_c_platform PRIVATE psapi)
	else()
		target_sources(hx_c_platform PRIVATE src/posix/sysinfo_native.c)
	endif()
endif()

if(HX_OS_ENABLE_PIPE)
	if(WIN32)
		target_sources(hx_c_platform PRIVATE src/windows/pipe_native.c)
	else()
		target_sources(hx_c_platform PRIVATE src/posix/pipe_native.c)
	endif()
endif()

if(HX_OS_ENABLE_POSITIONED_IO)
	if(WIN32)
		target_sources(hx_c_platform PRIVATE
			src/windows/file_at_native.c)
	else()
		target_sources(hx_c_platform PRIVATE src/posix/file_at_native.c)
	endif()
endif()

if(HX_OS_ENABLE_LIBRARY OR HX_OS_ENABLE_PATHS OR HX_OS_HAVE_LINUX_DESKTOP)
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
			src/windows/file_utils_native.c
			src/windows/file_stage_native.c)
		target_link_libraries(hx_c_platform PRIVATE advapi32)
	else()
		target_sources(hx_c_platform PRIVATE
			src/posix/file_utils_native.c
			src/posix/file_stage_native.c)
	endif()
else()
	target_sources(hx_c_platform PRIVATE src/file_utils_disabled.c)
endif()

if(HX_OS_ENABLE_CONTAINERS)
	include(cmake/HxDependencySources.cmake)
	hx_dependency_source(uthash hx_uthash_source)
	target_sources(hx_c_platform PRIVATE src/sequence.c src/map.c)
	target_include_directories(hx_c_platform SYSTEM PRIVATE
		"${hx_uthash_source}/src")

	install(FILES "${hx_uthash_source}/LICENSE"
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

if(HX_OS_ENABLE_NETWORK OR HX_OS_ENABLE_DUPLEX_PIPE)
	target_sources(hx_c_platform PRIVATE src/socket_native.c)
endif()

if(HX_OS_ENABLE_NETWORK AND HX_OS_ENABLE_NET_OPTIONS)
	if(WIN32)
		target_sources(hx_c_platform PRIVATE
			src/windows/net_interfaces_native.c)
		target_link_libraries(hx_c_platform PRIVATE iphlpapi)
	elseif(ANDROID)
		target_sources(hx_c_platform PRIVATE
			src/linux/net_interfaces_android.c)
	else()
		target_sources(hx_c_platform PRIVATE
			src/posix/net_interfaces_native.c)
	endif()
endif()

if(HX_OS_ENABLE_DUPLEX_PIPE)
	if(WIN32)
		target_sources(hx_c_platform PRIVATE
			src/windows/duplex_native.c)
	else()
		target_sources(hx_c_platform PRIVATE src/posix/duplex_native.c)
	endif()
endif()

if(HX_OS_ENABLE_LOOP)
	target_sources(hx_c_platform PRIVATE src/loop.c)
endif()

add_library(hx_c_platform::hx_c_platform ALIAS hx_c_platform)
target_sources(hx_c_platform PRIVATE src/kcp.c src/ipc.c)

if(HX_OS_HAVE_IPC)
	target_sources(hx_c_platform PRIVATE src/ipc_wire.c src/ipc_io.c)

	if(WIN32)
		target_sources(hx_c_platform PRIVATE src/windows/ipc_native.c)
		target_link_libraries(hx_c_platform PRIVATE advapi32)
	else()
		target_sources(hx_c_platform PRIVATE src/posix/ipc_native.c)
	endif()
endif()

if(HX_OS_ENABLE_KCP)
	include(cmake/HxDependencySources.cmake)
	hx_dependency_source(kcp hx_kcp_source)

	add_library(hx_kcp_vendor OBJECT src/kcp_vendor.c)
	set_target_properties(hx_kcp_vendor PROPERTIES
		C_STANDARD 99
		C_STANDARD_REQUIRED YES
		C_EXTENSIONS NO
		C_VISIBILITY_PRESET hidden
		POSITION_INDEPENDENT_CODE ON)
	target_include_directories(hx_kcp_vendor SYSTEM PRIVATE
		"${hx_kcp_source}")
	target_include_directories(hx_c_platform SYSTEM PRIVATE
		"${hx_kcp_source}")
	target_sources(hx_c_platform PRIVATE $<TARGET_OBJECTS:hx_kcp_vendor>)

	if(MSVC)
		target_compile_definitions(hx_kcp_vendor PRIVATE
			_CRT_SECURE_NO_WARNINGS)
	endif()

	if(HX_OS_SANITIZER AND NOT MSVC)
		target_compile_options(hx_kcp_vendor PRIVATE
			-fsanitize=${HX_OS_SANITIZER} -fno-omit-frame-pointer)
	endif()

	install(FILES "${hx_kcp_source}/LICENSE"
		DESTINATION ${CMAKE_INSTALL_DATADIR}/hx_c_platform/licenses
		RENAME kcp-LICENSE)
endif()

add_library(hx_c_platform_cpp INTERFACE)
add_library(hx_c_platform_native INTERFACE)
add_library(hx_c_platform::native ALIAS hx_c_platform_native)
set_target_properties(hx_c_platform_native PROPERTIES EXPORT_NAME native)
target_link_libraries(hx_c_platform_native INTERFACE hx_c_platform)
foreach(dependency sqlite openssl curl utf8proc archive zlib lzma zstd)
	if(TARGET hx_c_platform::${dependency})
		target_link_libraries(hx_c_platform_native INTERFACE
			hx_c_platform::${dependency})
	endif()
endforeach()
add_library(hx_c_platform::cpp ALIAS hx_c_platform_cpp)
set_target_properties(hx_c_platform_cpp PROPERTIES EXPORT_NAME cpp)
target_link_libraries(hx_c_platform_cpp INTERFACE hx_c_platform)
target_compile_features(hx_c_platform_cpp INTERFACE cxx_std_11)

add_library(hx_c_platform_cpp20 INTERFACE)
add_library(hx_c_platform::cpp20 ALIAS hx_c_platform_cpp20)
set_target_properties(hx_c_platform_cpp20 PROPERTIES EXPORT_NAME cpp20)
target_link_libraries(hx_c_platform_cpp20 INTERFACE hx_c_platform_cpp)
target_compile_features(hx_c_platform_cpp20 INTERFACE cxx_std_20)
set_target_properties(hx_c_platform PROPERTIES
	C_STANDARD 99
	C_STANDARD_REQUIRED YES
	C_EXTENSIONS NO
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
if(UNIX)
	target_link_libraries(hx_c_platform PRIVATE m)
endif()
hx_link_crypto(hx_c_platform)
hx_link_archive(hx_c_platform)
hx_link_transfer_sdk(hx_c_platform)
if(HX_OS_HAVE_SHARED_MEMORY AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
	find_library(HX_OS_SHM_RT_LIBRARY rt)
	if(HX_OS_SHM_RT_LIBRARY)
		target_link_libraries(hx_c_platform PRIVATE
			${HX_OS_SHM_RT_LIBRARY})
	endif()
endif()
if(WIN32 AND HX_OS_ENABLE_CLI)
	target_link_libraries(hx_c_platform PRIVATE shell32)
endif()
if(HX_OS_HAVE_PERSISTENT_QUEUE)
	target_link_libraries(hx_c_platform PRIVATE hx_c_platform_sqlite)
	if(APPLE)
		set_property(TARGET hx_c_platform APPEND PROPERTY INSTALL_RPATH
			"@loader_path")
	elseif(UNIX)
		set_property(TARGET hx_c_platform APPEND PROPERTY INSTALL_RPATH
			"$ORIGIN")
	endif()
endif()

if(HX_OS_NEED_RT)
	target_link_libraries(hx_c_platform PRIVATE rt)
endif()

if(BUILD_SHARED_LIBS)
	target_compile_definitions(hx_c_platform PUBLIC HX_OS_SHARED)
endif()

if(WIN32)
	if(HX_OS_ENABLE_NETWORK OR HX_OS_ENABLE_DUPLEX_PIPE)
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
include(cmake/HxSevenZip.cmake)

if(HX_OS_SANITIZER AND NOT MSVC)
	if(CMAKE_C_COMPILER_ID STREQUAL "GNU" AND
		HX_OS_SANITIZER STREQUAL "thread")
		# GCC TSan cannot model standalone fences. Keep its warning;
		# ordering tests use atomic pairs, not standalone fences.
		set_property(SOURCE src/atomic.c APPEND PROPERTY
			COMPILE_OPTIONS -Wno-error=tsan)
	endif()

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

	if(HX_OS_BUILD_TESTS AND HX_OS_EXTENDED_TESTS)
		add_subdirectory(tests/examples)
	endif()
endif()

if(HX_OS_BUILD_BENCHMARKS)
	add_subdirectory(benchmarks)
endif()

include(cmake/HxIde.cmake)

# Keep deployed test libraries beside their executables. Android 24+ uses
# RUNPATH; older native targets still use the runner's LD_LIBRARY_PATH.
if(ANDROID AND (BUILD_SHARED_LIBS OR HX_OS_HAVE_OPENSSL))
	set(hx_validation_dirs)

	if(HX_OS_BUILD_TESTS)
		list(APPEND hx_validation_dirs tests)
	endif()

	if(HX_OS_BUILD_EXAMPLES)
		list(APPEND hx_validation_dirs examples)
	endif()

	if(HX_OS_BUILD_BENCHMARKS)
		list(APPEND hx_validation_dirs benchmarks)
	endif()

	foreach(directory IN LISTS hx_validation_dirs)
		get_property(hx_validation_targets DIRECTORY
			"${PROJECT_SOURCE_DIR}/${directory}"
			PROPERTY BUILDSYSTEM_TARGETS)

		foreach(target IN LISTS hx_validation_targets)
			get_target_property(hx_validation_type ${target} TYPE)

			if(hx_validation_type STREQUAL "EXECUTABLE")
				target_link_options(${target} PRIVATE
					"-Wl,-rpath,$ORIGIN")
			endif()
		endforeach()
	endforeach()
endif()

install(TARGETS hx_c_platform EXPORT hx_c_platformTargets
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
	LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
	ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(TARGETS hx_c_platform_cpp EXPORT hx_c_platformTargets)
install(TARGETS hx_c_platform_native EXPORT hx_c_platformTargets)
install(TARGETS hx_c_platform_cpp20 EXPORT hx_c_platformTargets)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES README.md README.zh-CN.md THIRD_PARTY_NOTICES.md
	THIRD_PARTY_NOTICES.zh-CN.md DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(DIRECTORY docs/ DESTINATION ${CMAKE_INSTALL_DOCDIR}/docs
	PATTERN "local" EXCLUDE)
install(DIRECTORY examples/ DESTINATION ${CMAKE_INSTALL_DOCDIR}/examples
	FILES_MATCHING
	PATTERN "*.c" PATTERN "*.cpp" PATTERN "*.h"
	PATTERN "*.py" PATTERN "*.md" PATTERN "*.hxconf"
	PATTERN "__pycache__" EXCLUDE)
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
	cmake/HxFormat.cmake tools/hx_format_codegen.py
	${CMAKE_CURRENT_BINARY_DIR}/hx_c_platformConfig.cmake
	${CMAKE_CURRENT_BINARY_DIR}/hx_c_platformConfigVersion.cmake
	DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/hx_c_platform)

set(HX_OS_PC_LIBS_PRIVATE "")

if(HX_OS_HAVE_HTTP OR HX_OS_HAVE_URL)
	string(APPEND HX_OS_PC_LIBS_PRIVATE " -lcurl")
endif()
if(HX_OS_HAVE_UNICODE)
	string(APPEND HX_OS_PC_LIBS_PRIVATE " -lutf8proc")
endif()
if(HX_OS_HAVE_PERSISTENT_QUEUE)
	string(APPEND HX_OS_PC_LIBS_PRIVATE " -lhx_sqlite3")
endif()

if(APPLE AND HX_OS_HAVE_TLS)
	string(APPEND HX_OS_PC_LIBS_PRIVATE
		" -framework Security -framework CoreFoundation")
endif()

if(WIN32 AND HX_OS_ENABLE_WINDOWS_VERSION)
	string(APPEND HX_OS_PC_LIBS_PRIVATE " -lversion")
endif()

if(WIN32 AND HX_OS_ENABLE_WINDOWS_DESKTOP)
	string(APPEND HX_OS_PC_LIBS_PRIVATE " -luser32 -ldxgi -ldxguid")
endif()

if(APPLE AND HX_OS_ENABLE_WATCH)
	string(APPEND HX_OS_PC_LIBS_PRIVATE
		" -framework CoreServices -framework CoreFoundation")
endif()

if(APPLE AND (HX_OS_ENABLE_SLEEP_INHIBIT OR HX_OS_ENABLE_PERFORMANCE_REQUEST))
	string(APPEND HX_OS_PC_LIBS_PRIVATE " -framework IOKit")

	if(NOT HX_OS_ENABLE_WATCH)
		string(APPEND HX_OS_PC_LIBS_PRIVATE
			" -framework CoreFoundation")
	endif()

	if(HX_OS_ENABLE_PERFORMANCE_REQUEST)
		string(APPEND HX_OS_PC_LIBS_PRIVATE
			" -framework Foundation -lobjc")
	endif()
endif()

if(HX_OS_ENABLE_LIBRARY OR HX_OS_ENABLE_PATHS OR HX_OS_HAVE_LINUX_DESKTOP)
	foreach(library IN LISTS CMAKE_DL_LIBS)
		string(APPEND HX_OS_PC_LIBS_PRIVATE " -l${library}")
	endforeach()
endif()

set(HX_OS_PC_DEFINITIONS "")

if(WIN32)
	if(HX_OS_ENABLE_PATHS)
		string(APPEND HX_OS_PC_LIBS_PRIVATE " -lshell32 -lole32 -luuid")
	endif()

	if(HX_OS_ENABLE_RANDOM OR HX_OS_ENABLE_REGISTRY
		OR HX_OS_ENABLE_FILE_UTILS)
		string(APPEND HX_OS_PC_LIBS_PRIVATE " -ladvapi32")
	endif()

	if(HX_OS_ENABLE_SYSTEM_UTILS AND NOT HX_OS_ENABLE_PATHS)
		string(APPEND HX_OS_PC_LIBS_PRIVATE " -lshell32")
	endif()

	if(HX_OS_ENABLE_NETWORK OR HX_OS_ENABLE_DUPLEX_PIPE)
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

if(HX_OS_NEED_RT OR (HX_OS_HAVE_SHARED_MEMORY AND HX_OS_SHM_RT_LIBRARY))
	string(APPEND HX_OS_PC_LIBS_PRIVATE " -lrt")
endif()

if(UNIX)
	string(APPEND HX_OS_PC_LIBS_PRIVATE " -lm")
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
