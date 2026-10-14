# Optional parser dependencies are compiled privately from unmodified sources.
include("${CMAKE_CURRENT_LIST_DIR}/HxDependencySources.cmake")
set(hx_parser_sources)
set(hx_parser_objects)
set(hx_parser_includes)
set(hx_parser_aliases)
set(hx_parser_definitions)

function(hx_parser_config path content)
	if(EXISTS "${path}")
		file(READ "${path}" previous)
		if(previous STREQUAL content)
			return()
		endif()
	endif()
	file(WRITE "${path}" "${content}")
endfunction()

foreach(hx_name CJSON LIBYAML MINIXML INIH TOMLC)
	option(HX_OS_ENABLE_${hx_name} "Build optional ${hx_name} parser" OFF)

	set(HX_OS_${hx_name}_SOURCE_DIR "" CACHE PATH
		"Unmodified ${hx_name} source directory")
	set(HX_OS_HAVE_${hx_name} 0)

	if(HX_OS_ENABLE_${hx_name} AND NOT HX_OS_ENABLE_CONFIG)
		message(FATAL_ERROR "${hx_name} requires HX_OS_ENABLE_CONFIG")
	endif()

	if(HX_OS_ENABLE_${hx_name} AND NOT HX_OS_HAVE_LOCAL_STRTOD AND
		(hx_name STREQUAL "CJSON" OR hx_name STREQUAL "TOMLC"))
		message(FATAL_ERROR "${hx_name} requires a per-call C locale")
	endif()
endforeach()

function(hx_parser_vendor name)
	string(TOUPPER "${name}" upper)
	set(root "${HX_OS_${upper}_SOURCE_DIR}")
	if(NOT root)
		hx_dependency_source("${name}" root)
	endif()

	if(NOT IS_DIRECTORY "${root}")
		message(FATAL_ERROR
			"Invalid HX_OS_${upper}_SOURCE_DIR: ${root}")
	endif()

	set(generated "${CMAKE_CURRENT_BINARY_DIR}/parsers/${name}")
	file(MAKE_DIRECTORY "${generated}")

	if(name STREQUAL "cjson")
		set(sources "${root}/cJSON.c")
		set(headers "${root}/cJSON.h")
		set(includes "${root}")
		set(definitions CJSON_HIDE_SYMBOLS CJSON_NESTING_LIMIT=128)
		set(license "${root}/LICENSE")
	elseif(name STREQUAL "libyaml")
		set(sources)

		foreach(unit api dumper emitter loader parser reader scanner
			writer)
			list(APPEND sources "${root}/src/${unit}.c")
		endforeach()

		set(headers "${root}/include/yaml.h"
			"${root}/src/yaml_private.h")
		set(includes "${root}/include" "${generated}")
		string(CONCAT configuration
			"#define YAML_VERSION_MAJOR 0\n"
			"#define YAML_VERSION_MINOR 2\n"
			"#define YAML_VERSION_PATCH 5\n"
			"#define YAML_VERSION_STRING \"0.2.5\"\n")
		hx_parser_config("${generated}/config.h" "${configuration}")
		set(definitions YAML_DECLARE_STATIC HAVE_CONFIG_H)
		set(license "${root}/License")
	elseif(name STREQUAL "minixml")
		set(sources)

		foreach(unit attr file get index node options private search
			set)
			list(APPEND sources "${root}/mxml-${unit}.c")
		endforeach()

		set(headers "${root}/mxml.h" "${root}/mxml-private.h")
		set(includes "${root}" "${generated}")
		# No callback setter is exposed or called. The private table
		# stays read-only, requiring no TLS allocation or destructor.
		hx_parser_config("${generated}/config.h"
			"#define MXML_VERSION \"4.0.6\"\n")
		set(definitions)
		set(license "${root}/LICENSE")
	elseif(name STREQUAL "inih")
		set(sources "${root}/ini.c")
		set(headers "${root}/ini.h")
		set(includes "${root}")
		set(definitions INI_API= INI_USE_STACK=0 INI_ALLOW_REALLOC=1
			INI_MAX_LINE=1048579 INI_ALLOW_MULTILINE=0
			INI_HANDLER_LINENO=1 INI_STOP_ON_FIRST_ERROR=1
			INI_CALL_HANDLER_ON_NEW_SECTION=1)
		set(license "${root}/LICENSE.txt")
	elseif(name STREQUAL "tomlc")
		set(sources "${root}/src/tomlc17.c")
		set(headers "${root}/src/tomlc17.h")
		set(includes "${root}/src")
		set(definitions)
		set(license "${root}/LICENSE")
	endif()

	set(symbols)
	set(macros)

	foreach(header IN LISTS headers)
		file(READ "${header}" content)
		string(REGEX MATCHALL
			"#[ \t]*define[ \t]+[A-Za-z_][A-Za-z_0-9]*"
			macro_matches "${content}")

		foreach(match IN LISTS macro_matches)
			string(REGEX REPLACE "#[ \t]*define[ \t]+" ""
				macro "${match}")
			list(APPEND macros "${macro}")
		endforeach()

		string(CONCAT pattern "(cJSON_|yaml_|_?mxml|toml_|ini_)"
			"[A-Za-z0-9_]+[ \t\r\n]*\\(")
		string(REGEX MATCHALL "${pattern}" matches "${content}")

		foreach(match IN LISTS matches)
			string(REGEX REPLACE "[ \t\r\n]*\\($" ""
				symbol "${match}")
			list(APPEND symbols "${symbol}")
		endforeach()
	endforeach()

	list(REMOVE_DUPLICATES symbols)
	list(REMOVE_ITEM symbols ${macros})
	set(aliases)

	foreach(symbol IN LISTS symbols)
		string(TOLOWER "${symbol}" lower)
		list(APPEND aliases "${symbol}=hx_os_v_${lower}")
	endforeach()

	add_library(hx_parser_${name} OBJECT ${sources})
	set_target_properties(hx_parser_${name} PROPERTIES
		C_STANDARD 99
		C_STANDARD_REQUIRED YES
		C_EXTENSIONS NO
		POSITION_INDEPENDENT_CODE ON
		C_VISIBILITY_PRESET hidden)
	target_include_directories(hx_parser_${name} PRIVATE ${includes})
	target_compile_definitions(hx_parser_${name} PRIVATE
		${definitions} ${aliases})
	set(redirect "${PROJECT_SOURCE_DIR}/src/config_vendor_compile.h")

	if(MSVC)
		target_compile_options(hx_parser_${name} PRIVATE
			/utf-8 "/FI${redirect}")
		target_compile_definitions(hx_parser_${name} PRIVATE
			_CRT_SECURE_NO_WARNINGS= _CRT_NONSTDC_NO_WARNINGS=)
	else()
		target_compile_options(hx_parser_${name} PRIVATE
			-include "${redirect}")
		target_compile_definitions(hx_parser_${name} PRIVATE
			_GNU_SOURCE)
	endif()

	if(HX_OS_SANITIZER AND NOT MSVC)
		target_compile_options(hx_parser_${name} PRIVATE
			-fsanitize=${HX_OS_SANITIZER} -fno-omit-frame-pointer)
	endif()

	set(HX_OS_HAVE_${upper} 1 PARENT_SCOPE)
	set(hx_parser_sources ${hx_parser_sources}
		src/config_${name}.c PARENT_SCOPE)
	set(hx_parser_objects ${hx_parser_objects}
		$<TARGET_OBJECTS:hx_parser_${name}> PARENT_SCOPE)
	set(hx_parser_includes ${hx_parser_includes} ${includes} PARENT_SCOPE)
	set(hx_parser_aliases ${hx_parser_aliases} ${aliases} PARENT_SCOPE)
	set(hx_parser_definitions ${hx_parser_definitions}
		${definitions} PARENT_SCOPE)

	install(FILES "${license}" DESTINATION
		"${CMAKE_INSTALL_DATADIR}/hx_c_platform/licenses"
		RENAME "${name}-LICENSE")

	if(name STREQUAL "minixml")
		install(FILES "${root}/NOTICE" DESTINATION
			"${CMAKE_INSTALL_DATADIR}/hx_c_platform/licenses"
			RENAME "minixml-NOTICE")
	endif()
endfunction()

foreach(hx_name cjson libyaml minixml inih tomlc)
	string(TOUPPER "${hx_name}" hx_upper)

	if(HX_OS_ENABLE_${hx_upper})
		hx_parser_vendor("${hx_name}")
	endif()
endforeach()

if(hx_parser_sources)
	list(APPEND hx_parser_sources src/config_vendor_memory.c)
endif()
