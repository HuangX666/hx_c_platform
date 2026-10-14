function(hx_find_archive)
	if(HX_OS_DEPENDENCY_PREFIX)
		list(PREPEND CMAKE_PREFIX_PATH "${HX_OS_DEPENDENCY_PREFIX}")
		list(PREPEND CMAKE_FIND_ROOT_PATH "${HX_OS_DEPENDENCY_PREFIX}")
	endif()

	if(HX_OS_HAVE_ARCHIVE)
		find_package(LibArchive 3.8 REQUIRED)
		if(NOT TARGET LibArchive::LibArchive)
			add_library(LibArchive::LibArchive UNKNOWN IMPORTED)
			set_target_properties(LibArchive::LibArchive PROPERTIES
				IMPORTED_LOCATION "${LibArchive_LIBRARY}"
				INTERFACE_INCLUDE_DIRECTORIES
					"${LibArchive_INCLUDE_DIRS}")
		endif()
	endif()
	find_package(ZLIB CONFIG REQUIRED COMPONENTS static)
	find_package(liblzma CONFIG REQUIRED)
	find_package(zstd CONFIG REQUIRED)
endfunction()

if(HX_OS_HAVE_ARCHIVE OR HX_OS_HAVE_COMPRESSION)
	hx_find_archive()
	string(APPEND HX_OS_PC_REQUIRES_PRIVATE ", zlib, liblzma, libzstd")
	set(hx_archive_names zlib lzma zstd)
	set(hx_archive_targets ZLIB::ZLIBSTATIC
		liblzma::liblzma zstd::libzstd_static)
	if(HX_OS_HAVE_ARCHIVE)
		string(APPEND HX_OS_PC_REQUIRES_PRIVATE ", libarchive >= 3.8")
		list(APPEND hx_archive_names archive)
		list(APPEND hx_archive_targets LibArchive::LibArchive)
	endif()
	list(LENGTH hx_archive_names native_count)
	math(EXPR native_last "${native_count} - 1")
	foreach(index RANGE 0 ${native_last})
		list(GET hx_archive_names ${index} name)
		list(GET hx_archive_targets ${index} native)
		add_library(hx_c_platform_${name} INTERFACE)
		add_library(hx_c_platform::${name} ALIAS hx_c_platform_${name})
		set_target_properties(hx_c_platform_${name} PROPERTIES
			EXPORT_NAME ${name})
		target_link_libraries(hx_c_platform_${name} INTERFACE ${native})
		install(TARGETS hx_c_platform_${name}
			EXPORT hx_c_platformTargets)
		get_target_property(configurations ${native}
			IMPORTED_CONFIGURATIONS)
		foreach(configuration IN LISTS configurations)
			get_target_property(artifact ${native}
				IMPORTED_LOCATION_${configuration})
			if(artifact AND EXISTS "${artifact}")
				install(FILES "${artifact}"
					DESTINATION "${CMAKE_INSTALL_LIBDIR}")
			endif()
		endforeach()
		install(FILES "$<TARGET_FILE:${native}>"
			DESTINATION "${CMAKE_INSTALL_LIBDIR}")
	endforeach()
	if(HX_OS_HAVE_ARCHIVE)
		target_compile_definitions(hx_c_platform_archive INTERFACE
			LIBARCHIVE_STATIC)
		target_link_libraries(hx_c_platform_archive INTERFACE
			hx_c_platform_zlib hx_c_platform_lzma hx_c_platform_zstd
			OpenSSL::Crypto ${CMAKE_DL_LIBS})
		if(WIN32)
			target_link_libraries(hx_c_platform_archive INTERFACE
				bcrypt)
		endif()
	endif()
	hx_install_native_headers(ZLIB::ZLIBSTATIC zlib.h zconf.h)
	hx_install_native_headers(liblzma::liblzma lzma.h lzma)
	hx_install_native_headers(zstd::libzstd_static
		zstd.h zstd_errors.h zdict.h)
	if(HX_OS_HAVE_ARCHIVE)
		hx_install_native_headers(LibArchive::LibArchive
			archive.h archive_entry.h)
	endif()
	install(DIRECTORY "${ZLIB_DIR}/"
		DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/zlib")
	install(DIRECTORY "${liblzma_DIR}/"
		DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/liblzma")
	install(DIRECTORY "${zstd_DIR}/"
		DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/zstd")
endif()

function(hx_link_archive target)
	if(HX_OS_HAVE_COMPRESSION)
		target_link_libraries(${target} PRIVATE ZLIB::ZLIBSTATIC
			liblzma::liblzma zstd::libzstd_static)
	endif()
	if(NOT HX_OS_HAVE_ARCHIVE)
		return()
	endif()

	target_compile_definitions(${target} PRIVATE LIBARCHIVE_STATIC)
	target_link_libraries(${target} PRIVATE LibArchive::LibArchive
		ZLIB::ZLIBSTATIC liblzma::liblzma zstd::libzstd_static
		OpenSSL::Crypto ${CMAKE_DL_LIBS})
	if(WIN32)
		target_sources(${target} PRIVATE src/windows/archive_native.c)
		target_link_libraries(${target} PRIVATE bcrypt)
	else()
		target_sources(${target} PRIVATE src/posix/archive_native.c)
		if(NOT APPLE AND BUILD_SHARED_LIBS)
			target_link_options(${target} PRIVATE
				"-Wl,--exclude-libs,ALL")
		endif()
	endif()
endfunction()
