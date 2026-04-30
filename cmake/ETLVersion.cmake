# Modifications for VanguardMod:
# SPDX-FileCopyrightText: 2026 wahke <info@wahke.lu> (https://wahke.lu)
# SPDX-FileCopyrightText: 2026 VanguardMod Project Contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Modified for VanguardMod (https://vanguardmod.com).
# Modifications licensed under GPL-3.0-or-later (consistent with original).
#
# Vanguard-specific additions: CI_ETL_TAG / CI_ETL_DESCRIBE accepted as
# cmake cache variables (line ~100), ETL_CMAKE_VERSION_INT leading-
# zero stripping (octal-literal fix for any patch >= 8) added in v0.4.3,
# `git describe --tags` so lightweight tags drive auto-versioning, and
# VANGUARD_VERSION file fallback for non-git builds (both added during
# v0.4.4 prep).

#-----------------------------------------------------------------
# Version
#-----------------------------------------------------------------

# default values if they cannot be generated from git
set(ETLEGACY_VERSION_MAJOR "0")
set(ETLEGACY_VERSION_MINOR "0")
set(ETLEGACY_VERSION_PATCH "0")
set(ETLEGACY_VERSION_COMMIT "0")

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION.txt" version_file_data)

string(REGEX MATCH "VERSION_MAJOR ([0-9]*)" _ ${version_file_data})
set(ETLEGACY_VERSION_MAJOR ${CMAKE_MATCH_1})
string(REGEX MATCH "VERSION_MINOR ([0-9]*)" _ ${version_file_data})
set(ETLEGACY_VERSION_MINOR ${CMAKE_MATCH_1})
string(REGEX MATCH "VERSION_PATCH ([0-9]*)" _ ${version_file_data})
set(ETLEGACY_VERSION_PATCH ${CMAKE_MATCH_1})

set(ETLEGACY_VERSION "${ETLEGACY_VERSION_MAJOR}.${ETLEGACY_VERSION_MINOR}-dirty")
set(ETLEGACY_VERSIONPLAIN "${ETLEGACY_VERSION_MAJOR},${ETLEGACY_VERSION_MINOR},${ETLEGACY_VERSION_PATCH},${ETLEGACY_VERSION_COMMIT}")

message(STATUS "File version: ${ETLEGACY_VERSION_MAJOR}.${ETLEGACY_VERSION_MINOR}.${ETLEGACY_VERSION_PATCH}.${ETLEGACY_VERSION_COMMIT}")

function(PAD_STRING output str padchar length right_padded)
	string(LENGTH "${str}" _strlen)
	math(EXPR _strlen "${length} - ${_strlen}")

	if(_strlen GREATER 0)
		if(${CMAKE_VERSION} VERSION_LESS "3.14")
			unset(_pad)
			foreach(_i RANGE 1 ${_strlen}) # inclusive
				string(APPEND _pad ${padchar})
			endforeach()
		else()
			string(REPEAT ${padchar} ${_strlen} _pad)
		endif()

		if(${right_padded})
			string(APPEND str ${_pad})
		else()
			string(PREPEND str ${_pad})
		endif()
	endif()

	set(${output} "${str}" PARENT_SCOPE)
endfunction()

# Generates a version integer value in the format of major, minor(2 numbers), patch(2 numbers), commit(4 numbers)
# all numbers are left padded if needed.
function(VERSION_INT output major minor patch commit)
	PAD_STRING(out_minor ${minor} "0" "2" OFF)
	PAD_STRING(out_patch ${patch} "0" "2" OFF)
	PAD_STRING(out_commit ${commit} "0" "4" OFF)

	set(${output} "${major}${out_minor}${out_patch}${out_commit}" PARENT_SCOPE)
endfunction()

macro(HEXCHAR2DEC VAR VAL)
	if(${VAL} MATCHES "[0-9]")
		SET(${VAR} ${VAL})
	elseif(${VAL} MATCHES "[aA]")
		SET(${VAR} 10)
	elseif(${VAL} MATCHES "[bB]")
		SET(${VAR} 11)
	elseif(${VAL} MATCHES "[cC]")
		SET(${VAR} 12)
	elseif(${VAL} MATCHES "[dD]")
		SET(${VAR} 13)
	elseif(${VAL} MATCHES "[eE]")
		SET(${VAR} 14)
	elseif(${VAL} MATCHES "[fF]")
		SET(${VAR} 15)
	else()
		MESSAGE(FATAL_ERROR "Invalid format for hexadecimal character")
	endif()
endmacro(HEXCHAR2DEC)

macro(GENERATENUMBER VAR VAL)
	IF(${VAL} EQUAL 0)
		SET(${VAR} 0)
	ELSEIF(${VAL} MATCHES "^[0-9]+$") # if its just numbers we escape out and just use that
		SET(${VAR} ${VAL})
	ELSE()
		SET(CURINDEX 0)
		STRING(LENGTH "${VAL}" CURLENGTH)
		SET(${VAR} 0)
		WHILE(CURINDEX LESS  CURLENGTH)
			STRING(SUBSTRING "${VAL}" ${CURINDEX} 1 CHAR)
			HEXCHAR2DEC(CHAR ${CHAR})
			MATH(EXPR POWAH "(1<<((${CURLENGTH}-${CURINDEX}-1)*4))")
			MATH(EXPR CHAR "(${CHAR}*${POWAH})")
			MATH(EXPR ${VAR} "${${VAR}}+${CHAR}")
			MATH(EXPR CURINDEX "${CURINDEX}+1")
		ENDWHILE()
	ENDIF()
endmacro(GENERATENUMBER)


# VANGUARD: accept CI_ETL_DESCRIBE / CI_ETL_TAG as a cmake cache
# variable in addition to the upstream environment-variable form.
# Background: scripts/bootstrap.sh exports both as env before its
# three cmake calls, which works for an end-to-end bootstrap. But
# manually rebuilding one platform (e.g. `cmake -B build-windows
# -DCMAKE_TOOLCHAIN_FILE=...`) in a fresh shell produced a build
# whose binaries embedded "2.83-dirty" instead of the bumped
# version, because the env var wasn't set on that invocation and
# `git describe` falls through (we don't tag the repo). Reading
# the cache variable too lets `-DCI_ETL_TAG=v0.3.X` work as a
# first-class form, which docs/RELEASE_PROCESS.md now documents
# as the canonical pattern. Env-var form is retained for
# bootstrap.sh symmetry and CI scripts.
if(NOT CI_ETL_DESCRIBE AND DEFINED ENV{CI_ETL_DESCRIBE})
	set(CI_ETL_DESCRIBE "$ENV{CI_ETL_DESCRIBE}")
endif()
if(CI_ETL_DESCRIBE)
	set(GIT_DESCRIBE "${CI_ETL_DESCRIBE}")
else()
	# VANGUARD: --tags accepts lightweight tags. Upstream's flagless
	# `git describe` only walks annotated tags and silently falls
	# through to VERSION.txt (i.e. the imported ETLegacy 2.83.x
	# string) when only lightweight tags exist — exactly the
	# behaviour we want to avoid for the auto-versioning pipeline.
	execute_process(COMMAND git describe --tags --abbrev=7
		WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
		OUTPUT_STRIP_TRAILING_WHITESPACE
		OUTPUT_VARIABLE GIT_DESCRIBE
		ERROR_QUIET)
endif()

if(NOT CI_ETL_TAG AND DEFINED ENV{CI_ETL_TAG})
	set(CI_ETL_TAG "$ENV{CI_ETL_TAG}")
endif()
if(CI_ETL_TAG)
	set(GIT_DESCRIBE_TAG "${CI_ETL_TAG}")
else()
	# VANGUARD: --tags as above (lightweight-tag support).
	execute_process(COMMAND git describe --tags --abbrev=0
		WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
		OUTPUT_STRIP_TRAILING_WHITESPACE
		OUTPUT_VARIABLE GIT_DESCRIBE_TAG
		ERROR_QUIET)
endif()

# VANGUARD: VANGUARD_VERSION file fallback for non-git checkouts.
#
# When neither CI_ETL_TAG nor `git describe --tags` produces
# anything (typical for tarball / zip downloads of a release
# artifact, where there's no .git/ directory), read the project's
# own VANGUARD_VERSION file at the repo root. Keeps tarball
# rebuilds producing a Vanguard-named pk3 instead of falling all
# the way through to the imported ETLegacy 2.83.x identity.
#
# This file is small (one line, e.g. `v0.4.3`) and is the single
# source of truth for non-git builds. Tagged builds (the normal
# case) still take the git describe path above; the fallback only
# fires when both env-var override AND git describe come back
# empty.
if(NOT GIT_DESCRIBE AND NOT GIT_DESCRIBE_TAG)
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/VANGUARD_VERSION")
		file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VANGUARD_VERSION" VANGUARD_VERSION_CONTENT)
		string(STRIP "${VANGUARD_VERSION_CONTENT}" VANGUARD_VERSION_CONTENT)
		if(VANGUARD_VERSION_CONTENT)
			message(STATUS "VANGUARD: using VANGUARD_VERSION fallback: ${VANGUARD_VERSION_CONTENT}")
			set(GIT_DESCRIBE     "${VANGUARD_VERSION_CONTENT}")
			set(GIT_DESCRIBE_TAG "${VANGUARD_VERSION_CONTENT}")
		endif()
	endif()
endif()

if(GIT_DESCRIBE)
	set(ETL_CMAKE_VERSION ${GIT_DESCRIBE})
	set(ETL_CMAKE_VERSION_SHORT ${GIT_DESCRIBE_TAG})

	string(COMPARE EQUAL "${ETL_CMAKE_VERSION}" "${ETL_CMAKE_VERSION_SHORT}" VERSION_IS_CLEAN)

	if(NOT VERSION_IS_CLEAN)
		message(STATUS "Using a non release version build: '${ETL_CMAKE_VERSION}' != '${ETL_CMAKE_VERSION_SHORT}'")

		if("$ENV{CI}" STREQUAL "true")
			message(STATUS "Detected build running in CI, using full version string instead")
			set(ETL_CMAKE_VERSION_SHORT "${ETL_CMAKE_VERSION}")
		else()
			set(ETL_CMAKE_VERSION_SHORT "${GIT_DESCRIBE_TAG}_dirty")
		endif()
	endif()

	if("${GIT_DESCRIBE}" MATCHES "^v[0-9]+\\.[0-9]+.*")
		string(REGEX REPLACE "^v([0-9]+)\\..*" "\\1" VERSION_MAJOR "${GIT_DESCRIBE}")
		string(REGEX REPLACE "^v[0-9]+\\.([0-9]+).*" "\\1" VERSION_MINOR "${GIT_DESCRIBE}")

		if("${GIT_DESCRIBE}" MATCHES "^v[0-9]+\\.[0-9]+rc[0-9]+.*")
			string(REGEX REPLACE "^v[0-9]+\\.[0-9]+rc([0-9]+).*" "\\1" VERSION_PATCH "${GIT_DESCRIBE}")
		elseif("${GIT_DESCRIBE}" MATCHES "^v[0-9]+\\.[0-9]+\\.[0-9a-zA-Z]+.*")
			string(REGEX REPLACE "^v[0-9]+\\.[0-9]+\\.?([0-9a-zA-Z]+).*" "\\1" VERSION_PATCH "${GIT_DESCRIBE}")
			GENERATENUMBER(VERSION_PATCH ${VERSION_PATCH})
		else()
			set(VERSION_PATCH 0)
		endif()


		if("${GIT_DESCRIBE}" MATCHES "^v[0-9]+\\.[0-9]+.*\\-[0-9]+\\-[0-9a-zA-Z]+")
			string(REGEX REPLACE "^v[0-9]+\\.[0-9]+.*\\-([0-9]+)\\-[0-9a-zA-Z]+" "\\1" ETLEGACY_VERSION_COMMIT "${GIT_DESCRIBE}")
		else()
			set(ETLEGACY_VERSION_COMMIT 0)
		endif()

		set(ETL_CMAKE_PROD_VERSION "${VERSION_MAJOR},${VERSION_MINOR},${VERSION_PATCH},${ETLEGACY_VERSION_COMMIT}")
		if ("${ETLEGACY_VERSION_COMMIT}" EQUAL "0")
			set(ETL_CMAKE_PROD_VERSION_STR "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}")
		else()
			set(ETL_CMAKE_PROD_VERSION_STR "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}.${ETLEGACY_VERSION_COMMIT}")
		endif()

		set(ETLEGACY_VERSION_MAJOR "${VERSION_MAJOR}")
		set(ETLEGACY_VERSION_MINOR "${VERSION_MINOR}")
		set(ETLEGACY_VERSION_PATCH "${VERSION_PATCH}")
		set(ETLEGACY_VERSIONPLAIN "${ETLEGACY_VERSION_MAJOR},${ETLEGACY_VERSION_MINOR},${ETLEGACY_VERSION_PATCH},${ETLEGACY_VERSION_COMMIT}")
	else()
		set(ETL_CMAKE_PROD_VERSION ${ETLEGACY_VERSIONPLAIN})
		set(ETL_CMAKE_PROD_VERSION_STR ${ETLEGACY_VERSION})
	endif()
else() # Not using source from git repo
	message(STATUS "Not using source from git repo, using default version")
	set(ETL_CMAKE_VERSION ${ETLEGACY_VERSION})
	set(ETL_CMAKE_VERSION_SHORT ${ETLEGACY_VERSION})
	set(ETL_CMAKE_PROD_VERSION ${ETLEGACY_VERSIONPLAIN})
	set(ETL_CMAKE_PROD_VERSION_STR ${ETLEGACY_VERSION})
endif()

VERSION_INT(ETL_CMAKE_VERSION_INT ${ETLEGACY_VERSION_MAJOR} ${ETLEGACY_VERSION_MINOR} ${ETLEGACY_VERSION_PATCH} ${ETLEGACY_VERSION_COMMIT})

# VANGUARD: strip leading zeros from ETL_CMAKE_VERSION_INT.
#
# VERSION_INT() above pads minor/patch/commit with leading zeros to
# fixed widths so the resulting digit string sorts lexicographically.
# That string is then emitted verbatim into version_generated.h as
#
#     #define ETL_BUILD_VERSION_INT 0031380000
#
# When the C preprocessor sees a numeric literal that starts with `0`
# it parses the rest as octal — and `8` / `9` are not valid octal
# digits, so any version with a digit >= 8 anywhere in the padded
# form fails to compile with "invalid digit ... in octal constant".
# v0.3.7 happened to dodge this because every digit was 0-7; v0.3.8
# (or any patch-version >= 8, or any minor digit >= 8) hits it.
#
# Fix: drop leading zeros so the literal is parsed as decimal. The
# value still sorts the same numerically; only the textual padding
# is removed. Empty result (all zeros) is normalised back to "0".
string(REGEX REPLACE "^0+" "" ETL_CMAKE_VERSION_INT "${ETL_CMAKE_VERSION_INT}")
if("${ETL_CMAKE_VERSION_INT}" STREQUAL "")
	set(ETL_CMAKE_VERSION_INT "0")
endif()
# END VANGUARD

if(NOT CMAKE_VERSION VERSION_LESS 3.0.2)
	string(TIMESTAMP ETL_CMAKE_BUILD_TIME "%Y-%m-%dT%H:%M:%S" UTC)
	string(TIMESTAMP ETL_CMAKE_BUILD_DATE "%Y-%m-%d" UTC)
else()
	set(ETL_CMAKE_BUILD_TIME "1999-01-01T00:00:00") # Yes this is a joke, for the systems running ancient cmake versions
	set(ETL_CMAKE_BUILD_DATE "1999-01-01")
endif()

message(STATUS "Version: ${ETLEGACY_VERSION_MAJOR}.${ETLEGACY_VERSION_MINOR}.${ETLEGACY_VERSION_PATCH}.${ETLEGACY_VERSION_COMMIT} and int version: ${ETL_CMAKE_VERSION_INT}")

get_cmake_property(_variableNames VARIABLES)
list(SORT _variableNames)
foreach(_variableName ${_variableNames})
	if("${_variableName}" MATCHES "^FEATURE_.*" AND ${_variableName} AND NOT "${_variableName}" MATCHES "_AVAILABLE$")
		string(REGEX REPLACE "^FEATURE_" "" feature_name "${_variableName}")
		string(TOLOWER "${feature_name}" feature_name)
		list(APPEND ETL_COMPILE_FEATURES "${feature_name}")
	endif()
endforeach()
list(JOIN ETL_COMPILE_FEATURES ", " ETL_COMPILE_FEATURES)
message(VERBOSE "Enabled features: ${ETL_COMPILE_FEATURES}")

# Mod version
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/etmain/ui")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/version_generated.h.in" "${CMAKE_CURRENT_BINARY_DIR}/etmain/ui/version_generated.h" @ONLY)
# This is for NSIS
string(REPLACE "," "." ETL_CMAKE_PROD_VERSIONDOT ${ETL_CMAKE_PROD_VERSION})
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/version_generated.h.in" "${CMAKE_CURRENT_BINARY_DIR}/include/version_generated.h" @ONLY)
include_directories(${CMAKE_CURRENT_BINARY_DIR}/include) # version_generated.h

target_compile_definitions(shared_libraries INTERFACE MODNAME="${MODNAME}")
target_compile_definitions(shared_libraries INTERFACE MODURL="${MODURL}")

# if(DEFINED ENV{CI_ETL_UID})
	# set(ETL_UID $ENV{CI_ETL_UID})
execute_process(COMMAND uuidgen
	OUTPUT_STRIP_TRAILING_WHITESPACE
	OUTPUT_VARIABLE ETL_UID)
# endif()
