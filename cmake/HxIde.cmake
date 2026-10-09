if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
	set_property(GLOBAL PROPERTY USE_FOLDERS ON)
	set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER "CMake")
endif()

set_target_properties(hx_c_platform PROPERTIES FOLDER "libraries")
get_property(hx_ide_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(target IN LISTS hx_ide_targets)
	if(target MATCHES "^hx_(parser_|kcp_vendor$|iconv_impl$)")
		set_target_properties(${target} PROPERTIES FOLDER "deps")
	endif()
endforeach()

# Only header discovery is automatic; compiled sources stay explicit.
file(GLOB_RECURSE hx_ide_public_headers CONFIGURE_DEPENDS
	"${PROJECT_SOURCE_DIR}/include/*.h"
	"${PROJECT_SOURCE_DIR}/include/*.hpp")
file(GLOB hx_ide_private_headers CONFIGURE_DEPENDS
	"${PROJECT_SOURCE_DIR}/src/*.h"
	"${PROJECT_SOURCE_DIR}/${hx_private_include}/*.h")
set(hx_ide_config "${PROJECT_BINARY_DIR}/private/hx_os_config.h")
target_sources(hx_c_platform PRIVATE
	${hx_ide_public_headers} ${hx_ide_private_headers} "${hx_ide_config}")
source_group(TREE "${PROJECT_SOURCE_DIR}/include"
	PREFIX "Header Files\\Public" FILES ${hx_ide_public_headers})
source_group(TREE "${PROJECT_SOURCE_DIR}/src"
	PREFIX "Header Files\\Private" FILES ${hx_ide_private_headers})
source_group("Generated Files" FILES "${hx_ide_config}")

if(TARGET hx_archive_7zip)
	set_target_properties(hx_archive_7zip PROPERTIES FOLDER "libraries")
	target_sources(hx_archive_7zip PRIVATE
		src/7zip/Bridge.hpp src/archive_bridge.h)
	source_group("Header Files" FILES src/7zip/Bridge.hpp)
endif()

foreach(target IN ITEMS hx_c_platform hx_archive_7zip hx_kcp_vendor)
	if(TARGET ${target})
		get_target_property(hx_ide_sources ${target} SOURCES)
		list(FILTER hx_ide_sources INCLUDE
			REGEX "^src/.*\\.(c|cpp|m|S)$")
		source_group(TREE "${PROJECT_SOURCE_DIR}/src"
			PREFIX "Source Files" FILES ${hx_ide_sources})
	endif()
endforeach()
