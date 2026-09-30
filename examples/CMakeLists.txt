foreach(example IN ITEMS hello async_timer configuration containers system_info)
	add_executable(hx_os_${example} ${example}.c)
	target_link_libraries(hx_os_${example} PRIVATE hx_c_platform)
	set_target_properties(hx_os_${example} PROPERTIES
		C_STANDARD 99 C_STANDARD_REQUIRED YES C_EXTENSIONS NO
		RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
	hx_apply_warnings(hx_os_${example})
endforeach()
if(CMAKE_CXX_COMPILER_LOADED)
	add_executable(hx_os_cpp_thread_pool thread_pool.cpp)
	target_link_libraries(hx_os_cpp_thread_pool PRIVATE hx_c_platform::cpp)
	set_target_properties(hx_os_cpp_thread_pool PROPERTIES
		CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO
		RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
	hx_apply_warnings(hx_os_cpp_thread_pool)
endif()
