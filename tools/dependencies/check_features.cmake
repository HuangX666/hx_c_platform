if(NOT DEFINED HX_ARCHIVE_CONFIG OR NOT EXISTS "${HX_ARCHIVE_CONFIG}")
	message(FATAL_ERROR "The configured libarchive header is missing")
endif()

file(READ "${HX_ARCHIVE_CONFIG}" hx_config)
foreach(feature HAVE_LIBZ HAVE_LIBLZMA HAVE_LIBZSTD HAVE_LIBCRYPTO)
	if(NOT hx_config MATCHES "#define ${feature} 1([\r\n]|$)")
		message(FATAL_ERROR
			"The selected archive SDK is missing ${feature}. "
			"Check target dependency discovery; codec fallback "
			"is not accepted.")
	endif()
endforeach()

message(STATUS "Archive SDK: zlib, XZ, Zstd and OpenSSL enabled")
