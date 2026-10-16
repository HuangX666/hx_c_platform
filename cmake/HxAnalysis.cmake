include_guard(GLOBAL)

# Only owned targets opt in; exported consumers keep their own compiler flags.
function(hx_apply_analysis target)
	if(NOT HX_OS_DEVELOPER_ANALYSIS)
		return()
	endif()

	foreach(lang IN ITEMS C CXX)
		if(NOT CMAKE_${lang}_COMPILER_LOADED)
			continue()
		endif()

		if(MSVC)
			target_compile_options(${target} PRIVATE
				"$<$<COMPILE_LANGUAGE:${lang}>:/Zi>")
		elseif(CMAKE_${lang}_COMPILER_ID MATCHES "GNU|Clang")
			set(frame_flag -fno-omit-frame-pointer)
			target_compile_options(${target} PRIVATE
				"$<$<COMPILE_LANGUAGE:${lang}>:-g>"
				"$<$<COMPILE_LANGUAGE:${lang}>:${frame_flag}>")
		else()
			message(FATAL_ERROR
				"Unsupported compiler for developer analysis.")
		endif()
	endforeach()

	get_target_property(kind ${target} TYPE)
	if(MSVC AND NOT kind STREQUAL "STATIC_LIBRARY")
		target_link_options(${target} PRIVATE /DEBUG:FULL)
		# /DEBUG changes defaults; retain release linker optimization.
		foreach(config IN ITEMS Release MinSizeRel)
			target_link_options(${target} PRIVATE
				"$<$<CONFIG:${config}>:/OPT:REF>"
				"$<$<CONFIG:${config}>:/OPT:ICF>"
				"$<$<CONFIG:${config}>:/INCREMENTAL:NO>")
		endforeach()
	endif()
endfunction()
