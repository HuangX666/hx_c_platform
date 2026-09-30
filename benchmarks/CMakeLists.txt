add_executable(hx_os_benchmark benchmark.c)
target_link_libraries(hx_os_benchmark PRIVATE hx_c_platform)
set_target_properties(hx_os_benchmark PROPERTIES
	C_STANDARD 99 C_STANDARD_REQUIRED YES C_EXTENSIONS NO
	RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
hx_apply_warnings(hx_os_benchmark)

