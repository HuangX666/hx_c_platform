foreach(example IN ITEMS hello async_timer configuration containers system_info
	load_plugin text_file environment special_paths thread_options
	pipe_transfer positioned_read file_info config_save periodic_timer
	random_uuid mapped_file file_attributes watch_directory capture_process
	delayed_task duplex_message obfuscated_string localized_time
	signal_notifications settings profile_intervals atomic_publication
	common_tasks variant find_files coroutine ordered_map)
	add_executable(hx_os_${example} ${example}.c)
	target_link_libraries(hx_os_${example} PRIVATE hx_c_platform)
	set_target_properties(hx_os_${example} PROPERTIES
		C_STANDARD 99 C_STANDARD_REQUIRED YES C_EXTENSIONS NO
		RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
	hx_apply_warnings(hx_os_${example})
endforeach()
if(HX_OS_BUILD_TESTS)
	add_test(NAME ordered_map_example COMMAND hx_os_ordered_map)
	set_tests_properties(ordered_map_example PROPERTIES TIMEOUT 20)
	add_test(NAME coroutine_example COMMAND hx_os_coroutine)
	set_tests_properties(coroutine_example PROPERTIES TIMEOUT 20)
	add_test(NAME variant_example COMMAND hx_os_variant)
	set_tests_properties(variant_example PROPERTIES TIMEOUT 20)
	add_test(NAME common_tasks_example COMMAND hx_os_common_tasks)
	set_tests_properties(common_tasks_example PROPERTIES TIMEOUT 20)
	add_test(NAME atomic_publication_example
		COMMAND hx_os_atomic_publication)
	set_tests_properties(atomic_publication_example PROPERTIES TIMEOUT 20)
endif()
if(HX_OS_BUILD_TESTS AND HX_OS_ENABLE_FILE_UTILS
		AND HX_OS_ENABLE_SYSTEM_UTILS)
	set(hx_search_fixture "${CMAKE_CURRENT_BINARY_DIR}/find-中文")
	file(MAKE_DIRECTORY "${hx_search_fixture}/nested/deeper")
	file(WRITE "${hx_search_fixture}/first.txt" "first\n")
	file(WRITE "${hx_search_fixture}/nested/deeper/😀.txt" "second\n")
	add_test(NAME find_files_example COMMAND hx_os_find_files
		"${hx_search_fixture}" "*.txt")
	set_tests_properties(find_files_example PROPERTIES
		TIMEOUT 20 PASS_REGULAR_EXPRESSION "Files: 2")
endif()
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
if(HX_OS_BUILD_TESTS AND HX_OS_ENABLE_DUPLEX_PIPE)
	add_test(NAME duplex_message_example COMMAND hx_os_duplex_message)
	set_tests_properties(duplex_message_example PROPERTIES TIMEOUT 20)
endif()
if(HX_OS_BUILD_TESTS AND HX_OS_ENABLE_OBFUSCATION)
	add_test(NAME obfuscated_string_example COMMAND hx_os_obfuscated_string)
	set_tests_properties(obfuscated_string_example PROPERTIES TIMEOUT 20)
endif()
if(HX_OS_BUILD_TESTS AND HX_OS_ENABLE_CALENDAR AND HX_OS_ENABLE_TEXT_LOOKUP)
	add_test(NAME localized_time_example COMMAND hx_os_localized_time)
	set_tests_properties(localized_time_example PROPERTIES TIMEOUT 20)
endif()
if(HX_OS_BUILD_TESTS AND HX_OS_ENABLE_SIGNALS)
	add_test(NAME signal_notifications_example
		COMMAND hx_os_signal_notifications)
	set_tests_properties(signal_notifications_example PROPERTIES TIMEOUT 20)
endif()
if(HX_OS_BUILD_TESTS AND HX_OS_ENABLE_SETTINGS)
	add_test(NAME settings_example COMMAND hx_os_settings)
	set_tests_properties(settings_example PROPERTIES TIMEOUT 20)
endif()
if(HX_OS_BUILD_TESTS AND HX_OS_ENABLE_PROFILER)
	add_test(NAME profile_intervals_example COMMAND hx_os_profile_intervals)
	set_tests_properties(profile_intervals_example PROPERTIES TIMEOUT 20)
endif()
if(CMAKE_CXX_COMPILER_LOADED)
	add_executable(hx_os_cpp_coroutine coroutine.cpp)
	target_link_libraries(hx_os_cpp_coroutine PRIVATE hx_c_platform::cpp)
	set_target_properties(hx_os_cpp_coroutine PROPERTIES
		CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO
		RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
	hx_apply_warnings(hx_os_cpp_coroutine)
	if(HX_OS_BUILD_TESTS)
		add_test(NAME cpp_coroutine_example COMMAND hx_os_cpp_coroutine)
		set_tests_properties(cpp_coroutine_example
			PROPERTIES TIMEOUT 20)
	endif()
	if(HX_OS_HAVE_CPP20_COROUTINES)
		add_executable(hx_os_cpp_coroutine20 coroutine20.cpp)
		target_link_libraries(hx_os_cpp_coroutine20 PRIVATE
			hx_c_platform::cpp20)
		set_target_properties(hx_os_cpp_coroutine20 PROPERTIES
			CXX_STANDARD 20 CXX_STANDARD_REQUIRED YES
			CXX_EXTENSIONS NO
			RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
		hx_apply_warnings(hx_os_cpp_coroutine20)
		if(HX_OS_BUILD_TESTS)
			add_test(NAME cpp_coroutine20_example
				COMMAND hx_os_cpp_coroutine20)
			set_tests_properties(cpp_coroutine20_example
				PROPERTIES TIMEOUT 20)
		endif()
	endif()
	add_executable(hx_os_cpp_find_files find_files_async.cpp)
	target_link_libraries(hx_os_cpp_find_files PRIVATE hx_c_platform::cpp)
	set_target_properties(hx_os_cpp_find_files PROPERTIES
		CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO
		RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
	hx_apply_warnings(hx_os_cpp_find_files)
	if(HX_OS_BUILD_TESTS AND HX_OS_ENABLE_FILE_UTILS
			AND HX_OS_ENABLE_SYSTEM_UTILS
			AND HX_OS_ENABLE_THREAD_POOL AND HX_OS_HAVE_TIMED_JOIN)
		add_test(NAME cpp_find_files_example
			COMMAND hx_os_cpp_find_files
			"${hx_search_fixture}" "*.txt")
		set_tests_properties(cpp_find_files_example PROPERTIES
			TIMEOUT 20 PASS_REGULAR_EXPRESSION "Files: 2")
	endif()
	add_executable(hx_os_cpp_variant variant.cpp)
	target_link_libraries(hx_os_cpp_variant PRIVATE hx_c_platform::cpp)
	set_target_properties(hx_os_cpp_variant PROPERTIES
		CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO
		RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
	hx_apply_warnings(hx_os_cpp_variant)
	if(HX_OS_BUILD_TESTS)
		add_test(NAME cpp_variant_example COMMAND hx_os_cpp_variant)
		set_tests_properties(cpp_variant_example PROPERTIES TIMEOUT 20)
	endif()
	add_executable(hx_os_cpp_thread_pool thread_pool.cpp)
	target_link_libraries(hx_os_cpp_thread_pool PRIVATE hx_c_platform::cpp)
	set_target_properties(hx_os_cpp_thread_pool PROPERTIES
		CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO
		RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
	hx_apply_warnings(hx_os_cpp_thread_pool)
	add_executable(hx_os_cpp_direct_calls direct_calls.cpp)
	target_link_libraries(hx_os_cpp_direct_calls PRIVATE hx_c_platform::cpp)
	set_target_properties(hx_os_cpp_direct_calls PROPERTIES
		CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO
		RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
	hx_apply_warnings(hx_os_cpp_direct_calls)
	if(HX_OS_BUILD_TESTS)
		add_test(NAME cpp_direct_calls_example
			COMMAND hx_os_cpp_direct_calls)
		set_tests_properties(cpp_direct_calls_example
			PROPERTIES TIMEOUT 20)
	endif()
endif()
