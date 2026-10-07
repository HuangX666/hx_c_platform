cmake_minimum_required(VERSION 3.16)
project(hx_dependency_sources LANGUAGES NONE)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/HxDependencySources.cmake")
set(HX_OS_DEPENDENCIES "cjson;libyaml;minixml;inih;tomlc;iconv;kcp;uthash"
	CACHE STRING "Dependencies to prepare through the common provider")

foreach(name IN LISTS HX_OS_DEPENDENCIES)
	hx_dependency_source("${name}" source)
	message(STATUS "${name}: ${source}")
endforeach()
