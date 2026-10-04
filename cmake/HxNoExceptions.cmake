# Applies only to explicitly selected first-party validation/example targets.
# Public interface targets do not change unrelated consumer translation units.
include_guard(GLOBAL)

function(hx_disable_exceptions target)
	if(MSVC)
		target_compile_options(${target} PRIVATE
			"$<$<COMPILE_LANGUAGE:CXX>:/EHs-c->")
		target_compile_definitions(${target} PRIVATE
			"$<$<COMPILE_LANGUAGE:CXX>:_HAS_EXCEPTIONS=0>")
	elseif(CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$")
		target_compile_options(${target} PRIVATE
			"$<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>")
	endif()
endfunction()
