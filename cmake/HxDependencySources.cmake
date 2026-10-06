include_guard(GLOBAL)

set(HX_OS_DEPENDENCY_PROVIDER "SUBMODULE" CACHE STRING
	"Dependency source provider: SUBMODULE or FETCHCONTENT")
set_property(CACHE HX_OS_DEPENDENCY_PROVIDER PROPERTY STRINGS
	SUBMODULE FETCHCONTENT)
get_filename_component(hx_dependency_root "${CMAKE_CURRENT_LIST_DIR}/.."
	ABSOLUTE)
set(HX_OS_DEPENDENCY_CACHE_DIR "${hx_dependency_root}/deps/.fetchcontent"
	CACHE PATH "FetchContent source cache, separate from submodules")

function(hx_dependency_metadata name repository revision version)
	if(name STREQUAL "openssl")
		set(repo "https://github.com/openssl/openssl.git")
		set(pin "45e844fa2a14ec92d146bd8f5778ac130b6625fb")
		set(release "3.5.9")
	elseif(name STREQUAL "libarchive")
		set(repo "https://github.com/libarchive/libarchive.git")
		set(pin "27cbc7827172698143e440801fc0ba39ccb4f1f5")
		set(release "3.8.9")
	elseif(name STREQUAL "zlib")
		set(repo "https://github.com/madler/zlib.git")
		set(pin "da607da739fa6047df13e66a2af6b8bec7c2a498")
		set(release "1.3.2")
	elseif(name STREQUAL "xz")
		set(repo "https://github.com/tukaani-project/xz.git")
		set(pin "d3e650e63c110e830fd5391e7f8b45df0b91d3da")
		set(release "5.8.4")
	elseif(name STREQUAL "zstd")
		set(repo "https://github.com/facebook/zstd.git")
		set(pin "f8745da6ff1ad1e7bab384bd1f9d742439278e99")
		set(release "1.5.7")
	else()
		message(FATAL_ERROR "Unknown hx dependency: ${name}")
	endif()

	set(${repository} "${repo}" PARENT_SCOPE)
	set(${revision} "${pin}" PARENT_SCOPE)
	set(${version} "${release}" PARENT_SCOPE)
endfunction()

function(hx_dependency_source name output)
	hx_dependency_metadata("${name}" repository revision version)
	string(TOUPPER "${HX_OS_DEPENDENCY_PROVIDER}" provider)

	if(provider STREQUAL "SUBMODULE")
		set(source "${hx_dependency_root}/deps/${name}")
		if(NOT EXISTS "${source}/.git")
			message(FATAL_ERROR "Initialize dependency submodules: "
				"git submodule update --init --recursive. "
				"Alternatively select "
				"HX_OS_DEPENDENCY_PROVIDER=FETCHCONTENT.")
		endif()
	elseif(provider STREQUAL "FETCHCONTENT")
		include(FetchContent)
		set(cache "${HX_OS_DEPENDENCY_CACHE_DIR}/${name}/${revision}")
		FetchContent_Declare(hx_dep_${name}
			GIT_REPOSITORY "${repository}"
			GIT_TAG "${revision}"
			SOURCE_DIR "${cache}"
			GIT_PROGRESS FALSE)
		FetchContent_GetProperties(hx_dep_${name})
		if(NOT hx_dep_${name}_POPULATED)
			# Populate only; upstream projects build out of source.
			cmake_policy(PUSH)
			if(POLICY CMP0169)
				cmake_policy(SET CMP0169 OLD)
			endif()
			FetchContent_Populate(hx_dep_${name})
			cmake_policy(POP)
		endif()
		set(source "${hx_dep_${name}_SOURCE_DIR}")
	else()
		message(FATAL_ERROR
			"HX_OS_DEPENDENCY_PROVIDER must be SUBMODULE "
			"or FETCHCONTENT")
	endif()

	find_package(Git REQUIRED)
	execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${source}"
		rev-parse HEAD
		RESULT_VARIABLE result OUTPUT_VARIABLE actual
		OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
	if(NOT result EQUAL 0 OR NOT actual STREQUAL revision)
		message(FATAL_ERROR "${name} must use commit ${revision}; "
			"found '${actual}' at ${source}")
	endif()

	set(${output} "${source}" PARENT_SCOPE)
endfunction()
