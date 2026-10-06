cmake_minimum_required(VERSION 3.20)
project(hx_dependency_check LANGUAGES C)

enable_testing()

find_package(LibArchive REQUIRED)
find_package(OpenSSL CONFIG REQUIRED)
find_package(ZLIB CONFIG REQUIRED COMPONENTS static)
find_package(liblzma CONFIG REQUIRED)
find_package(zstd CONFIG REQUIRED)
find_package(Threads REQUIRED)

add_executable(hx_dependency_check archive_check.c)
set_target_properties(hx_dependency_check PROPERTIES
	C_STANDARD 99 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
target_compile_definitions(hx_dependency_check PRIVATE LIBARCHIVE_STATIC)
target_link_libraries(hx_dependency_check PRIVATE
	LibArchive::LibArchive ZLIB::ZLIBSTATIC liblzma::liblzma
	zstd::libzstd_static OpenSSL::Crypto Threads::Threads
	${CMAKE_DL_LIBS})

if(WIN32)
	target_link_libraries(hx_dependency_check PRIVATE bcrypt)
endif()

if(MSVC)
	target_compile_options(hx_dependency_check PRIVATE /utf-8 /W4)
endif()

add_test(NAME archive_formats COMMAND hx_dependency_check)
set_tests_properties(archive_formats PROPERTIES TIMEOUT 30)
