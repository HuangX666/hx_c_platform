# Compile upstream C sources unchanged; configured files stay in the build.
set(hx_iconv_generated "${CMAKE_CURRENT_BINARY_DIR}/iconv")
file(MAKE_DIRECTORY "${hx_iconv_generated}")
set(DLL_VARIABLE "")
set(EILSEQ 42)
set(ICONV_CONST "const")
set(USE_MBSTATE_T 0)
set(BROKEN_WCHAR_H 0)

configure_file("${HX_OS_ICONV_SOURCE_DIR}/include/iconv.h.in"
	"${hx_iconv_generated}/iconv.h" @ONLY)
configure_file(
	"${HX_OS_ICONV_SOURCE_DIR}/libcharset/include/localcharset.h.in"
	"${hx_iconv_generated}/localcharset.h" @ONLY)
include(TestBigEndian)

test_big_endian(hx_iconv_big_endian)

if(hx_iconv_big_endian)
	set(hx_iconv_little 0)
else()
	set(hx_iconv_little 1)
endif()

configure_file("${CMAKE_CURRENT_LIST_DIR}/iconv-config.h.in"
	"${hx_iconv_generated}/config.h" @ONLY)
set(hx_iconv_aliases
	libiconv=hx_os_vendor_iconv
	libiconv_open=hx_os_vendor_iconv_open
	libiconv_close=hx_os_vendor_iconv_close
	libiconv_open_into=hx_os_vendor_iconv_open_into
	libiconvctl=hx_os_vendor_iconvctl
	libiconvlist=hx_os_vendor_iconvlist
	libiconv_canonicalize=hx_os_vendor_iconv_canonicalize
	libiconv_set_relocation_prefix=hx_os_vendor_iconv_relocation
	_libiconv_version=hx_os_vendor_iconv_version
	locale_charset=hx_os_vendor_locale_charset)

add_library(hx_iconv_impl OBJECT
	"${HX_OS_ICONV_SOURCE_DIR}/lib/iconv.c"
	"${HX_OS_ICONV_SOURCE_DIR}/lib/compat.c"
	"${HX_OS_ICONV_SOURCE_DIR}/libcharset/lib/localcharset.c")
target_include_directories(hx_iconv_impl PRIVATE "${hx_iconv_generated}")
target_compile_definitions(hx_iconv_impl PRIVATE ${hx_iconv_aliases})
set_target_properties(hx_iconv_impl PROPERTIES
	C_STANDARD 99
	POSITION_INDEPENDENT_CODE ON
	C_VISIBILITY_PRESET hidden)

if(MSVC)
	target_compile_options(hx_iconv_impl PRIVATE /utf-8)
	target_compile_definitions(hx_iconv_impl PRIVATE
		_CRT_SECURE_NO_WARNINGS)
endif()

install(FILES "${HX_OS_ICONV_SOURCE_DIR}/COPYING.LIB"
	DESTINATION "${CMAKE_INSTALL_DATADIR}/hx_c_platform/licenses"
	RENAME libiconv-COPYING.LIB)
