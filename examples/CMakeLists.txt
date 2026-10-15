set(CMAKE_FOLDER "examples/demos")

foreach(example IN ITEMS hello async_timer configuration containers system_info
	byte_queue_ring bitset lru_cache message_queue algorithms
	text_utilities encoding read_lines control coordination integer_tools
	persistent_queue unicode_tools incremental_hash fast_algorithms
	regex once framed_messages compress_memory url_query command_line
	http_json http_file temporary_file serial_message shared_memory
	encrypt_text archive_files tls_json_client tls_json_server
	load_plugin text_file environment special_paths thread_options
	windows_info
	native_file
	sleep_inhibit
	binary_stream buffered_file config_edit
	serialization nested_document codec_macros
	pipe_transfer positioned_read file_info config_save periodic_timer
	random_uuid mapped_file file_attributes watch_directory capture_process
	delayed_task duplex_message obfuscated_string localized_time
	signal_notifications signal_observer signal_mutable parameters settings
	profile_intervals profile_statistics high_resolution_time
	atomic_publication
	common_tasks parse_script preprocess_config variant find_files
	coroutine coroutine_udp coroutine_http ordered_map
	format
	ipc_server ipc_client ipc_device_server ipc_device_client
	tcp_order_server tcp_order_client udp_dashboard udp_sensor
	network_interfaces multicast_discovery dns_lookup udp_packet
	kcp_transfer_server kcp_transfer_client kcp_session)
	add_executable(hx_os_${example} ${example}.c)
	target_link_libraries(hx_os_${example} PRIVATE hx_c_platform)
	set_target_properties(hx_os_${example} PROPERTIES
		C_STANDARD 99
		C_STANDARD_REQUIRED YES
		C_EXTENSIONS NO
		RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
	hx_apply_warnings(hx_os_${example})
endforeach()

add_library(hx_os_demo_plugin SHARED demo_plugin.c)
target_include_directories(hx_os_demo_plugin PRIVATE
	"${PROJECT_SOURCE_DIR}/include")
set_target_properties(hx_os_demo_plugin PROPERTIES
	C_STANDARD 99
	C_STANDARD_REQUIRED YES
	C_EXTENSIONS NO
	RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin"
	LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
hx_apply_warnings(hx_os_demo_plugin)

if(TARGET hx_c_platform::sqlite AND TARGET hx_c_platform::openssl
		AND TARGET hx_c_platform::curl
		AND TARGET hx_c_platform::utf8proc)
	add_executable(hx_os_native_dependencies native_dependencies.c)
	target_link_libraries(hx_os_native_dependencies PRIVATE
		hx_c_platform::native)
	if(TARGET hx_c_platform::archive)
		target_compile_definitions(hx_os_native_dependencies PRIVATE
			HX_OS_DEMO_ARCHIVE)
	endif()
	set_target_properties(hx_os_native_dependencies PROPERTIES
		C_STANDARD 99 C_STANDARD_REQUIRED YES C_EXTENSIONS NO
		RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
	hx_apply_warnings(hx_os_native_dependencies)
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
endif()

if(CMAKE_CXX_COMPILER_LOADED)
	add_executable(hx_os_regex_cpp regex_cpp.cpp)
	target_link_libraries(hx_os_regex_cpp PRIVATE hx_c_platform::cpp)
	set_target_properties(hx_os_regex_cpp PROPERTIES CXX_STANDARD 11
		CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO
		RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
	hx_apply_warnings(hx_os_regex_cpp)

	add_executable(hx_os_algorithms_cpp algorithms_cpp.cpp)
	target_link_libraries(hx_os_algorithms_cpp PRIVATE hx_c_platform::cpp)
	set_target_properties(hx_os_algorithms_cpp PROPERTIES
		CXX_STANDARD 11
		CXX_STANDARD_REQUIRED YES
		CXX_EXTENSIONS NO
		RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
	hx_apply_warnings(hx_os_algorithms_cpp)

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

	add_executable(hx_os_cpp_coroutine coroutine.cpp)
	target_link_libraries(hx_os_cpp_coroutine PRIVATE hx_c_platform::cpp)
	set_target_properties(hx_os_cpp_coroutine PROPERTIES
		CXX_STANDARD 11
		CXX_STANDARD_REQUIRED YES
		CXX_EXTENSIONS NO
		RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
	hx_apply_warnings(hx_os_cpp_coroutine)

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
	endif()

	add_executable(hx_os_cpp_find_files find_files_async.cpp)
	target_link_libraries(hx_os_cpp_find_files PRIVATE hx_c_platform::cpp)
	set_target_properties(hx_os_cpp_find_files PROPERTIES
		CXX_STANDARD 11
		CXX_STANDARD_REQUIRED YES
		CXX_EXTENSIONS NO
		RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
	hx_apply_warnings(hx_os_cpp_find_files)

	add_executable(hx_os_cpp_variant variant.cpp)
	target_link_libraries(hx_os_cpp_variant PRIVATE hx_c_platform::cpp)
	set_target_properties(hx_os_cpp_variant PROPERTIES
		CXX_STANDARD 11
		CXX_STANDARD_REQUIRED YES
		CXX_EXTENSIONS NO
		RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
	hx_apply_warnings(hx_os_cpp_variant)

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

	add_executable(hx_os_cpp_direct_calls direct_calls.cpp)
	target_link_libraries(hx_os_cpp_direct_calls PRIVATE hx_c_platform::cpp)
	set_target_properties(hx_os_cpp_direct_calls PROPERTIES
		CXX_STANDARD 11
		CXX_STANDARD_REQUIRED YES
		CXX_EXTENSIONS NO
		RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
	hx_apply_warnings(hx_os_cpp_direct_calls)
endif()
