foreach(example IN ITEMS hello async_timer configuration containers system_info
	load_plugin text_file environment special_paths thread_options
	pipe_transfer positioned_read file_info config_save periodic_timer
	random_uuid mapped_file file_attributes watch_directory capture_process
	delayed_task)
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
if(HX_OS_BUILD_TESTS AND HX_OS_ENABLE_FILE_UTILS)
	file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/text-fixture.txt"
		"UTF-8 example: 中文\n")
	add_test(NAME text_file_example COMMAND hx_os_text_file
		"${CMAKE_CURRENT_BINARY_DIR}/text-fixture.txt")
	set_tests_properties(text_file_example PROPERTIES TIMEOUT 20)
	if(HX_OS_ENABLE_SYSTEM_UTILS)
		set(hx_unicode_fixture
			"${CMAKE_CURRENT_BINARY_DIR}/配置-😀.txt")
		file(WRITE "${hx_unicode_fixture}" "Unicode filename\n")
		add_test(NAME text_file_unicode_name
			COMMAND hx_os_text_file "${hx_unicode_fixture}")
		set_tests_properties(text_file_unicode_name PROPERTIES
			TIMEOUT 20)
	endif()
endif()
if(HX_OS_BUILD_TESTS AND HX_OS_ENABLE_ENV)
	add_test(NAME environment_example COMMAND hx_os_environment
		HX_PLATFORM_EXAMPLE)
	set_tests_properties(environment_example PROPERTIES TIMEOUT 20
		ENVIRONMENT "HX_PLATFORM_EXAMPLE=UTF-8 example")
endif()
if(HX_OS_BUILD_TESTS AND HX_OS_ENABLE_PATHS)
	add_test(NAME special_paths_example COMMAND hx_os_special_paths)
	set_tests_properties(special_paths_example PROPERTIES TIMEOUT 20)
endif()
if(HX_OS_BUILD_TESTS AND HX_OS_ENABLE_THREAD_EX)
	add_test(NAME thread_options_example COMMAND hx_os_thread_options)
	set_tests_properties(thread_options_example PROPERTIES TIMEOUT 20)
endif()
if(HX_OS_BUILD_TESTS AND HX_OS_ENABLE_PIPE AND HX_OS_ENABLE_LOOP)
	add_test(NAME pipe_transfer_example COMMAND hx_os_pipe_transfer)
	set_tests_properties(pipe_transfer_example PROPERTIES TIMEOUT 20)
endif()
if(HX_OS_BUILD_TESTS AND HX_OS_ENABLE_POSITIONED_IO)
	file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/positioned-fixture.bin"
		"0123456789")
	add_test(NAME positioned_read_example COMMAND hx_os_positioned_read
		"${CMAKE_CURRENT_BINARY_DIR}/positioned-fixture.bin")
	set_tests_properties(positioned_read_example PROPERTIES TIMEOUT 20)
endif()
if(HX_OS_BUILD_TESTS AND HX_OS_ENABLE_FILE_INFO)
	file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/info-fixture.txt" "hello")
	add_test(NAME file_info_example COMMAND hx_os_file_info
		"${CMAKE_CURRENT_BINARY_DIR}/info-fixture.txt")
	set_tests_properties(file_info_example PROPERTIES TIMEOUT 20)
endif()
if(HX_OS_BUILD_TESTS AND HX_OS_ENABLE_CONFIG AND HX_OS_ENABLE_CONFIG_SERIALIZE)
	add_test(NAME config_serialize_example COMMAND hx_os_config_save)
	set_tests_properties(config_serialize_example PROPERTIES TIMEOUT 20)
endif()
if(HX_OS_BUILD_TESTS AND HX_OS_ENABLE_TIMER AND HX_OS_ENABLE_LOOP)
	add_test(NAME periodic_timer_example COMMAND hx_os_periodic_timer)
	set_tests_properties(periodic_timer_example PROPERTIES TIMEOUT 20)
endif()
if(HX_OS_BUILD_TESTS AND HX_OS_ENABLE_RANDOM)
	add_test(NAME random_uuid_example COMMAND hx_os_random_uuid)
	set_tests_properties(random_uuid_example PROPERTIES TIMEOUT 20)
endif()
if(HX_OS_BUILD_TESTS AND HX_OS_ENABLE_VIRTUAL_MEMORY)
	file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/mapped-fixture.txt"
		"mapped data")
	add_test(NAME mapped_file_example COMMAND hx_os_mapped_file
		"${CMAKE_CURRENT_BINARY_DIR}/mapped-fixture.txt")
	set_tests_properties(mapped_file_example PROPERTIES TIMEOUT 20)
endif()
if(HX_OS_BUILD_TESTS AND HX_OS_ENABLE_FILE_EXTRA)
	file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/attributes-fixture.txt"
		"attribute example")
	add_test(NAME file_attributes_example COMMAND hx_os_file_attributes
		"${CMAKE_CURRENT_BINARY_DIR}/attributes-fixture.txt")
	set_tests_properties(file_attributes_example PROPERTIES TIMEOUT 20)
endif()
if(HX_OS_BUILD_TESTS AND HX_OS_ENABLE_WATCH)
	add_test(NAME watch_directory_example COMMAND hx_os_watch_directory)
	set_tests_properties(watch_directory_example PROPERTIES TIMEOUT 20)
endif()
if(HX_OS_BUILD_TESTS AND HX_OS_ENABLE_PROCESS AND HX_OS_ENABLE_PIPE
		AND HX_OS_ENABLE_LOOP)
	add_test(NAME process_capture_example COMMAND hx_os_capture_process
		$<TARGET_FILE:hx_os_process_fixture> empty)
	set_tests_properties(process_capture_example PROPERTIES TIMEOUT 20)
endif()
if(HX_OS_BUILD_TESTS AND HX_OS_ENABLE_THREAD_POOL
		AND HX_OS_ENABLE_DELAYED_TASKS)
	add_test(NAME delayed_task_example COMMAND hx_os_delayed_task)
	set_tests_properties(delayed_task_example PROPERTIES TIMEOUT 20)
endif()
if(CMAKE_CXX_COMPILER_LOADED)
	add_executable(hx_os_cpp_thread_pool thread_pool.cpp)
	target_link_libraries(hx_os_cpp_thread_pool PRIVATE hx_c_platform::cpp)
	set_target_properties(hx_os_cpp_thread_pool PROPERTIES
		CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO
		RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
	hx_apply_warnings(hx_os_cpp_thread_pool)
endif()
