# Probe the library and compiler together; C++20 flag support alone does not
# guarantee <coroutine>. C99/C++11 builds stay usable on older toolchains.
include_guard(GLOBAL)
function(hx_check_cpp20)
	if(NOT "cxx_std_20" IN_LIST CMAKE_CXX_COMPILE_FEATURES)
		set(HX_OS_HAVE_CPP20_COROUTINES OFF PARENT_SCOPE)
		return()
	endif()
	include(CheckCXXSourceCompiles)
	set(CMAKE_CXX_STANDARD 20)
	set(CMAKE_CXX_STANDARD_REQUIRED YES)
	set(CMAKE_CXX_EXTENSIONS NO)
	check_cxx_source_compiles("
		#include <coroutine>
		#ifndef __cpp_impl_coroutine
		#error C++20 compiler unavailable
		#endif
		#ifndef __cpp_lib_coroutine
		#error C++20 library unavailable
		#endif
		int main() {
			std::coroutine_handle<> handle;
			return handle ? 1 : 0;
		}" HX_OS_HAVE_CPP20_COROUTINES)
endfunction()
