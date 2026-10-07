if(NOT HX_OS_HAVE_7ZIP)
	return()
endif()

enable_language(CXX)
include("${CMAKE_CURRENT_LIST_DIR}/HxDependencySources.cmake")
hx_dependency_source(7zip hx_seven_source)

add_library(hx_archive_7zip SHARED
	src/7zip/Bridge.cpp src/7zip/Archive.cpp)
target_include_directories(hx_archive_7zip PRIVATE include src)
target_include_directories(hx_archive_7zip SYSTEM PRIVATE
	"${hx_seven_source}/CPP" "${hx_seven_source}")
set_target_properties(hx_archive_7zip PROPERTIES
	CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO
	CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES
	RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin"
	LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
hx_apply_warnings(hx_archive_7zip)
if(MSVC)
	target_link_libraries(hx_archive_7zip PRIVATE oleaut32 ole32 uuid)
endif()
if(HX_OS_SANITIZER AND NOT MSVC)
	target_compile_options(hx_archive_7zip PRIVATE
		-fsanitize=${HX_OS_SANITIZER} -fno-omit-frame-pointer)
	target_link_options(hx_archive_7zip PRIVATE
		-fsanitize=${HX_OS_SANITIZER})
endif()

install(TARGETS hx_archive_7zip
	RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
	LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(FILES "${hx_seven_source}/DOC/License.txt"
	DESTINATION "${CMAKE_INSTALL_DATADIR}/hx-dependencies"
	RENAME 7-Zip-LICENSE.txt)

install(FILES "${hx_seven_source}/DOC/copying.txt"
	DESTINATION "${CMAKE_INSTALL_DATADIR}/hx-dependencies"
	RENAME 7-Zip-LGPL.txt)
install(FILES "${hx_seven_source}/DOC/unRarLicense.txt"
	DESTINATION "${CMAKE_INSTALL_DATADIR}/hx-dependencies"
	RENAME 7-Zip-unRAR.txt)
