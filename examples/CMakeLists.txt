function(hx_add_serialization_interop)
	if(CMAKE_CROSSCOMPILING)
		return()
	endif()

	find_package(Python3 QUIET COMPONENTS Interpreter)
	if(NOT Python3_Interpreter_FOUND)
		return()
	endif()

	set(hx_serialization_program
		"${PROJECT_SOURCE_DIR}/tests/test_serialization_interop.py")
	set(hx_serialization_args
		--example $<TARGET_FILE:hx_os_serialization>
		--nested $<TARGET_FILE:hx_os_nested_document>)

	if(BUILD_SHARED_LIBS AND NOT HX_OS_SANITIZER)
		list(APPEND hx_serialization_args
			--library $<TARGET_FILE:hx_c_platform>)
	endif()

	add_test(NAME serialization_interop
		COMMAND "${Python3_EXECUTABLE}"
		"${hx_serialization_program}" ${hx_serialization_args})
	set_tests_properties(serialization_interop PROPERTIES TIMEOUT 60)
endfunction()

foreach(example IN ITEMS hello async_timer configuration containers system_info
	load_plugin text_file environment special_paths thread_options
	binary_stream buffered_file config_edit
	serialization nested_document codec_macros
	pipe_transfer positioned_read file_info config_save periodic_timer
	random_uuid mapped_file file_attributes watch_directory capture_process
	delayed_task duplex_message obfuscated_string localized_time
	signal_notifications parameters settings profile_intervals
	profile_statistics
	atomic_publication
	common_tasks variant find_files coroutine ordered_map format
	ipc_server ipc_client ipc_device_server ipc_device_client
	tcp_order_server tcp_order_client udp_dashboard udp_sensor
	kcp_transfer_server kcp_transfer_client)
	add_executable(hx_os_${example} ${example}.c)
	target_link_libraries(hx_os_${example} PRIVATE hx_c_platform)
	set_target_properties(hx_os_${example} PROPERTIES
		C_STANDARD 99
		C_STANDARD_REQUIRED YES
		C_EXTENSIONS NO
		RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
	hx_apply_warnings(hx_os_${example})
endforeach()

if(HX_OS_BUILD_TESTS AND HX_OS_ENABLE_STREAM)
	add_test(NAME binary_stream_example COMMAND hx_os_binary_stream)
	set_tests_properties(binary_stream_example PROPERTIES TIMEOUT 20)
	if(HX_OS_ENABLE_BUFFERED_FILE)
		add_test(NAME buffered_file_example
			COMMAND hx_os_buffered_file)
		set_tests_properties(buffered_file_example PROPERTIES
			TIMEOUT 20)
	endif()
endif()

if(HX_OS_BUILD_TESTS AND HX_OS_ENABLE_CONFIG AND HX_OS_ENABLE_CODEC
	AND HX_OS_ENABLE_CONFIG_SERIALIZE)
	add_test(NAME codec_macros_example COMMAND hx_os_codec_macros)
	set_tests_properties(codec_macros_example PROPERTIES TIMEOUT 20)
endif()

if(HX_OS_BUILD_TESTS AND HX_OS_ENABLE_CONFIG AND HX_OS_ENABLE_CONFIG_SERIALIZE
	AND HX_OS_ENABLE_FILE_UTILS)
	add_test(NAME config_edit_example COMMAND hx_os_config_edit)
	set_tests_properties(config_edit_example PROPERTIES TIMEOUT 20)
endif()

if(HX_OS_BUILD_TESTS AND HX_OS_HAVE_IPC AND NOT CMAKE_CROSSCOMPILING)
	find_package(Python3 QUIET COMPONENTS Interpreter)
	if(Python3_Interpreter_FOUND)
		set(hx_ipc_example_args
			--server $<TARGET_FILE:hx_os_ipc_server>
			--client $<TARGET_FILE:hx_os_ipc_client>
			--python-peer
			"${CMAKE_CURRENT_SOURCE_DIR}/ipc_python.py")

		if(CMAKE_CXX_COMPILER_LOADED)
			list(APPEND hx_ipc_example_args
				--threads $<TARGET_FILE:hx_os_ipc_threads>)
		endif()

		if(HX_OS_ENABLE_KCP)
			list(APPEND hx_ipc_example_args --kcp)
		endif()

		if((WIN32 AND HX_OS_ENABLE_PIPE) OR
			(HX_OS_ENABLE_LOCAL_SOCKETS AND HX_OS_ENABLE_LOOP_FD))
			list(APPEND hx_ipc_example_args --local)
		endif()

		add_test(NAME ipc_examples COMMAND "${Python3_EXECUTABLE}"
			"${PROJECT_SOURCE_DIR}/tests/test_ipc_examples.py"
			${hx_ipc_example_args})
		set_tests_properties(ipc_examples PROPERTIES TIMEOUT 90)
	endif()
endif()

if(HX_OS_BUILD_TESTS AND NOT CMAKE_CROSSCOMPILING AND HX_OS_ENABLE_CONFIG
		AND HX_OS_ENABLE_CONFIG_SERIALIZE AND HX_OS_ENABLE_SETTINGS)
	find_package(Python3 QUIET COMPONENTS Interpreter)
	if(Python3_Interpreter_FOUND)
		function(hx_add_business_test name script target)
			add_test(NAME ${name} COMMAND "${Python3_EXECUTABLE}"
				"${CMAKE_CURRENT_SOURCE_DIR}/${script}"
				--bin-dir $<TARGET_FILE_DIR:${target}> ${ARGN})
			set_tests_properties(${name} PROPERTIES TIMEOUT 45)
		endfunction()

		if(HX_OS_HAVE_IPC AND ((WIN32 AND HX_OS_ENABLE_PIPE) OR
			(HX_OS_ENABLE_LOCAL_SOCKETS AND HX_OS_ENABLE_LOOP_FD)))
			hx_add_business_test(ipc_business_local
				run_ipc_business.py hx_os_ipc_device_server)
		endif()

		if(HX_OS_ENABLE_NETWORK)
			hx_add_business_test(tcp_business_orders
				run_tcp_business.py hx_os_tcp_order_server)
			hx_add_business_test(tcp_business_framing
				run_tcp_business.py hx_os_tcp_order_server
				--fragmented)
			hx_add_business_test(tcp_business_json_validation
				run_tcp_business.py hx_os_tcp_order_server
				--json-errors)
			hx_add_business_test(udp_business_samples
				run_udp_business.py hx_os_udp_dashboard)
			hx_add_business_test(udp_business_gaps
				run_udp_business.py hx_os_udp_dashboard
				--simulate-gaps)

			if(HX_OS_ENABLE_KCP)
				hx_add_business_test(kcp_business_transfer
					run_kcp_business.py
					hx_os_kcp_transfer_server)
				hx_add_business_test(kcp_business_loss
					run_kcp_business.py
					hx_os_kcp_transfer_server
					--drop-first)
			endif()
		endif()
	endif()
endif()

if(HX_OS_BUILD_TESTS)
	add_test(NAME format_example COMMAND hx_os_format)
	set_tests_properties(format_example PROPERTIES TIMEOUT 20)
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

if(HX_OS_TEST_FORMAT_SYNTAX)
	add_executable(hx_os_format_syntax format_syntax.c)
	target_link_libraries(hx_os_format_syntax PRIVATE hx_c_platform)
	set_target_properties(hx_os_format_syntax PROPERTIES
		C_STANDARD 99
		C_STANDARD_REQUIRED YES
		C_EXTENSIONS NO
		RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
	hx_apply_warnings(hx_os_format_syntax)
	hx_enable_format_syntax(hx_os_format_syntax SOURCES format_syntax.c)

	if(HX_OS_BUILD_TESTS)
		add_test(NAME format_syntax_example COMMAND hx_os_format_syntax)
		set_tests_properties(format_syntax_example
			PROPERTIES TIMEOUT 20)
	endif()
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

if(HX_OS_BUILD_TESTS AND HX_OS_ENABLE_PARAMETERS)
	add_test(NAME parameters_example COMMAND hx_os_parameters)
	set_tests_properties(parameters_example PROPERTIES TIMEOUT 20)
endif()

if(HX_OS_BUILD_TESTS AND HX_OS_ENABLE_SETTINGS)
	add_test(NAME settings_example COMMAND hx_os_settings)
	set_tests_properties(settings_example PROPERTIES TIMEOUT 20)
endif()

if(HX_OS_BUILD_TESTS AND HX_OS_ENABLE_PROFILER)
	add_test(NAME profile_intervals_example COMMAND hx_os_profile_intervals)
	set_tests_properties(profile_intervals_example PROPERTIES TIMEOUT 20)
	add_test(NAME profile_statistics_example
		COMMAND hx_os_profile_statistics)
	set_tests_properties(profile_statistics_example PROPERTIES
		TIMEOUT 20)
endif()

if(CMAKE_CXX_COMPILER_LOADED)
	add_executable(hx_os_ipc_threads ipc_threads.cpp)
	target_link_libraries(hx_os_ipc_threads PRIVATE hx_c_platform::cpp)
	set_target_properties(hx_os_ipc_threads PROPERTIES
		CXX_STANDARD 11
		CXX_STANDARD_REQUIRED YES
		CXX_EXTENSIONS NO
		RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
	hx_apply_warnings(hx_os_ipc_threads)

	add_executable(hx_os_go go.cpp)
	target_link_libraries(hx_os_go PRIVATE hx_c_platform::cpp)
	set_target_properties(hx_os_go PROPERTIES
		CXX_STANDARD 11
		CXX_STANDARD_REQUIRED YES
		CXX_EXTENSIONS NO
		RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
	hx_apply_warnings(hx_os_go)

	if(HX_OS_BUILD_TESTS)
		add_test(NAME go_example COMMAND hx_os_go)
		set_tests_properties(go_example PROPERTIES TIMEOUT 20)
	endif()

	add_executable(hx_os_cpp_coroutine coroutine.cpp)
	target_link_libraries(hx_os_cpp_coroutine PRIVATE hx_c_platform::cpp)
	set_target_properties(hx_os_cpp_coroutine PROPERTIES
		CXX_STANDARD 11
		CXX_STANDARD_REQUIRED YES
		CXX_EXTENSIONS NO
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
			CXX_STANDARD 20
			CXX_STANDARD_REQUIRED YES
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
		CXX_STANDARD 11
		CXX_STANDARD_REQUIRED YES
		CXX_EXTENSIONS NO
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
		CXX_STANDARD 11
		CXX_STANDARD_REQUIRED YES
		CXX_EXTENSIONS NO
		RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
	hx_apply_warnings(hx_os_cpp_variant)

	if(HX_OS_BUILD_TESTS)
		add_test(NAME cpp_variant_example COMMAND hx_os_cpp_variant)
		set_tests_properties(cpp_variant_example PROPERTIES TIMEOUT 20)
	endif()

	add_executable(hx_os_cpp_thread_pool thread_pool.cpp)
	target_link_libraries(hx_os_cpp_thread_pool PRIVATE hx_c_platform::cpp)
	set_target_properties(hx_os_cpp_thread_pool PROPERTIES
		CXX_STANDARD 11
		CXX_STANDARD_REQUIRED YES
		CXX_EXTENSIONS NO
		RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
	hx_apply_warnings(hx_os_cpp_thread_pool)

	add_executable(hx_os_cpp_serialization serialization.cpp)
	target_link_libraries(hx_os_cpp_serialization PRIVATE
		hx_c_platform::cpp)
	set_target_properties(hx_os_cpp_serialization PROPERTIES
		CXX_STANDARD 11
		CXX_STANDARD_REQUIRED YES
		CXX_EXTENSIONS NO
		RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
	hx_apply_warnings(hx_os_cpp_serialization)

	if(HX_OS_BUILD_TESTS AND HX_OS_ENABLE_CONFIG AND HX_OS_ENABLE_CODEC
		AND HX_OS_ENABLE_CONFIG_SERIALIZE)
		add_test(NAME cpp_serialization_example
			COMMAND hx_os_cpp_serialization)
		add_test(NAME nested_document_example
			COMMAND hx_os_nested_document)
		hx_add_serialization_interop()

		foreach(format IN ITEMS json yaml xml toml)
			add_test(NAME serialization_${format}_example
				COMMAND hx_os_serialization ${format})
		endforeach()
	endif()

	add_executable(hx_os_cpp_direct_calls direct_calls.cpp)
	target_link_libraries(hx_os_cpp_direct_calls PRIVATE hx_c_platform::cpp)
	set_target_properties(hx_os_cpp_direct_calls PROPERTIES
		CXX_STANDARD 11
		CXX_STANDARD_REQUIRED YES
		CXX_EXTENSIONS NO
		RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
	hx_apply_warnings(hx_os_cpp_direct_calls)

	if(HX_OS_BUILD_TESTS)
		add_test(NAME cpp_direct_calls_example
			COMMAND hx_os_cpp_direct_calls)
		set_tests_properties(cpp_direct_calls_example
			PROPERTIES TIMEOUT 20)
	endif()
endif()
