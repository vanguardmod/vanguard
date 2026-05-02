# Modifications for VanguardMod:
# SPDX-FileCopyrightText: 2026 wahke <info@wahke.lu> (https://wahke.lu)
# SPDX-FileCopyrightText: 2026 VanguardMod Project Contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Modified for VanguardMod (https://vanguardmod.com).
# Modifications licensed under GPL-3.0-or-later (consistent with original).
#
# Vanguard-specific additions: BONE_HITTESTS=1 propagation across
# qagame compile units, multi-arch DLL bundling into vanguard_v*.pk3.

#-----------------------------------------------------------------
# Build mod pack
#-----------------------------------------------------------------

# find libm where it exists and link game modules against it
include(CheckLibraryExists)
check_library_exists(m pow "" LIBM)
if(LIBM)
    target_link_libraries(cgame_libraries INTERFACE m)
    target_link_libraries(ui_libraries INTERFACE m)
    target_link_libraries(qagame_libraries INTERFACE m)
    target_link_libraries(tvgame_libraries INTERFACE m)
endif()

# Keep stricter undefined-symbol checks scoped to the ET:L VM modules so
# bundled third-party libraries continue to use their own upstream defaults.
function(etl_enforce_linux_mod_no_undefined target_name)
	if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_C_COMPILER_ID STREQUAL "GNU")
		target_link_options(${target_name} PRIVATE LINKER:--no-undefined)
	endif()
endfunction()

#
# cgame
#
if(BUILD_CLIENT_MOD)
	add_library(cgame MODULE ${CGAME_SRC})
	target_link_libraries(cgame cgame_libraries mod_libraries)
	etl_enforce_linux_mod_no_undefined(cgame)

	set_target_properties(cgame
		PROPERTIES
		PREFIX ""
		C_STANDARD 90
		OUTPUT_NAME "cgame${LIB_SUFFIX}${ARCH}"
		LIBRARY_OUTPUT_DIRECTORY "${MODNAME}"
		LIBRARY_OUTPUT_DIRECTORY_DEBUG "${MODNAME}"
		LIBRARY_OUTPUT_DIRECTORY_RELEASE "${MODNAME}"
		LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO "${MODNAME}"
	)
	target_compile_definitions(cgame PRIVATE CGAMEDLL=1 MODLIB=1)
endif()

#
# ui
#
if(BUILD_CLIENT_MOD)
	add_library(ui MODULE ${UI_SRC})
	target_link_libraries(ui ui_libraries mod_libraries)
	etl_enforce_linux_mod_no_undefined(ui)

	set_target_properties(ui
		PROPERTIES
		PREFIX ""
		C_STANDARD 90
		OUTPUT_NAME "ui${LIB_SUFFIX}${ARCH}"
		LIBRARY_OUTPUT_DIRECTORY "${MODNAME}"
		LIBRARY_OUTPUT_DIRECTORY_DEBUG "${MODNAME}"
		LIBRARY_OUTPUT_DIRECTORY_RELEASE "${MODNAME}"
		LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO "${MODNAME}"
	)
	target_compile_definitions(ui PRIVATE UIDLL=1 MODLIB=1)
endif()

#
# qagame
#
if(BUILD_SERVER_MOD)
	add_library(qagame MODULE ${QAGAME_SRC})
	target_link_libraries(qagame qagame_libraries mod_libraries)
	etl_enforce_linux_mod_no_undefined(qagame)

	if(FEATURE_LUASQL AND FEATURE_DBMS)
		target_compile_definitions(qagame PRIVATE FEATURE_DBMS FEATURE_LUASQL)

		if(BUNDLED_SQLITE3)
			target_link_libraries(qagame bundled_sqlite3)
		else() # BUNDLED_SQLITE3
			find_package(SQLite3 REQUIRED)
			target_link_libraries(qagame ${SQLITE3_LIBRARY})
			target_include_directories(qagame PUBLIC ${SQLITE3_INCLUDE_DIR})
		endif()

		FILE(GLOB LUASQL_SRC
			"vendor/luasql/luasql.c"
			"vendor/luasql/luasql.h"
			"vendor/luasql/ls_sqlite3.c"
		)
		set(QAGAME_SRC ${QAGAME_SRC} ${LUASQL_SRC})
	endif()

	if(FEATURE_SERVERMDX)
		target_compile_definitions(qagame PRIVATE FEATURE_SERVERMDX)
	endif()

	# VANGUARDMOD: propagate BONE_HITTESTS to all qagame compile units
	# (Phase 6.0). The header-level #define in src/game/g_mdx.h is only
	# visible to files that include that header; shared sources like
	# src/qcommon/q_math.c (which contains the BONE_HITTESTS-gated
	# quat_from_axis helper) need the define at the target level.
	# See docs/PHASE_6_PLAN.md and docs/HITS_FORMAT.md.
	target_compile_definitions(qagame PRIVATE BONE_HITTESTS=1)
	# END VANGUARDMOD

	# VANGUARDMOD v0.6.0: WolfGuard plugin layer.
	# wg_interface.h + wg_banner.{c,h} live in src/game/wolfguard/ and
	# are NOT picked up by ETLSources.cmake's QAGAME_SRC glob (which is
	# `src/game/*.c`, non-recursive). Add them via target_sources so the
	# qagame target carries the banner unconditionally and either the
	# stub or the real provider depending on FEATURE_WOLFGUARD. The
	# orphan top-level wolfguard/ + cmake/WolfGuard.cmake from earlier
	# scaffolding are deliberately untouched in this PR — separate
	# cleanup.
	target_sources(qagame PRIVATE
		"${CMAKE_SOURCE_DIR}/src/game/wolfguard/wg_interface.h"
		"${CMAKE_SOURCE_DIR}/src/game/wolfguard/wg_banner.h"
		"${CMAKE_SOURCE_DIR}/src/game/wolfguard/wg_banner.c"
	)

	if(FEATURE_WOLFGUARD)
		# Protected build path. Refuses to configure unless the private
		# WolfGuard sources are present so a misconfigured CI doesn't
		# silently produce a community binary while claiming protected.
		if(NOT EXISTS "${CMAKE_SOURCE_DIR}/src/game/wolfguard/private/CMakeLists.txt")
			message(FATAL_ERROR
				"FEATURE_WOLFGUARD=ON requires the private WolfGuard sources at\n"
				"src/game/wolfguard/private/. Clone vanguardmod/wolfguard into that\n"
				"path, or build with -DFEATURE_WOLFGUARD=OFF for a community build.")
		endif()
		add_subdirectory("${CMAKE_SOURCE_DIR}/src/game/wolfguard/private")
		target_compile_definitions(qagame PRIVATE WG_ENABLED=1)
	else()
		# Community build path. wg_stub.c provides the no-op dispatch
		# table + WG_IsAvailable=qfalse + WG_Version="n/a" so the
		# banner emits the [ NOT INCLUDED ] variant.
		target_sources(qagame PRIVATE
			"${CMAKE_SOURCE_DIR}/src/game/wolfguard/wg_stub.c"
		)
	endif()
	# END VANGUARDMOD v0.6.0

	set_target_properties(qagame
		PROPERTIES
		PREFIX ""
		C_STANDARD 90
		OUTPUT_NAME "qagame${LIB_SUFFIX}${ARCH}"
		LIBRARY_OUTPUT_DIRECTORY "${MODNAME}"
		LIBRARY_OUTPUT_DIRECTORY_DEBUG "${MODNAME}"
		LIBRARY_OUTPUT_DIRECTORY_RELEASE "${MODNAME}"
		LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO "${MODNAME}"
		RUNTIME_OUTPUT_DIRECTORY "${MODNAME}"
		RUNTIME_OUTPUT_DIRECTORY_DEBUG "${MODNAME}"
		RUNTIME_OUTPUT_DIRECTORY_RELEASE "${MODNAME}"
	)
	target_compile_definitions(qagame PRIVATE GAMEDLL=1 MODLIB=1)
endif()

#
# tvgame
#
if(BUILD_SERVER_MOD)
	add_library(tvgame MODULE ${TVGAME_SRC})
	target_link_libraries(tvgame tvgame_libraries mod_libraries)
	etl_enforce_linux_mod_no_undefined(tvgame)

	if(FEATURE_LUASQL AND FEATURE_DBMS)
		target_compile_definitions(tvgame PRIVATE FEATURE_DBMS FEATURE_LUASQL)

		if(BUNDLED_SQLITE3)
			target_link_libraries(tvgame bundled_sqlite3)
		else() # BUNDLED_SQLITE3
			find_package(SQLite3 REQUIRED)
			target_link_libraries(tvgame ${SQLITE3_LIBRARY})
			target_include_directories(tvgame PUBLIC ${SQLITE3_INCLUDE_DIR})
		endif()

		FILE(GLOB LUASQL_SRC
			"vendor/luasql/luasql.c"
			"vendor/luasql/luasql.h"
			"vendor/luasql/ls_sqlite3.c"
		)
		set(TVGAME_SRC ${TVGAME_SRC} ${LUASQL_SRC})
	endif()

	set_target_properties(tvgame
		PROPERTIES
		PREFIX ""
		C_STANDARD 90
		OUTPUT_NAME "tvgame${LIB_SUFFIX}${ARCH}"
		LIBRARY_OUTPUT_DIRECTORY "${MODNAME}"
		LIBRARY_OUTPUT_DIRECTORY_DEBUG "${MODNAME}"
		LIBRARY_OUTPUT_DIRECTORY_RELEASE "${MODNAME}"
		LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO "${MODNAME}"
		RUNTIME_OUTPUT_DIRECTORY "${MODNAME}"
		RUNTIME_OUTPUT_DIRECTORY_DEBUG "${MODNAME}"
		RUNTIME_OUTPUT_DIRECTORY_RELEASE "${MODNAME}"
	)
	target_compile_definitions(tvgame PRIVATE GAMEDLL=1 MODLIB=1)
endif()

# Build both architectures on older xcode versions
if(APPLE)

	if (DEFINED CMAKE_OSX_ARCHITECTURES AND NOT CMAKE_OSX_ARCHITECTURES STREQUAL "")
		message(STATUS "Using the user provided osx architectures: ${CMAKE_OSX_ARCHITECTURES}")
		set(OSX_MOD_ARCH "${CMAKE_OSX_ARCHITECTURES}")
	else()

		execute_process(
			COMMAND uname -m
			OUTPUT_VARIABLE ETL_OSX_NATIVE_ARCHITECTURE
			OUTPUT_STRIP_TRAILING_WHITESPACE
		)

		check_c_compiler_flag("-arch i386" i386Supported)
		check_c_compiler_flag("-arch x86_64" x86_64Supported)
		check_c_compiler_flag("-arch arm64" arm64Supported)

		# Mojave was the last version to support 32 bit binaries and building.
		# Newer SDK's just fail compilation
		# TODO: maybe remove this whole thing after the next release.
		if(XCODE_SDK_VERSION LESS "10.14" AND CMAKE_OSX_DEPLOYMENT_TARGET LESS "10.14" AND i386Supported AND x86_64Supported)
			# Force universal mod on osx up to Mojave
			message(STATUS "Enabling MacOS x86 and x86_64 builds on mods")
			set(OSX_MOD_ARCH "i386;x86_64")
		elseif(XCODE_SDK_VERSION GREATER_EQUAL "11.00" AND x86_64Supported AND arm64Supported)
			message(STATUS "Enabling MacOS x86_64 and Arm builds on mods")
			set(OSX_MOD_ARCH "x86_64;arm64")
		else()
			# Using only the native arch
			message(STATUS "Only doing MacOS ${ETL_OSX_NATIVE_ARCHITECTURE} bit build")
			set(OSX_MOD_ARCH "${ETL_OSX_NATIVE_ARCHITECTURE}")
		endif()

	endif()

	if(BUILD_CLIENT_MOD)
		set_target_properties(cgame PROPERTIES OSX_ARCHITECTURES "${OSX_MOD_ARCH}" )
		set_target_properties(ui PROPERTIES OSX_ARCHITECTURES "${OSX_MOD_ARCH}" )
	endif()
elseif(ANDROID)
	if(BUILD_CLIENT_MOD)
		set_target_properties(cgame PROPERTIES PREFIX "lib")
		set_target_properties(ui PROPERTIES PREFIX "lib")
	endif()
endif()

# install bins of cgame, ui and qgame
if(BUILD_SERVER_MOD)
	install(TARGETS qagame tvgame
		RUNTIME DESTINATION "${INSTALL_DEFAULT_MODDIR}/${MODNAME}"
		LIBRARY DESTINATION "${INSTALL_DEFAULT_MODDIR}/${MODNAME}"
		ARCHIVE DESTINATION "${INSTALL_DEFAULT_MODDIR}/${MODNAME}"
	)
endif()

if(NOT BUILD_MOD_PK3 AND BUILD_CLIENT_MOD)
	install(TARGETS cgame ui
		RUNTIME DESTINATION "${INSTALL_DEFAULT_MODDIR}/${MODNAME}"
		LIBRARY DESTINATION "${INSTALL_DEFAULT_MODDIR}/${MODNAME}"
		ARCHIVE DESTINATION "${INSTALL_DEFAULT_MODDIR}/${MODNAME}"
	)
endif()

#
# mod pk3
#
if(BUILD_MOD_PK3)
	# etmain
	file(GLOB_RECURSE	ETMAIN_FILES			CONFIGURE_DEPENDS	"${CMAKE_CURRENT_SOURCE_DIR}/etmain/*")
	file(GLOB			ETMAIN_FILES_SHALLOW	CONFIGURE_DEPENDS	"${CMAKE_CURRENT_SOURCE_DIR}/etmain/*")

	# Vanguard: filter pak0/1/2.pk3 and mp_bin.pk3 — these are genuine Activision
	# Wolfenstein: Enemy Territory paydata that some devs keep in etmain/ for
	# local play. Upstream's source tree never contains them, so upstream's
	# unfiltered GLOB is safe there; for us it would silently bundle 200+ MB of
	# copyrighted content into a redistributable .pk3. Re-add this filter when
	# re-syncing from etlegacy.git master.
	list(FILTER ETMAIN_FILES         EXCLUDE REGEX "/(pak[0-2]|mp_bin)\\.pk3$")
	list(FILTER ETMAIN_FILES_SHALLOW EXCLUDE REGEX "/(pak[0-2]|mp_bin)\\.pk3$")

	set(ETMAIN_FILE_LIST_REL "")
	foreach(FILE ${ETMAIN_FILES_SHALLOW})
		file(RELATIVE_PATH REL "${CMAKE_CURRENT_SOURCE_DIR}/etmain" ${FILE})
		list(APPEND ETMAIN_FILES_SHALLOW_REL ${REL})
	endforeach()
	foreach(FILE ${ETMAIN_FILES})
		file(RELATIVE_PATH REL "${CMAKE_CURRENT_SOURCE_DIR}/etmain" ${FILE})
		list(APPEND ETMAIN_FILE_LIST_REL ${REL})
	endforeach()
	list(SORT ETMAIN_FILE_LIST_REL)

	# Vanguard: collect cross-built Windows DLLs from build-windows{,-32}/${MODNAME}
	# so the resulting .pk3 carries every architecture (Linux .so via TARGET_FILE_NAME
	# below, Windows x64 + x86 .dll via this glob). Empty list = degraded build that
	# only ships Linux modules, which is a useful fallback for "linux-only" CI runs.
	# Re-add this block when re-syncing ETLBuildMod.cmake from upstream.
	set(VANGUARD_WIN_DLL_FILES "")
	set(VANGUARD_WIN_DLL_NAMES "")
	foreach(_vg_win_dir
		"${CMAKE_CURRENT_SOURCE_DIR}/build-windows/${MODNAME}"
		"${CMAKE_CURRENT_SOURCE_DIR}/build-windows-32/${MODNAME}")
		if(EXISTS "${_vg_win_dir}")
			file(GLOB _vg_dlls CONFIGURE_DEPENDS "${_vg_win_dir}/*.dll")
			foreach(_vg_dll ${_vg_dlls})
				list(APPEND VANGUARD_WIN_DLL_FILES "${_vg_dll}")
				get_filename_component(_vg_name "${_vg_dll}" NAME)
				list(APPEND VANGUARD_WIN_DLL_NAMES "${_vg_name}")
			endforeach()
		endif()
	endforeach()
	if(VANGUARD_WIN_DLL_FILES)
		message(STATUS "Vanguard: bundling ${VANGUARD_WIN_DLL_NAMES} into mod pk3")
	else()
		message(STATUS "Vanguard: no Windows cross-build DLLs found, mod pk3 will be Linux-only")
	endif()

	# Vanguard: server-side modules (qagame/tvgame) are also packed so that
	# listen-server hosts and tv spectator setups can run self-contained from
	# a single mod pk3. Upstream packs only cgame+ui (client-side); add these
	# back to the list when re-syncing.
	set(VANGUARD_SERVER_MOD_TARGETS "")
	set(VANGUARD_SERVER_MOD_FILES "")
	if(BUILD_SERVER_MOD)
		list(APPEND VANGUARD_SERVER_MOD_TARGETS qagame tvgame)
		list(APPEND VANGUARD_SERVER_MOD_FILES
			"$<TARGET_FILE_NAME:qagame>"
			"$<TARGET_FILE_NAME:tvgame>"
		)
	endif()

	# Record the full file list so removals can be detected without reacting to edits.
	set(ETMAIN_FILE_LIST_CONTENT "")
	foreach(REL ${ETMAIN_FILE_LIST_REL})
		string(APPEND ETMAIN_FILE_LIST_CONTENT "${REL}\n")
	endforeach()
	set(ETMAIN_FILE_LIST_FILE "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/etl_etmain_file_list.txt")
	file(GENERATE OUTPUT "${ETMAIN_FILE_LIST_FILE}" CONTENT "${ETMAIN_FILE_LIST_CONTENT}")

	# Remove old legacy mod pk3 files from the build directory (useful for the development)
	file(GLOB OLD_LEGACY_PK3_FILES "${CMAKE_CURRENT_BINARY_DIR}/${MODNAME}/${MODNAME}_*.pk3")
	list(REMOVE_ITEM OLD_LEGACY_PK3_FILES "${CMAKE_CURRENT_BINARY_DIR}/${MODNAME}/${MODNAME}_${ETL_CMAKE_VERSION_SHORT}.pk3")
	add_custom_target(remove_old_pk3_files
		COMMAND ${CMAKE_COMMAND} -E remove -f "${OLD_LEGACY_PK3_FILES}"
		COMMAND_EXPAND_LISTS
	)

	# CMake's copy_directory won't remove stale files, so clear staged etmain only when it changes.
	set(ETMAIN_PREV_LIST_FILE "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/etl_etmain_prev_list.txt")
	set(ETMAIN_STAGE_DIR "${CMAKE_CURRENT_BINARY_DIR}/${MODNAME}")
	set(ETMAIN_CURRENT_LIST_FILE "${ETMAIN_FILE_LIST_FILE}")
	set(ETMAIN_CLEAR_SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/etl_clear_etmain.cmake")
	configure_file(
		"${CMAKE_CURRENT_SOURCE_DIR}/cmake/ETLBuildModFileDeletionDetector.cmake.in"
		"${ETMAIN_CLEAR_SCRIPT}"
		@ONLY
	)

	# Only refresh staged etmain when etmain files (or their list) change.
	set(ETMAIN_STAGE_STAMP "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/etl_etmain_stage.stamp")
	add_custom_command(
		OUTPUT "${ETMAIN_STAGE_STAMP}"
		COMMAND ${CMAKE_COMMAND} -P "${ETMAIN_CLEAR_SCRIPT}"
		COMMAND ${CMAKE_COMMAND} -E touch "${ETMAIN_STAGE_STAMP}"
		DEPENDS "${ETMAIN_FILE_LIST_FILE}"
		VERBATIM
	)

	# Vanguard: stage cross-built Windows DLLs into the working dir before tar.
	# Held as an injected list of COMMAND clauses so the custom_command stays
	# linear and re-syncable. Empty when no DLLs were found at configure time.
	set(VANGUARD_WIN_DLL_STAGE_CMD "")
	if(VANGUARD_WIN_DLL_FILES)
		list(APPEND VANGUARD_WIN_DLL_STAGE_CMD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different
				${VANGUARD_WIN_DLL_FILES}
				"${CMAKE_CURRENT_BINARY_DIR}/${MODNAME}/"
		)
	endif()

	add_custom_command(
		OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${MODNAME}/${MODNAME}_${ETL_CMAKE_VERSION_SHORT}.pk3
		COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/etmain ${CMAKE_CURRENT_BINARY_DIR}/${MODNAME}
		# Version header is generated in the build tree and staged into ui/ for menu includes.
		COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_BINARY_DIR}/etmain/ui/version_generated.h ${CMAKE_CURRENT_BINARY_DIR}/${MODNAME}/ui/version_generated.h
		# Vanguard: stage misc/description.txt to the mod root so FS_GetModList
		# (qcommon/files.c:3413) can read it via FS_SV_FOpenFileRead and surface
		# it in the engine's mod-selection menu (UI feeder, ui/ui_main.c:8077).
		# That code reads a loose file off disk and never opens pk3s, so the
		# brand string has to live next to the .pk3, not inside it. v0.5.2.3.
		COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/misc/description.txt ${CMAKE_CURRENT_BINARY_DIR}/${MODNAME}/description.txt
		# Vanguard: stage Windows DLLs (no-op if VANGUARD_WIN_DLL_FILES is empty).
		${VANGUARD_WIN_DLL_STAGE_CMD}
		# Vanguard: tar list extended with qagame/tvgame and Windows DLL basenames.
		COMMAND ${CMAKE_COMMAND} -E tar c ${CMAKE_CURRENT_BINARY_DIR}/${MODNAME}/${MODNAME}_${ETL_CMAKE_VERSION_SHORT}.pk3 --format=zip $<TARGET_FILE_NAME:cgame> $<TARGET_FILE_NAME:ui> ${VANGUARD_SERVER_MOD_FILES} ${VANGUARD_WIN_DLL_NAMES} ${ETMAIN_FILES_SHALLOW_REL} ui/version_generated.h
		DEPENDS cgame ui ${VANGUARD_SERVER_MOD_TARGETS} ${VANGUARD_WIN_DLL_FILES} ${ETMAIN_FILES} ${CMAKE_CURRENT_BINARY_DIR}/etmain/ui/version_generated.h ${CMAKE_CURRENT_SOURCE_DIR}/misc/description.txt remove_old_pk3_files "${ETMAIN_STAGE_STAMP}"
		WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/${MODNAME}/
		VERBATIM
	)

	add_custom_target(mod_pk3 ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${MODNAME}/${MODNAME}_${ETL_CMAKE_VERSION_SHORT}.pk3)

	install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${MODNAME}/${MODNAME}_${ETL_CMAKE_VERSION_SHORT}.pk3
		DESTINATION "${INSTALL_DEFAULT_MODDIR}/${MODNAME}"
	)
endif()
