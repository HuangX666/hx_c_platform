cmake_minimum_required(VERSION 3.20)
project(hx_optional_dependencies LANGUAGES C)

include(ExternalProject)
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/HxDependencySources.cmake")

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
	set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/prefix" CACHE PATH
		"Private dependency installation" FORCE)
endif()

set(HX_DEPS_JOBS 4 CACHE STRING "Maximum OpenSSL build jobs")
set(HX_DEPS_OPENSSL_TARGET "" CACHE STRING "OpenSSL Configure target")
set(HX_DEPS_CONFIGURATION "Release" CACHE STRING
	"Compression SDK configuration (match the MSVC consumer CRT)")
set_property(CACHE HX_DEPS_CONFIGURATION PROPERTY STRINGS Release Debug)
if(NOT HX_DEPS_CONFIGURATION MATCHES "^(Release|Debug)$")
	message(FATAL_ERROR "HX_DEPS_CONFIGURATION must be Release or Debug")
endif()
option(HX_DEPS_OPENSSL_ASM "Use OpenSSL assembler optimizations" ON)
option(HX_DEPS_OPENSSL_SHARED "Build the OpenSSL shared runtime" ON)

if(NOT HX_DEPS_JOBS MATCHES "^[1-9][0-9]*$")
	message(FATAL_ERROR "HX_DEPS_JOBS must be a positive integer")
endif()

if(CMAKE_CROSSCOMPILING AND NOT HX_DEPS_OPENSSL_TARGET)
	message(FATAL_ERROR "Set HX_DEPS_OPENSSL_TARGET for cross compilation")
endif()

set(hx_common_args
	"-DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_INSTALL_PREFIX}"
	"-DCMAKE_INSTALL_LIBDIR:STRING=lib"
	"-DCMAKE_PREFIX_PATH:PATH=${CMAKE_INSTALL_PREFIX}"
	"-DCMAKE_BUILD_TYPE:STRING=${HX_DEPS_CONFIGURATION}"
	"-DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=ON"
	"-DBUILD_SHARED_LIBS:BOOL=OFF"
	"-DBUILD_TESTING:BOOL=OFF")
if(APPLE)
	list(APPEND hx_common_args
		"-DCMAKE_INSTALL_RPATH:STRING=@loader_path")
elseif(UNIX)
	list(APPEND hx_common_args "-DCMAKE_INSTALL_RPATH:STRING=$ORIGIN")
endif()
set(hx_cmake_build "${CMAKE_COMMAND}" --build <BINARY_DIR>
	--config "${HX_DEPS_CONFIGURATION}" --parallel "${HX_DEPS_JOBS}")
set(hx_cmake_install "${CMAKE_COMMAND}" --install <BINARY_DIR>
	--config "${HX_DEPS_CONFIGURATION}")
if(CMAKE_MAKE_PROGRAM)
	list(APPEND hx_common_args
		"-DCMAKE_MAKE_PROGRAM:FILEPATH=${CMAKE_MAKE_PROGRAM}")
endif()

if(CMAKE_CROSSCOMPILING)
	list(APPEND hx_common_args
		"-DCMAKE_FIND_ROOT_PATH:PATH=${CMAKE_INSTALL_PREFIX}")
endif()

if(CMAKE_TOOLCHAIN_FILE)
	list(APPEND hx_common_args
		"-DCMAKE_TOOLCHAIN_FILE:FILEPATH=${CMAKE_TOOLCHAIN_FILE}")
endif()

set(hx_forward CMAKE_SYSROOT ANDROID_ABI ANDROID_PLATFORM ANDROID_STL
	${CMAKE_TRY_COMPILE_PLATFORM_VARIABLES})
list(REMOVE_DUPLICATES hx_forward)
foreach(variable IN LISTS hx_forward)
	if(DEFINED ${variable})
		list(APPEND hx_common_args "-D${variable}=${${variable}}")
	endif()
endforeach()

foreach(name openssl zlib xz zstd libarchive)
	hx_dependency_source("${name}" ${name}_source)
endforeach()

foreach(name curl utf8proc)
	hx_dependency_source("${name}" ${name}_source)
endforeach()

find_package(Perl REQUIRED)
if(WIN32 AND MSVC)
	find_program(hx_make NAMES nmake)
	if(NOT hx_make)
		message(FATAL_ERROR "Run this builder from a Visual Studio "
			"Developer Command Prompt; nmake must be on PATH")
	endif()
	if(NOT HX_DEPS_OPENSSL_TARGET)
		if(CMAKE_SIZEOF_VOID_P EQUAL 4)
			set(HX_DEPS_OPENSSL_TARGET VC-WIN32)
		elseif(CMAKE_GENERATOR_PLATFORM STREQUAL "ARM64")
			message(FATAL_ERROR
				"Set HX_DEPS_OPENSSL_TARGET for Windows ARM64")
		else()
			set(HX_DEPS_OPENSSL_TARGET VC-WIN64A)
		endif()
	endif()
	set(hx_make_jobs /NOLOGO)
	if(HX_DEPS_OPENSSL_ASM)
		find_program(hx_nasm NAMES nasm)
		if(NOT hx_nasm)
			message(FATAL_ERROR "Install NASM or explicitly use "
				"HX_DEPS_OPENSSL_ASM=OFF for a portable build")
		endif()
	endif()
else()
	find_program(hx_make NAMES gmake make REQUIRED)
	set(hx_make_jobs "-j${HX_DEPS_JOBS}")
endif()

# Keep ENGINE ABI entry points: a host libssl can reference them even when
# this SDK does not build dynamically loaded engine modules.
set(hx_openssl_options no-tests no-module no-legacy
	"--prefix=${CMAKE_INSTALL_PREFIX}" --libdir=lib)
if(HX_DEPS_OPENSSL_SHARED)
	list(APPEND hx_openssl_options shared)
else()
	list(APPEND hx_openssl_options no-shared)
endif()

if(HX_DEPS_OPENSSL_SHARED)
	set(hx_openssl_static OFF)
else()
	set(hx_openssl_static ON)
endif()

if(NOT HX_DEPS_OPENSSL_ASM)
	list(APPEND hx_openssl_options no-asm)
endif()

set(hx_openssl_environment)
set(hx_openssl_target "${HX_DEPS_OPENSSL_TARGET}")
if(ANDROID)
	if(CMAKE_HOST_WIN32)
		message(FATAL_ERROR
			"Build the Android SDK in WSL/Linux with "
			"a Linux NDK; the core library can still "
			"be built on Windows.")
	endif()
	get_filename_component(hx_ndk_bin "${CMAKE_C_COMPILER}" DIRECTORY)
	list(APPEND hx_openssl_environment
		"ANDROID_NDK_ROOT=${CMAKE_ANDROID_NDK}"
		"PATH=${hx_ndk_bin}:$ENV{PATH}")
	set(hx_android_api "${CMAKE_SYSTEM_VERSION}")
	if(DEFINED ANDROID_PLATFORM_LEVEL)
		set(hx_android_api "${ANDROID_PLATFORM_LEVEL}")
	elseif(DEFINED CMAKE_ANDROID_API)
		set(hx_android_api "${CMAKE_ANDROID_API}")
	endif()
	if(hx_android_api LESS 21)
		message(FATAL_ERROR
			"The Android dependency SDK requires API 21+")
	endif()
	list(APPEND hx_openssl_options
		"-D__ANDROID_API__=${hx_android_api}")
	if(NOT hx_openssl_target MATCHES "^android-(arm|arm64|x86_64)$")
		message(FATAL_ERROR "Unsupported Android OpenSSL target")
	endif()
	set(hx_openssl_target "hx-${hx_openssl_target}")
	list(APPEND hx_openssl_options
		"--config=${CMAKE_CURRENT_LIST_DIR}/openssl-android.conf")
elseif(CMAKE_CROSSCOMPILING)
	list(APPEND hx_openssl_environment "CC=${CMAKE_C_COMPILER}"
		"AR=${CMAKE_AR}" "RANLIB=${CMAKE_RANLIB}")
	if(CMAKE_SYSROOT)
		list(APPEND hx_openssl_options "--sysroot=${CMAKE_SYSROOT}")
	endif()
endif()
set(hx_openssl_command "${CMAKE_COMMAND}" -E env
	${hx_openssl_environment})

ExternalProject_Add(hx_dep_build_openssl
	SOURCE_DIR "${openssl_source}"
	BINARY_DIR "${CMAKE_BINARY_DIR}/openssl"
	DOWNLOAD_COMMAND ""
	UPDATE_COMMAND ""
	CONFIGURE_COMMAND ${hx_openssl_command}
		"${PERL_EXECUTABLE}" <SOURCE_DIR>/Configure
		${hx_openssl_target} ${hx_openssl_options}
	BUILD_COMMAND ${hx_openssl_command} "${hx_make}" ${hx_make_jobs}
	INSTALL_COMMAND ${hx_openssl_command} "${hx_make}" install_sw)

ExternalProject_Add(hx_dep_build_zlib
	SOURCE_DIR "${zlib_source}"
	BINARY_DIR "${CMAKE_BINARY_DIR}/zlib"
	DOWNLOAD_COMMAND ""
	UPDATE_COMMAND ""
	CMAKE_ARGS ${hx_common_args}
		-DZLIB_BUILD_SHARED:BOOL=OFF
		-DZLIB_BUILD_STATIC:BOOL=ON
		-DZLIB_BUILD_TESTING:BOOL=OFF
	BUILD_COMMAND ${hx_cmake_build}
	INSTALL_COMMAND ${hx_cmake_install})

ExternalProject_Add(hx_dep_build_xz
	SOURCE_DIR "${xz_source}"
	BINARY_DIR "${CMAKE_BINARY_DIR}/xz"
	DOWNLOAD_COMMAND ""
	UPDATE_COMMAND ""
	CMAKE_ARGS ${hx_common_args}
		-DXZ_NLS:BOOL=OFF
		-DXZ_TOOL_XZ:BOOL=OFF
		-DXZ_TOOL_XZDEC:BOOL=OFF
		-DXZ_TOOL_LZMADEC:BOOL=OFF
		-DXZ_TOOL_LZMAINFO:BOOL=OFF
	BUILD_COMMAND ${hx_cmake_build}
	INSTALL_COMMAND ${hx_cmake_install})

ExternalProject_Add(hx_dep_build_zstd
	SOURCE_DIR "${zstd_source}/build/cmake"
	BINARY_DIR "${CMAKE_BINARY_DIR}/zstd"
	DOWNLOAD_COMMAND ""
	UPDATE_COMMAND ""
	CMAKE_ARGS ${hx_common_args}
		-DZSTD_BUILD_SHARED:BOOL=OFF
		-DZSTD_BUILD_STATIC:BOOL=ON
		-DZSTD_BUILD_TESTS:BOOL=OFF
		-DZSTD_BUILD_PROGRAMS:BOOL=OFF
	BUILD_COMMAND ${hx_cmake_build}
	INSTALL_COMMAND ${hx_cmake_install})

ExternalProject_Add(hx_dep_build_utf8proc
	SOURCE_DIR "${utf8proc_source}"
	BINARY_DIR "${CMAKE_BINARY_DIR}/utf8proc"
	DOWNLOAD_COMMAND "" UPDATE_COMMAND ""
	CMAKE_ARGS ${hx_common_args}
		-DBUILD_SHARED_LIBS:BOOL=ON
		-DUTF8PROC_ENABLE_TESTING:BOOL=OFF
	BUILD_COMMAND ${hx_cmake_build}
	INSTALL_COMMAND ${hx_cmake_install})

ExternalProject_Add(hx_dep_build_curl
	SOURCE_DIR "${curl_source}"
	BINARY_DIR "${CMAKE_BINARY_DIR}/curl"
	DOWNLOAD_COMMAND "" UPDATE_COMMAND ""
	DEPENDS hx_dep_build_openssl
	CMAKE_ARGS ${hx_common_args}
		-DBUILD_SHARED_LIBS:BOOL=ON
		-DBUILD_STATIC_LIBS:BOOL=OFF
		-DBUILD_CURL_EXE:BOOL=OFF
		-DBUILD_TESTING:BOOL=OFF
		-DCURL_USE_OPENSSL:BOOL=ON
		-DOPENSSL_USE_STATIC_LIBS:BOOL=${hx_openssl_static}
		-DCMAKE_FIND_PACKAGE_PREFER_CONFIG:BOOL=ON
		-DCURL_USE_LIBPSL:BOOL=OFF
		-DCURL_USE_LIBSSH2:BOOL=OFF
		-DUSE_LIBIDN2:BOOL=OFF
		-DCURL_USE_LIBSSH:BOOL=OFF
		-DCURL_USE_GSSAPI:BOOL=OFF
		-DENABLE_THREADED_RESOLVER:BOOL=OFF
		-DCURL_ZLIB:BOOL=OFF
		-DCURL_BROTLI:BOOL=OFF
		-DCURL_ZSTD:BOOL=OFF
	BUILD_COMMAND ${hx_cmake_build}
	INSTALL_COMMAND ${hx_cmake_install})

set(hx_archive_args
	-DWINDOWS_VERSION:STRING=WIN7
	-DCMAKE_TRY_COMPILE_CONFIGURATION:STRING=${HX_DEPS_CONFIGURATION})
if(WIN32 AND MSVC)
	set(hx_zlib_static "${CMAKE_INSTALL_PREFIX}/lib/zs.lib")
	if(HX_DEPS_CONFIGURATION STREQUAL "Debug")
		set(hx_zlib_static "${CMAKE_INSTALL_PREFIX}/lib/zsd.lib")
	endif()
	list(APPEND hx_archive_args
		"-DZLIB_LIBRARY_RELEASE:FILEPATH=${hx_zlib_static}"
		"-DZLIB_LIBRARY_DEBUG:FILEPATH=${hx_zlib_static}"
		"-DCMAKE_C_FLAGS:STRING=/utf-8 /DLZMA_API_STATIC"
		"-DCMAKE_EXE_LINKER_FLAGS:STRING=ws2_32.lib crypt32.lib"
		-UARCHIVE_CRYPTO_*_OPENSSL
		-ULIBLZMA_HAS_*
		-UHAVE_PKCS5_PBKDF2_HMAC_SHA1
		-UHAVE_LIBCRYPTO)
elseif(NOT WIN32)
	set(hx_crypto_suffix so)
	if(APPLE)
		set(hx_crypto_suffix dylib)
	endif()
	if(hx_openssl_static)
		set(hx_crypto_suffix a)
	endif()
	set(hx_dep_lib "${CMAKE_INSTALL_PREFIX}/lib")
	set(hx_crypto_path "${hx_dep_lib}/libcrypto.${hx_crypto_suffix}")
	set(hx_dep_include "${CMAKE_INSTALL_PREFIX}/include")
	# Do not silently substitute NDK/system libraries or drop codecs.
	list(APPEND hx_archive_args
		"-DZLIB_INCLUDE_DIR:PATH=${hx_dep_include}"
		"-DZLIB_LIBRARY_RELEASE:FILEPATH=${hx_dep_lib}/libz.a"
		"-DZLIB_LIBRARY_DEBUG:FILEPATH=${hx_dep_lib}/libz.a"
		"-DLIBLZMA_INCLUDE_DIR:PATH=${hx_dep_include}"
		"-DLIBLZMA_LIBRARY_RELEASE:FILEPATH=${hx_dep_lib}/liblzma.a"
		"-DLIBLZMA_LIBRARY_DEBUG:FILEPATH=${hx_dep_lib}/liblzma.a"
		"-DZSTD_INCLUDE_DIR:PATH=${hx_dep_include}"
		"-DZSTD_LIBRARY:FILEPATH=${hx_dep_lib}/libzstd.a"
		"-DOPENSSL_INCLUDE_DIR:PATH=${hx_dep_include}"
		"-DOPENSSL_CRYPTO_LIBRARY=${hx_crypto_path}"
		"-DOPENSSL_SSL_LIBRARY=${hx_dep_lib}/libssl.${hx_crypto_suffix}"
		-UARCHIVE_CRYPTO_*_OPENSSL -ULIBLZMA_HAS_*
		-UHAVE_PKCS5_PBKDF2_HMAC_SHA1 -UHAVE_LIBCRYPTO)
endif()

ExternalProject_Add(hx_dep_build_archive
	DEPENDS hx_dep_build_openssl hx_dep_build_zlib hx_dep_build_xz
		hx_dep_build_zstd
	SOURCE_DIR "${libarchive_source}"
	BINARY_DIR "${CMAKE_BINARY_DIR}/libarchive"
	DOWNLOAD_COMMAND ""
	UPDATE_COMMAND ""
	CMAKE_ARGS ${hx_common_args} ${hx_archive_args}
		"-DOPENSSL_ROOT_DIR:PATH=${CMAKE_INSTALL_PREFIX}"
		-DOPENSSL_USE_STATIC_LIBS:BOOL=${hx_openssl_static}
		-DENABLE_OPENSSL:BOOL=ON
		-DENABLE_CNG:BOOL=OFF
		-DENABLE_LIBB2:BOOL=OFF
		-DENABLE_LZ4:BOOL=OFF
		-DENABLE_LZO:BOOL=OFF
		-DENABLE_BZip2:BOOL=OFF
		-DENABLE_LIBXML2:BOOL=OFF
		-DENABLE_EXPAT:BOOL=OFF
		-DENABLE_WIN32_XMLLITE:BOOL=OFF
		-DENABLE_PCREPOSIX:BOOL=OFF
		-DENABLE_PCRE2POSIX:BOOL=OFF
		-DENABLE_ICONV:BOOL=OFF
		-DENABLE_TEST:BOOL=OFF
		-DENABLE_TAR:BOOL=OFF
		-DENABLE_CPIO:BOOL=OFF
		-DENABLE_CAT:BOOL=OFF
		-DENABLE_UNZIP:BOOL=OFF
	BUILD_COMMAND ${hx_cmake_build}
	INSTALL_COMMAND ${hx_cmake_install})

ExternalProject_Add_Step(hx_dep_build_archive verify_features
	COMMAND "${CMAKE_COMMAND}"
		"-DHX_ARCHIVE_CONFIG=<BINARY_DIR>/config.h"
		-P "${CMAKE_CURRENT_LIST_DIR}/check_features.cmake"
	DEPENDEES configure DEPENDERS build)
