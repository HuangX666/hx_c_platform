include_guard(GLOBAL)

set(HX_OS_HAVE_HTTP 0)
set(HX_OS_HAVE_URL 0)
set(HX_OS_HAVE_UNICODE 0)
set(hx_sdk_prefix_save "${CMAKE_PREFIX_PATH}")
set(hx_sdk_roots_save "${CMAKE_FIND_ROOT_PATH}")
if(HX_OS_DEPENDENCY_PREFIX)
	list(PREPEND CMAKE_PREFIX_PATH "${HX_OS_DEPENDENCY_PREFIX}")
	list(PREPEND CMAKE_FIND_ROOT_PATH "${HX_OS_DEPENDENCY_PREFIX}")
endif()

if(HX_OS_ENABLE_HTTP OR HX_OS_ENABLE_URL)
	find_package(CURL 8.22 CONFIG REQUIRED)
	add_library(hx_c_platform_curl INTERFACE)
	add_library(hx_c_platform::curl ALIAS hx_c_platform_curl)
	set_target_properties(hx_c_platform_curl PROPERTIES EXPORT_NAME curl)
	target_link_libraries(hx_c_platform_curl INTERFACE CURL::libcurl)
	install(TARGETS hx_c_platform_curl EXPORT hx_c_platformTargets)
	hx_install_native_headers(CURL::libcurl curl)
	install(DIRECTORY "${CURL_DIR}/"
		DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/CURL")
	set(HX_OS_HAVE_HTTP ${HX_OS_ENABLE_HTTP})
	set(HX_OS_HAVE_URL ${HX_OS_ENABLE_URL})
	if(EXISTS "${PROJECT_SOURCE_DIR}/deps/curl/COPYING")
		install(FILES "${PROJECT_SOURCE_DIR}/deps/curl/COPYING"
			DESTINATION "${CMAKE_INSTALL_DATADIR}/hx-dependencies"
			RENAME curl-COPYING)
	endif()
endif()

if(HX_OS_ENABLE_UNICODE)
	find_package(utf8proc 2.12 CONFIG REQUIRED)
	add_library(hx_c_platform_utf8proc INTERFACE)
	add_library(hx_c_platform::utf8proc ALIAS hx_c_platform_utf8proc)
	set_target_properties(hx_c_platform_utf8proc PROPERTIES
		EXPORT_NAME utf8proc)
	target_link_libraries(hx_c_platform_utf8proc INTERFACE
		utf8proc::utf8proc)
	install(TARGETS hx_c_platform_utf8proc EXPORT hx_c_platformTargets)
	hx_install_native_headers(utf8proc::utf8proc utf8proc.h)
	install(DIRECTORY "${utf8proc_DIR}/"
		DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/utf8proc")
	set(HX_OS_HAVE_UNICODE 1)
	if(EXISTS "${PROJECT_SOURCE_DIR}/deps/utf8proc/LICENSE.md")
		install(FILES "${PROJECT_SOURCE_DIR}/deps/utf8proc/LICENSE.md"
			DESTINATION "${CMAKE_INSTALL_DATADIR}/hx-dependencies"
			RENAME utf8proc-LICENSE.md)
	endif()
endif()

set(CMAKE_PREFIX_PATH "${hx_sdk_prefix_save}")
set(CMAKE_FIND_ROOT_PATH "${hx_sdk_roots_save}")

function(hx_link_transfer_sdk target)
	set(native_targets "")
	set(runtime_manifest "")
	set(runtime "${PROJECT_BINARY_DIR}/bin")
	if(CMAKE_CONFIGURATION_TYPES)
		string(APPEND runtime "/$<CONFIG>")
	endif()
	if(HX_OS_HAVE_HTTP OR HX_OS_HAVE_URL)
		list(APPEND native_targets CURL::libcurl)
	endif()
	if(HX_OS_HAVE_UNICODE)
		list(APPEND native_targets utf8proc::utf8proc)
	endif()
	foreach(native IN LISTS native_targets)
		string(APPEND runtime_manifest "$<TARGET_FILE:${native}>\n")
		target_link_libraries(${target} PRIVATE ${native})
		if(WIN32)
			add_custom_command(TARGET ${target} POST_BUILD
				COMMAND ${CMAKE_COMMAND} -E make_directory
					"${runtime}"
				COMMAND ${CMAKE_COMMAND} -E copy_if_different
					"$<TARGET_FILE:${native}>"
					"${runtime}"
				VERBATIM)
			install(FILES "$<TARGET_FILE:${native}>"
				DESTINATION "${CMAKE_INSTALL_BINDIR}")
			install(FILES "$<TARGET_LINKER_FILE:${native}>"
				DESTINATION "${CMAKE_INSTALL_LIBDIR}")
		else()
			if(native STREQUAL "CURL::libcurl")
				set(stem curl)
			else()
				set(stem utf8proc)
			endif()
			set(link_file "$<TARGET_FILE_DIR:${native}>/"
				"${CMAKE_SHARED_LIBRARY_PREFIX}${stem}"
				"${CMAKE_SHARED_LIBRARY_SUFFIX}")
			string(JOIN "" link_file ${link_file})
			set(destination "\${CMAKE_INSTALL_PREFIX}/")
			string(APPEND destination "${CMAKE_INSTALL_LIBDIR}")
			install(CODE "set(link_file \"${link_file}\")
				if(NOT EXISTS \"\${link_file}\")
					set(link_file
						\"$<TARGET_FILE:${native}>\")
				endif()
				file(INSTALL
				DESTINATION \"${destination}\"
				TYPE SHARED_LIBRARY FOLLOW_SYMLINK_CHAIN
				FILES \"\${link_file}\")")
			if(APPLE)
				set(origin "@loader_path")
			else()
				set(origin "$ORIGIN")
			endif()
			set_property(TARGET ${target} APPEND PROPERTY
				INSTALL_RPATH "${origin}")
		endif()
	endforeach()
	file(GENERATE OUTPUT
		"${PROJECT_BINARY_DIR}/hx_transfer_runtime_$<CONFIG>.txt"
		CONTENT "${runtime_manifest}")
endfunction()
