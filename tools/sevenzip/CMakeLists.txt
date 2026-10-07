cmake_minimum_required(VERSION 3.16)
project(hx_native_sevenzip LANGUAGES C CXX)

include(ExternalProject)
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/HxDependencySources.cmake")
hx_dependency_source(7zip hx_source)

set(HX_SEVENZIP_JOBS 4 CACHE STRING "Maximum native make jobs")
if(NOT HX_SEVENZIP_JOBS MATCHES "^[1-9][0-9]*$")
	message(FATAL_ERROR "HX_SEVENZIP_JOBS must be positive")
endif()
if(CMAKE_BINARY_DIR MATCHES "[ \t]")
	message(FATAL_ERROR
		"The upstream makefiles need a build path without spaces")
endif()
if(CMAKE_CROSSCOMPILING)
	message(FATAL_ERROR
		"This helper builds host runtimes only. "
		"Provide a separately cross-built target runtime.")
endif()

set(hx_output "${CMAKE_BINARY_DIR}/runtime")
file(MAKE_DIRECTORY "${hx_output}")
if(WIN32 AND MSVC)
	find_program(hx_make NAMES nmake)
	if(CMAKE_GENERATOR_PLATFORM STREQUAL "ARM64")
		set(hx_platform arm64)
	elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
		set(hx_platform x86)
	else()
		set(hx_platform x64)
	endif()
	file(TO_NATIVE_PATH "${hx_output}" hx_native_output)
	set(hx_command "${hx_make}" /nologo /f makefile
		"O=${hx_native_output}" "PLATFORM=${hx_platform}")
	set(hx_runtime "${hx_output}/7z.dll")
else()
	find_program(hx_make NAMES gmake make)
	set(hx_command "${hx_make}" -f makefile.gcc
		"O=${hx_output}" "-j${HX_SEVENZIP_JOBS}"
		"CC=${CMAKE_C_COMPILER}" "CXX=${CMAKE_CXX_COMPILER}")
	set(hx_runtime "${hx_output}/7z.so")
	if(APPLE)
		list(APPEND hx_command "SHARED_EXT=.dylib"
			"LDFLAGS=-dynamiclib -fPIC" "LFLAGS_STRIP=")
		set(hx_runtime "${hx_output}/7z.dylib")
	endif()
endif()

if(NOT hx_make)
	message(FATAL_ERROR "Native make was not found; use a developer shell")
endif()

ExternalProject_Add(hx_sevenzip_runtime
	SOURCE_DIR "${hx_source}"
	BINARY_DIR "${CMAKE_BINARY_DIR}/driver"
	DOWNLOAD_COMMAND "" UPDATE_COMMAND "" CONFIGURE_COMMAND ""
	BUILD_COMMAND "${CMAKE_COMMAND}" -E chdir
		"${hx_source}/CPP/7zip/Bundles/Format7zF" ${hx_command}
	INSTALL_COMMAND ""
	BUILD_BYPRODUCTS "${hx_runtime}")
install(FILES "${hx_runtime}" DESTINATION lib/hx-sevenzip)
install(FILES "${hx_source}/DOC/License.txt"
	DESTINATION share/hx-dependencies RENAME 7-Zip-LICENSE.txt)

install(FILES "${hx_source}/DOC/copying.txt"
	DESTINATION "share/hx-dependencies"
	RENAME 7-Zip-LGPL.txt)
install(FILES "${hx_source}/DOC/unRarLicense.txt"
	DESTINATION "share/hx-dependencies"
	RENAME 7-Zip-unRAR.txt)
