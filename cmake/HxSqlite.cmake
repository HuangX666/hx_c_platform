include_guard(GLOBAL)

set(HX_OS_HAVE_PERSISTENT_QUEUE 0)
if(HX_OS_ENABLE_PERSISTENT_QUEUE AND HX_OS_ENABLE_RANDOM
		AND HX_OS_ENABLE_THREAD_EX)
	include("${CMAKE_CURRENT_LIST_DIR}/HxDependencySources.cmake")
	hx_dependency_source(sqlite hx_sqlite_source)
	add_library(hx_c_platform_sqlite SHARED "${hx_sqlite_source}/sqlite3.c")
	add_library(hx_c_platform::sqlite ALIAS hx_c_platform_sqlite)
	set_target_properties(hx_c_platform_sqlite PROPERTIES
		EXPORT_NAME sqlite OUTPUT_NAME hx_sqlite3
		C_STANDARD 99 POSITION_INDEPENDENT_CODE ON
		RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin"
		FOLDER "dependencies")
	target_include_directories(hx_c_platform_sqlite PUBLIC
		$<BUILD_INTERFACE:${hx_sqlite_source}>
		$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/hx-deps/sqlite>)
	target_compile_definitions(hx_c_platform_sqlite PRIVATE
		SQLITE_THREADSAFE=1 SQLITE_DQS=0)
	if(WIN32)
		target_compile_definitions(hx_c_platform_sqlite PRIVATE
			"SQLITE_API=__declspec(dllexport)")
		target_compile_definitions(hx_c_platform_sqlite INTERFACE
			"SQLITE_API=__declspec(dllimport)")
	else()
		target_link_libraries(hx_c_platform_sqlite PRIVATE
			Threads::Threads ${CMAKE_DL_LIBS} m)
	endif()
	install(TARGETS hx_c_platform_sqlite EXPORT hx_c_platformTargets
		RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
		LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
		ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
	install(FILES "${hx_sqlite_source}/sqlite3.h"
		"${hx_sqlite_source}/sqlite3ext.h"
		DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/hx-deps/sqlite")
	set(HX_OS_HAVE_PERSISTENT_QUEUE 1)
endif()
