function(hx_find_archive)
	if(HX_OS_DEPENDENCY_PREFIX)
		list(PREPEND CMAKE_PREFIX_PATH "${HX_OS_DEPENDENCY_PREFIX}")
	endif()

	find_package(LibArchive 3.8 REQUIRED)
	if(NOT TARGET LibArchive::LibArchive)
		add_library(LibArchive::LibArchive UNKNOWN IMPORTED)
		set_target_properties(LibArchive::LibArchive PROPERTIES
			IMPORTED_LOCATION "${LibArchive_LIBRARY}"
			INTERFACE_INCLUDE_DIRECTORIES
				"${LibArchive_INCLUDE_DIRS}")
	endif()
	find_package(ZLIB CONFIG REQUIRED COMPONENTS static)
	find_package(liblzma CONFIG REQUIRED)
	find_package(zstd CONFIG REQUIRED)
endfunction()

if(HX_OS_HAVE_ARCHIVE)
	hx_find_archive()
	string(APPEND HX_OS_PC_REQUIRES_PRIVATE
		", libarchive >= 3.8, zlib, liblzma, libzstd")
endif()

function(hx_link_archive target)
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
