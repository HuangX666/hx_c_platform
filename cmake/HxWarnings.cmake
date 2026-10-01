# Copyright (c) 2026 hx_c_log contributors. MIT.
# Apply OBS-derived warning options only to a specified first-party target.
# Baseline: OBS commit bdf6dbe17cee9a1371add901e20359d9f0757159.
# Dependencies: CMake 3.16+ and an enabled C or C++ compiler.
# This module does not change language standards, SDKs, IPO, or vendor targets.

include_guard(GLOBAL)
include(CheckCCompilerFlag)
include(CheckCXXCompilerFlag)
include("${CMAKE_CURRENT_LIST_DIR}/HxNoExceptions.cmake")

# Configure compiler warnings and the default warnings-as-errors policy.
# The target must already exist. An explicit CMAKE_COMPILE_WARNING_AS_ERROR
# value overrides the default; return-type errors retain the OBS flag policy.
function(hx_apply_warnings target)
	hx_disable_exceptions(${target})
	if(MSVC)
		target_compile_definitions(${target} PRIVATE
			_CRT_SECURE_NO_WARNINGS)
		target_compile_options(${target} PRIVATE /utf-8)
	endif()
	set(warnings_as_errors ON)
	if(DEFINED CMAKE_COMPILE_WARNING_AS_ERROR)
		set(warnings_as_errors ${CMAKE_COMPILE_WARNING_AS_ERROR})
	endif()

	foreach(lang IN ITEMS C CXX)
		if(NOT CMAKE_${lang}_COMPILER_LOADED)
			continue()
		endif()

		set(compiler ${CMAKE_${lang}_COMPILER_ID})
		set(version ${CMAKE_${lang}_COMPILER_VERSION})
		set(flags)
		set(msvc_frontend FALSE)
		if(compiler STREQUAL "MSVC" OR
			CMAKE_${lang}_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
			set(msvc_frontend TRUE)
			target_compile_options(${target} PRIVATE
				"$<$<COMPILE_LANGUAGE:${lang}>:/W3>"
			)
		endif()

		if(compiler STREQUAL "GNU")
			set(flags
				-Wdeprecated-declarations
				-Wempty-body
				-Wenum-conversion
				-Werror=return-type
				-Wextra
				-Wformat
				-Wformat-security
				-Wno-conversion
				-Wno-float-conversion
				-Wno-implicit-fallthrough
				-Wno-missing-braces
				-Wno-missing-field-initializers
				-Wno-shadow
				-Wno-sign-conversion
				-Wno-trigraphs
				-Wno-unknown-pragmas
				-Wno-unused-function
				-Wno-unused-label
				-Wparentheses
				-Wswitch
				-Wuninitialized
				-Wunreachable-code
				-Wunused-parameter
				-Wunused-value
				-Wunused-variable
				-Wvla
			)
			if(lang STREQUAL "C")
				list(APPEND flags -Wint-conversion
					-Wno-missing-prototypes
					-Wno-strict-prototypes
					-Wpointer-sign
				)
			else()
				list(APPEND flags
					-Winvalid-offsetof
					-Wno-overloaded-virtual
				)
			endif()
			if(version VERSION_GREATER_EQUAL 12)
				list(APPEND flags
					-Winfinite-recursion
					-Wno-stringop-overflow
				)
			endif()
			if(version VERSION_GREATER_EQUAL 12.1)
				list(APPEND flags
					-Wno-error=maybe-uninitialized
				)
			endif()
			if(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
				list(APPEND flags -Wno-error=type-limits)
			endif()
		elseif(compiler MATCHES "^(Clang|AppleClang)$")
			set(flags
				-Wno-trigraphs
				-Wno-missing-field-initializers
				-Wno-missing-prototypes
				-Werror=return-type
				-Wunreachable-code
				-Wquoted-include-in-framework-header
				-Wno-missing-braces
				-Wparentheses
				-Wswitch
				-Wno-unused-function
				-Wno-unused-label
				-Wunused-parameter
				-Wunused-variable
				-Wunused-value
				-Wempty-body
				-Wuninitialized
				-Wno-unknown-pragmas
				-Wfour-char-constants
				-Wconstant-conversion
				-Wno-conversion
				-Wint-conversion
				-Wbool-conversion
				-Wenum-conversion
				-Wnon-literal-null-conversion
				-Wsign-compare
				-Wshorten-64-to-32
				-Wpointer-sign
				-Wnewline-eof
				-Wno-implicit-fallthrough
				-Wdeprecated-declarations
				-Wno-sign-conversion
				-Winfinite-recursion
				-Wcomma
				-Wno-strict-prototypes
				-Wno-semicolon-before-method-body
				-Wformat-security
				-Wvla
				-Wno-error=shorten-64-to-32
			)
			if(lang STREQUAL "C")
				list(APPEND flags
					-Wno-shadow -Wno-float-conversion
				)
			else()
				list(APPEND flags
					-Wno-non-virtual-dtor
					-Wno-overloaded-virtual
					-Wno-exit-time-destructors
					-Wno-shadow
					-Winvalid-offsetof
					-Wmove
					-Werror=block-capture-autoreleasing
					-Wrange-loop-analysis
				)
			endif()
		elseif(NOT compiler STREQUAL "MSVC")
			message(FATAL_ERROR
				"No OBS profile: ${lang}/${compiler}."
			)
		endif()

		if(OBS_COMPILE_DEPRECATION_AS_WARNING AND flags)
			list(APPEND flags -Wno-error=deprecated-declarations)
		endif()

		foreach(flag IN LISTS flags)
			# Probe positive flags; GCC can accept unknown -Wno-*.
			string(REGEX REPLACE "^-W(no-)?error=" "-W"
				probe "${flag}"
			)
			string(REGEX REPLACE "^-Wno-" "-W" probe "${probe}")
			string(MAKE_C_IDENTIFIER
				"OBS_RULES_${lang}_${probe}" supported
			)
			set(probe_flags "${probe}")
			if(compiler STREQUAL "GNU" AND
				probe STREQUAL "-Wformat-security")
				list(PREPEND probe_flags -Wformat)
			endif()
			if(lang STREQUAL "C")
				check_c_compiler_flag(
					"${probe_flags}" ${supported})
			else()
				check_cxx_compiler_flag(
					"${probe_flags}" ${supported})
			endif()
			if(${supported})
				target_compile_options(${target} PRIVATE
					"$<$<COMPILE_LANGUAGE:${lang}>:${flag}>"
				)
			else()
				message(STATUS "Omitted OBS flag: ${flag}")
			endif()
		endforeach()

		if(warnings_as_errors)
			if(msvc_frontend)
				set(error_flag /WX)
			else()
				set(error_flag -Werror)
			endif()
			target_compile_options(${target} PRIVATE
				"$<$<COMPILE_LANGUAGE:${lang}>:${error_flag}>"
			)
		endif()
	endforeach()

	get_target_property(target_type ${target} TYPE)
	set(linked_types EXECUTABLE SHARED_LIBRARY MODULE_LIBRARY)
	if(MSVC AND warnings_as_errors AND
		target_type IN_LIST linked_types)
		target_link_options(${target} PRIVATE /WX)
	endif()
endfunction()
