# Optional host-side source generation. C99 targets do not need this module
# unless they use the HX_PRINT/HX_FPRINT/HX_FORMAT source markers.
include_guard(GLOBAL)
set_property(GLOBAL PROPERTY HX_FORMAT_MODULE_DIR "${CMAKE_CURRENT_LIST_DIR}")

function(hx_enable_format_syntax target)
	cmake_parse_arguments(HX "" "CLANG;PYTHON"
		"SOURCES;CLANG_OPTIONS" ${ARGN})
	if(NOT TARGET ${target} OR NOT HX_SOURCES)
		message(FATAL_ERROR
			"Format syntax needs a target and explicit SOURCES"
		)
	endif()
	if(NOT HX_PYTHON)
		find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
		set(HX_PYTHON "${Python3_EXECUTABLE}")
	endif()
	if(NOT HX_CLANG)
		if(HX_FORMAT_CLANG)
			set(HX_CLANG "${HX_FORMAT_CLANG}")
		elseif(CMAKE_C_COMPILER_ID MATCHES "Clang")
			set(HX_CLANG "${CMAKE_C_COMPILER}")
		else()
			find_program(HX_FORMAT_CLANG
				NAMES clang clang-21 clang-20 clang-19
				DOC "Host Clang used for optional C syntax")
			set(HX_CLANG "${HX_FORMAT_CLANG}")
		endif()
	endif()
	if(NOT HX_CLANG OR NOT EXISTS "${HX_CLANG}")
		message(FATAL_ERROR "Format syntax needs host Clang. "
			"Set HX_FORMAT_CLANG or pass CLANG to this function.")
	endif()
	set_property(TARGET ${target} PROPERTY HX_FORMAT_CLANG "${HX_CLANG}")
	get_property(module_dir GLOBAL PROPERTY HX_FORMAT_MODULE_DIR)
	if(EXISTS "${module_dir}/../tools/hx_format_codegen.py")
		set(script "${module_dir}/../tools/hx_format_codegen.py")
	else()
		set(script "${module_dir}/hx_format_codegen.py")
	endif()
	get_target_property(source_dir ${target} SOURCE_DIR)
	get_target_property(original ${target} SOURCES)
	get_target_property(enabled ${target} HX_FORMAT_ENABLED)
	if(enabled)
		message(FATAL_ERROR "Enable format syntax once per target")
	endif()
	set_property(TARGET ${target} PROPERTY HX_FORMAT_ENABLED TRUE)
	set_property(TARGET ${target} PROPERTY HX_FORMAT_OPTIONS
		"${HX_CLANG_OPTIONS}")
	set(triple "${CMAKE_C_COMPILER_TARGET}")
	set(runtime "")
	if(MSVC)
		get_target_property(runtime ${target} MSVC_RUNTIME_LIBRARY)
		if(runtime STREQUAL "runtime-NOTFOUND")
			set(runtime "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
		endif()
		if(CMAKE_SIZEOF_VOID_P EQUAL 4)
			set(triple i686-pc-windows-msvc)
		elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM64|arm64|aarch64)$")
			set(triple aarch64-pc-windows-msvc)
		else()
			set(triple x86_64-pc-windows-msvc)
		endif()
	elseif(NOT triple AND CMAKE_C_COMPILER_ID STREQUAL "GNU")
		execute_process(COMMAND "${CMAKE_C_COMPILER}" -dumpmachine
			OUTPUT_VARIABLE triple OUTPUT_STRIP_TRAILING_WHITESPACE
			RESULT_VARIABLE queried)
		if(NOT queried EQUAL 0)
			message(FATAL_ERROR "Cannot query the C target triple")
		endif()
	endif()
	if(CMAKE_CONFIGURATION_TYPES)
		set(configurations ${CMAKE_CONFIGURATION_TYPES})
	elseif(CMAKE_BUILD_TYPE)
		set(configurations "${CMAKE_BUILD_TYPE}")
	else()
		set(configurations default)
	endif()
	set(commands)
	set(products)
	foreach(config IN LISTS configurations)
		string(TOUPPER "${config}" upper)
		set(base "${CMAKE_CURRENT_BINARY_DIR}/hx-format/${target}")
		set(directory "${base}/${config}")
		set(settings "${directory}/flags-$<COMPILE_LANGUAGE>.cfg")
		set(content "[target]\n${triple}\n")
		string(APPEND content "[sysroot]\n${CMAKE_SYSROOT}\n")
		string(APPEND content "[msvc_runtime]\n${runtime}\n")
		string(APPEND content "[includes]\n"
			"$<JOIN:$<TARGET_PROPERTY:${target},"
			"INCLUDE_DIRECTORIES>,\n>\n")
		string(APPEND content "[definitions]\n"
			"$<JOIN:$<TARGET_PROPERTY:${target},"
			"COMPILE_DEFINITIONS>,\n>\n")
		string(APPEND content "[options]\n"
			"$<JOIN:$<TARGET_PROPERTY:${target},"
			"COMPILE_OPTIONS>,\n>\n"
			"$<JOIN:$<TARGET_GENEX_EVAL:${target},"
			"$<TARGET_PROPERTY:${target},HX_FORMAT_OPTIONS>>,\n>\n")
		string(APPEND content "[flags]\n${CMAKE_C_FLAGS}\n"
			"${CMAKE_C_FLAGS_${upper}}\n")
		if(config STREQUAL "default")
			file(GENERATE OUTPUT "${settings}" CONTENT "${content}")
		else()
			file(GENERATE OUTPUT "${settings}" CONTENT "${content}"
				CONDITION "$<CONFIG:${config}>")
		endif()
		foreach(input IN LISTS HX_SOURCES)
			get_filename_component(absolute "${input}" ABSOLUTE
				BASE_DIR "${source_dir}")
			if(NOT input IN_LIST original AND
				NOT absolute IN_LIST original)
				message(FATAL_ERROR
					"Missing target source: ${input}")
			endif()
			foreach(key IN ITEMS COMPILE_OPTIONS COMPILE_DEFINITIONS
				COMPILE_FLAGS INCLUDE_DIRECTORIES)
				get_source_file_property(specific
					"${input}" ${key})
				if(specific)
					message(FATAL_ERROR
						"Move ${key} to the target "
						"for ${input} before enabling "
						"format syntax")
				endif()
			endforeach()
			get_filename_component(extension "${absolute}" EXT)
			if(NOT extension STREQUAL ".c")
				message(FATAL_ERROR
					"Format generation accepts C .c files")
			endif()
			string(SHA256 identity "${absolute}")
			string(SUBSTRING "${identity}" 0 12 identity)
			get_filename_component(name "${absolute}" NAME_WE)
			get_filename_component(input_dir
				"${absolute}" DIRECTORY)
			set(output "${directory}/${name}-${identity}.c")
			list(APPEND products "${output}")
			list(APPEND commands COMMAND "${HX_PYTHON}" "${script}"
				--clang "${HX_CLANG}" --input "${absolute}"
				--output "${output}"
				--config "${directory}/flags-C.cfg"
				--active "$<CONFIG>"
				--configuration "${config}")
			set_source_files_properties("${output}" PROPERTIES
				GENERATED TRUE
				INCLUDE_DIRECTORIES "${input_dir}")
			if(config STREQUAL "default")
				target_sources(${target} PRIVATE "${output}")
			else()
				target_sources(${target} PRIVATE
					"$<$<CONFIG:${config}>:${output}>")
			endif()
		endforeach()
	endforeach()
	get_target_property(updated ${target} SOURCES)
	foreach(input IN LISTS HX_SOURCES)
		get_filename_component(absolute "${input}" ABSOLUTE
			BASE_DIR "${source_dir}")
		list(REMOVE_ITEM updated "${input}" "${absolute}")
	endforeach()
	set_property(TARGET ${target} PROPERTY SOURCES "${updated}")
	add_custom_target(${target}_hx_format ${commands}
		BYPRODUCTS ${products} VERBATIM
		COMMENT "Checking C format sources for ${target}")
	add_dependencies(${target} ${target}_hx_format)
endfunction()
