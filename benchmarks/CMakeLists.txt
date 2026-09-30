add_executable(hx_os_benchmark benchmark.c)
target_link_libraries(hx_os_benchmark PRIVATE hx_c_platform)
set_target_properties(hx_os_benchmark PROPERTIES
	C_STANDARD 99 C_STANDARD_REQUIRED YES C_EXTENSIONS NO
	RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
hx_apply_warnings(hx_os_benchmark)

add_executable(hx_os_layout_report ../tools/report_layout.c)
target_include_directories(hx_os_layout_report PRIVATE
	${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
	${PROJECT_SOURCE_DIR}/${hx_private_include}
	${PROJECT_BINARY_DIR}/private)
if(WIN32)
	target_compile_definitions(hx_os_layout_report PRIVATE
		_WIN32_WINNT=0x0601 WINVER=0x0601 WIN32_LEAN_AND_MEAN)
else()
	target_compile_definitions(hx_os_layout_report PRIVATE
		_GNU_SOURCE _FILE_OFFSET_BITS=64)
endif()
set_target_properties(hx_os_layout_report PROPERTIES
	C_STANDARD 99 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
hx_apply_warnings(hx_os_layout_report)
