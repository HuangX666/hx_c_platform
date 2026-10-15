set(HX_OS_DEPENDENCY_PREFIX "" CACHE PATH "Prepared optional dependency SDK")
option(HX_OS_OPENSSL_STATIC
	"Link OpenSSL statically (explicit TLS cleanup)" OFF)
option(HX_OS_INSTALL_OPENSSL_RUNTIME
	"Install selected shared OpenSSL runtime" ON)

function(hx_find_openssl)
	if(HX_OS_DEPENDENCY_PREFIX)
		list(PREPEND CMAKE_PREFIX_PATH "${HX_OS_DEPENDENCY_PREFIX}")
		list(PREPEND CMAKE_FIND_ROOT_PATH "${HX_OS_DEPENDENCY_PREFIX}")
	endif()

	set(OPENSSL_USE_STATIC_LIBS ${HX_OS_OPENSSL_STATIC})
	find_package(OpenSSL 3.5 CONFIG QUIET)
	if(NOT OpenSSL_FOUND OR NOT TARGET OpenSSL::Crypto)
		message(FATAL_ERROR
			"OpenSSL 3.5+ SDK required by crypto/archive/curl. "
			"Build tools/dependencies, then set "
			"HX_OS_DEPENDENCY_PREFIX to its prefix. "
			"Disable CRYPTO/ARCHIVE/HTTP/URL to omit OpenSSL.")
	endif()

	get_target_property(kind OpenSSL::Crypto TYPE)
	if(HX_OS_OPENSSL_STATIC AND NOT kind STREQUAL "STATIC_LIBRARY")
		message(FATAL_ERROR "The selected OpenSSL SDK is not static")
	elseif(NOT HX_OS_OPENSSL_STATIC AND NOT kind STREQUAL "SHARED_LIBRARY")
		message(FATAL_ERROR "The selected OpenSSL SDK is not shared. "
			"Prepare it with HX_DEPS_OPENSSL_SHARED=ON.")
	endif()
endfunction()

if(HX_OS_HAVE_OPENSSL)
	hx_find_openssl()
	add_library(hx_c_platform_openssl INTERFACE)
	add_library(hx_c_platform::openssl ALIAS hx_c_platform_openssl)
	set_target_properties(hx_c_platform_openssl PROPERTIES
		EXPORT_NAME openssl)
	target_link_libraries(hx_c_platform_openssl INTERFACE
		OpenSSL::Crypto OpenSSL::SSL)
	install(TARGETS hx_c_platform_openssl EXPORT hx_c_platformTargets)
	hx_install_native_headers(OpenSSL::Crypto openssl)
	install(DIRECTORY "${OpenSSL_DIR}/"
		DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/OpenSSL")
	foreach(native OpenSSL::Crypto OpenSSL::SSL)
		if(WIN32 AND NOT HX_OS_OPENSSL_STATIC)
			install(FILES "$<TARGET_LINKER_FILE:${native}>"
				DESTINATION "${CMAKE_INSTALL_LIBDIR}")
		elseif(HX_OS_OPENSSL_STATIC)
			install(FILES "$<TARGET_FILE:${native}>"
				DESTINATION "${CMAKE_INSTALL_LIBDIR}")
		endif()
	endforeach()
	set(HX_OS_PC_REQUIRES_PRIVATE "libcrypto >= 3.5")
	if(HX_OS_HAVE_TLS OR HX_OS_ENABLE_HTTP OR HX_OS_ENABLE_URL)
		string(APPEND HX_OS_PC_REQUIRES_PRIVATE ", libssl >= 3.5")
	endif()
endif()

function(hx_link_crypto target)
	if(NOT HX_OS_HAVE_OPENSSL)
		return()
	endif()

	target_link_libraries(${target} PRIVATE OpenSSL::Crypto)
	set(hx_ssl_targets OpenSSL::Crypto OpenSSL::SSL)
	if(HX_OS_HAVE_TLS)
		target_link_libraries(${target} PRIVATE OpenSSL::SSL)
		if(APPLE)
			target_link_libraries(${target} PRIVATE
				"-framework Security"
				"-framework CoreFoundation")
		endif()
	endif()
	if(EXISTS "${HX_OS_DEPENDENCY_PREFIX}/share/hx-dependencies")
		install(DIRECTORY
			"${HX_OS_DEPENDENCY_PREFIX}/share/hx-dependencies"
			DESTINATION "${CMAKE_INSTALL_DATADIR}")
	endif()
	if(EXISTS "${PROJECT_SOURCE_DIR}/deps/openssl/LICENSE.txt")
		install(FILES "${PROJECT_SOURCE_DIR}/deps/openssl/LICENSE.txt"
			DESTINATION "${CMAKE_INSTALL_DATADIR}/hx-dependencies"
			RENAME OpenSSL-LICENSE.txt)
	endif()
	if(HX_OS_OPENSSL_STATIC)
		if(UNIX AND NOT APPLE AND BUILD_SHARED_LIBS)
			target_link_options(${target} PRIVATE
				"-Wl,--exclude-libs,ALL")
		endif()
		return()
	endif()

	if(WIN32)
		set(runtime "${PROJECT_BINARY_DIR}/bin")
		if(CMAKE_CONFIGURATION_TYPES)
			string(APPEND runtime "/$<CONFIG>")
		endif()
		set(destination "${CMAKE_INSTALL_BINDIR}")
	else()
		set(destination "${CMAKE_INSTALL_LIBDIR}")
		if(APPLE)
			set(rpath "@loader_path")
		else()
			set(rpath "$ORIGIN")
		endif()
		set_property(TARGET ${target} APPEND PROPERTY INSTALL_RPATH
			"${rpath}")
		if(ANDROID)
			# NDK CMake suppresses ordinary build RPATH flags.
			target_link_options(${target} PRIVATE
				"-Wl,-rpath,$ORIGIN")
		endif()
	endif()

	set(hx_ssl_manifest "")
	foreach(hx_ssl IN LISTS hx_ssl_targets)
		set(runtime_file "$<TARGET_FILE:${hx_ssl}>")
		if(ANDROID)
			# Resolve the runtime; installation follows the chain.
			get_target_property(hx_runtime ${hx_ssl}
				IMPORTED_LOCATION)
			if(hx_runtime AND EXISTS "${hx_runtime}")
				get_filename_component(runtime_file
					"${hx_runtime}"
					REALPATH)
			endif()
		endif()
		if(WIN32)
			add_custom_command(TARGET ${target} POST_BUILD
				COMMAND ${CMAKE_COMMAND} -E make_directory
					"${runtime}"
				COMMAND ${CMAKE_COMMAND} -E copy_if_different
					"${runtime_file}" "${runtime}"
				VERBATIM)
		endif()
		if(NOT HX_OS_INSTALL_OPENSSL_RUNTIME)
			continue()
		endif()
		string(APPEND hx_ssl_manifest "${runtime_file}\n")
		if(WIN32)
			install(FILES "${runtime_file}"
				DESTINATION "${destination}")
		else()
			# Include the entire symlink chain and actual runtime.
			set(install_to
				"\${CMAKE_INSTALL_PREFIX}/${destination}")
			install(CODE "file(INSTALL
				DESTINATION \"${install_to}\"
				TYPE SHARED_LIBRARY FOLLOW_SYMLINK_CHAIN
				FILES \"${runtime_file}\")")
		endif()
	endforeach()
	if(HX_OS_INSTALL_OPENSSL_RUNTIME)
		set(manifest "hx_openssl_runtime_$<CONFIG>.txt")
		file(GENERATE OUTPUT "${PROJECT_BINARY_DIR}/${manifest}"
			CONTENT "${hx_ssl_manifest}")
	endif()
endfunction()
