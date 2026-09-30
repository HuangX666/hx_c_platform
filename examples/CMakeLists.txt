foreach(example IN ITEMS hello async_timer configuration containers system_info
	load_plugin)
	add_executable(hx_os_${example} ${example}.c)
	target_link_libraries(hx_os_${example} PRIVATE hx_c_platform)
	set_target_properties(hx_os_${example} PROPERTIES
		C_STANDARD 99 C_STANDARD_REQUIRED YES C_EXTENSIONS NO
		RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
	hx_apply_warnings(hx_os_${example})
endforeach()
if(HX_OS_BUILD_TESTS AND HX_OS_ENABLE_LIBRARY)
	add_test(NAME plugin_example COMMAND hx_os_load_plugin
		$<TARGET_FILE:hx_os_test_library_module>)
	set_tests_properties(plugin_example PROPERTIES TIMEOUT 20)
endif()
if(CMAKE_CXX_COMPILER_LOADED)
	add_executable(hx_os_cpp_thread_pool thread_pool.cpp)
	target_link_libraries(hx_os_cpp_thread_pool PRIVATE hx_c_platform::cpp)
	set_target_properties(hx_os_cpp_thread_pool PROPERTIES
		CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO
		RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
	hx_apply_warnings(hx_os_cpp_thread_pool)
endif()
