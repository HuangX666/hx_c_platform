include_guard(GLOBAL)

function(hx_install_native_headers target)
	get_target_property(roots ${target} INTERFACE_INCLUDE_DIRECTORIES)
	foreach(header IN LISTS ARGN)
		set(found FALSE)
		foreach(root IN LISTS roots)
			if(IS_DIRECTORY "${root}/${header}")
				install(DIRECTORY "${root}/${header}"
					DESTINATION
						"${CMAKE_INSTALL_INCLUDEDIR}")
			elseif(EXISTS "${root}/${header}")
				install(FILES "${root}/${header}"
					DESTINATION
						"${CMAKE_INSTALL_INCLUDEDIR}")
			else()
				continue()
			endif()
			set(found TRUE)
			break()
		endforeach()
		if(NOT found)
			message(FATAL_ERROR
				"Selected ${target} SDK is missing ${header}")
		endif()
	endforeach()
endfunction()

set(HX_OS_DEPENDENCY_PROVIDER "SUBMODULE" CACHE STRING
	"Dependency source provider: SUBMODULE or FETCHCONTENT")
set_property(CACHE HX_OS_DEPENDENCY_PROVIDER PROPERTY STRINGS
	SUBMODULE FETCHCONTENT)
get_filename_component(hx_dependency_root "${CMAKE_CURRENT_LIST_DIR}/.."
	ABSOLUTE)
set(HX_OS_DEPENDENCY_CACHE_DIR "${hx_dependency_root}/deps/.fetchcontent"
	CACHE PATH "FetchContent source cache, separate from submodules")

function(hx_dependency_metadata name repository revision version)
	if(name STREQUAL "sqlite")
		string(CONCAT repo "https://www.sqlite.org/2026/"
			"sqlite-amalgamation-3540000.zip")
		string(CONCAT pin "68e913b0fe8ec6b4e5f391c594a28872"
			"884e80d6cd042a1e50c957be46b5aa4d")
		set(release "3.54.0")
	elseif(name STREQUAL "c-ares")
		set(repo "https://github.com/c-ares/c-ares.git")
		set(pin "c7a3138dcfe3bb0eaaf10c0c24c36dc66dc790ab")
		set(release "1.34.8")
	elseif(name STREQUAL "curl")
		set(repo "https://github.com/curl/curl.git")
		set(pin "01346829096c61b372692f6dc43ffa778c6caccd")
		set(release "8.22.0")
	elseif(name STREQUAL "utf8proc")
		set(repo "https://github.com/JuliaStrings/utf8proc.git")
		set(pin "79cdd5ab40c79afa559f689ffc13b76812dee1ac")
		set(release "2.12.0")
	elseif(name STREQUAL "openssl")
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
	elseif(name STREQUAL "7zip")
		set(repo "https://github.com/ip7z/7zip.git")
		set(pin "9128b80e3a471108678e2b3ec3c985861c8d7b0f")
		set(release "26.04")
	elseif(name STREQUAL "cjson")
		set(repo "https://github.com/DaveGamble/cJSON.git")
		set(pin "c859b25da02955fef659d658b8f324b5cde87be3")
		set(release "1.7.19")
	elseif(name STREQUAL "libyaml")
		set(repo "https://github.com/yaml/libyaml.git")
		set(pin "2c891fc7a770e8ba2fec34fc6b545c672beb37e6")
		set(release "0.2.5")
	elseif(name STREQUAL "minixml")
		set(repo "https://github.com/michaelrsweet/mxml.git")
		set(pin "874e249a0d3b506e883210b7ceece5316a8489d4")
		set(release "4.0.6")
	elseif(name STREQUAL "inih")
		set(repo "https://github.com/benhoyt/inih.git")
		set(pin "26254ee9de7681f8825433415443e7116ff24b98")
		set(release "r62")
	elseif(name STREQUAL "tomlc")
		set(repo "https://github.com/cktan/tomlc17.git")
		set(pin "e0e8868546b4611a86fcdb284819e60866a3480b")
		set(release "R260821")
	elseif(name STREQUAL "kcp")
		set(repo "https://github.com/skywind3000/kcp.git")
		set(pin "8004f7eba5d1bf33f0691eef5f887f2cd3140cb5")
		set(release "v1")
	elseif(name STREQUAL "uthash")
		set(repo "https://github.com/troydhanson/uthash.git")
		set(pin "a49bed0b4abb7dff16c73906dcdc8a9718d582d2")
		set(release "2.4.0")
	elseif(name STREQUAL "iconv")
		string(CONCAT repo "https://ftp.gnu.org/pub/gnu/libiconv/"
			"libiconv-1.18.tar.gz")
		string(CONCAT pin
			"3b08f5f4f9b4eb82f151a7040bfd6fe6c6"
			"fb922efe4b1659c66ea933276965e8")
		set(release "1.18")
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
	if(NOT provider MATCHES "^(SUBMODULE|FETCHCONTENT)$")
		message(FATAL_ERROR
			"HX_OS_DEPENDENCY_PROVIDER must be SUBMODULE "
			"or FETCHCONTENT")
	endif()

	# GNU's Git tree omits the generated codec tables in its release.
	# Keep the official release unchanged instead of requiring host gperf.
	if(name STREQUAL "iconv" OR name STREQUAL "sqlite")
		set(provider FETCHCONTENT)
	endif()

	if(provider STREQUAL "SUBMODULE")
		set(source "${hx_dependency_root}/deps/${name}")
		if(name STREQUAL "kcp" OR name STREQUAL "uthash")
			# Existing source snapshots remain unchanged.
			string(APPEND source "/upstream")
		endif()
		if(NOT EXISTS "${source}/.git")
			message(FATAL_ERROR "Initialize dependency submodules: "
				"git submodule update --init --recursive. "
				"Alternatively select "
				"HX_OS_DEPENDENCY_PROVIDER=FETCHCONTENT.")
		endif()
	elseif(provider STREQUAL "FETCHCONTENT")
		include(FetchContent)
		if(POLICY CMP0135)
			cmake_policy(SET CMP0135 NEW)
		endif()
		set(cache "${HX_OS_DEPENDENCY_CACHE_DIR}/${name}/${revision}")
		file(MAKE_DIRECTORY "${HX_OS_DEPENDENCY_CACHE_DIR}/${name}")
		file(LOCK "${cache}.lock" GUARD FUNCTION TIMEOUT 180)
		string(TOUPPER "${name}" upper)
		set(override "${FETCHCONTENT_SOURCE_DIR_HX_DEP_${upper}}")
		set(ready FALSE)
		if(override)
			set(source "${override}")
			set(ready TRUE)
		elseif(EXISTS "${cache}/.git")
			set(source "${cache}")
			set(ready TRUE)
		elseif(EXISTS "${cache}.ready")
			file(READ "${cache}.ready" cached_revision)
			string(STRIP "${cached_revision}" cached_revision)
			if(name STREQUAL "sqlite")
				set(marker "sqlite3.c")
			else()
				set(marker "lib/iconv.c")
			endif()
			if(NOT cached_revision STREQUAL revision OR
				NOT EXISTS "${cache}/${marker}")
				message(FATAL_ERROR
					"Incomplete cache: " "${cache}"
					". Select a fresh cache directory.")
			endif()
			set(source "${cache}")
			set(ready TRUE)
		endif()
		if(name STREQUAL "iconv" OR name STREQUAL "sqlite")
			FetchContent_Declare(hx_dep_${name}
				URL "${repository}"
				URL_HASH "SHA256=${revision}"
				DOWNLOAD_DIR
					"${HX_OS_DEPENDENCY_CACHE_DIR}/${name}"
				SOURCE_DIR "${cache}")
		else()
			FetchContent_Declare(hx_dep_${name}
				GIT_REPOSITORY "${repository}"
				GIT_TAG "${revision}"
				SOURCE_DIR "${cache}" GIT_PROGRESS FALSE)
		endif()
		FetchContent_GetProperties(hx_dep_${name})
		if(NOT ready AND NOT hx_dep_${name}_POPULATED)
			# Populate only; upstream projects build out of source.
			cmake_policy(PUSH)
			if(POLICY CMP0169)
				cmake_policy(SET CMP0169 OLD)
			endif()
			FetchContent_Populate(hx_dep_${name})
			cmake_policy(POP)
		endif()
		if(NOT ready)
			set(source "${hx_dep_${name}_SOURCE_DIR}")
			if(name STREQUAL "iconv" OR name STREQUAL "sqlite")
				file(WRITE "${cache}.ready" "${revision}\n")
			endif()
		endif()
	endif()

	if(name STREQUAL "iconv" OR name STREQUAL "sqlite")
		set(${output} "${source}" PARENT_SCOPE)
		return()
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
