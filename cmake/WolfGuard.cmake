# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 wahke <info@wahke.lu> (https://wahke.lu)
# SPDX-FileCopyrightText: 2026 VanguardMod Project Contributors
#
# This file is part of VanguardMod.
# Built on ETLegacy (https://www.etlegacy.com), licensed under GPL-3.0-or-later.
# Licensed under GPL-3.0-or-later. See LICENSE for details.

# ===========================================================================
# WolfGuard.cmake — Anti-cheat integration switch
# ===========================================================================
#
# The public VanguardMod repo ships only the null provider (community build).
# Trusted core developers may clone the closed-source WolfGuard implementation
# into wolfguard/private/, then configure with -DVANGUARD_WITH_WOLFGUARD=ON
# to produce the protected build.
#
# This file exposes the function vanguard_link_wolfguard(<target>) which
# downstream targets (e.g. native qagame) call to get the right hook impl
# linked in, regardless of which flavour is being built.
# ===========================================================================

option(VANGUARD_WITH_WOLFGUARD
    "Build with WolfGuard anti-cheat (requires private repo at wolfguard/private/)"
    OFF)

set(WOLFGUARD_DIR         "${CMAKE_SOURCE_DIR}/wolfguard")
set(WOLFGUARD_PRIVATE_DIR "${WOLFGUARD_DIR}/private")

# ---------------------------------------------------------------------------
# Branch on availability and request
# ---------------------------------------------------------------------------

if(VANGUARD_WITH_WOLFGUARD)
    if(NOT EXISTS "${WOLFGUARD_PRIVATE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR
            "\n"
            "  VANGUARD_WITH_WOLFGUARD=ON but the private impl is missing.\n"
            "  Expected: ${WOLFGUARD_PRIVATE_DIR}/CMakeLists.txt\n"
            "\n"
            "  Trusted core developers: clone the private WolfGuard repo into\n"
            "    ${WOLFGUARD_PRIVATE_DIR}\n"
            "  before configuring with this option.\n"
            "\n"
            "  Otherwise build with -DVANGUARD_WITH_WOLFGUARD=OFF (the default).\n")
    endif()

    message(STATUS "VanguardMod: WolfGuard ENABLED (protected build)")
    set(VANGUARD_BUILD_PROTECTED TRUE PARENT_SCOPE)

    # The private CMakeLists.txt is responsible for defining a target named
    # "wolfguard". It must satisfy the API declared in wolfguard/wolfguard.h.
    add_subdirectory(${WOLFGUARD_PRIVATE_DIR} wolfguard_build)

    if(NOT TARGET wolfguard)
        message(FATAL_ERROR
            "wolfguard/private/CMakeLists.txt did not define a target named 'wolfguard'.")
    endif()

    set(VANGUARD_WOLFGUARD_TARGET wolfguard)
else()
    message(STATUS "VanguardMod: WolfGuard DISABLED (community build, null provider)")
    set(VANGUARD_BUILD_PROTECTED FALSE PARENT_SCOPE)

    add_library(wolfguard_null STATIC
        ${WOLFGUARD_DIR}/wolfguard_null.c
    )
    target_include_directories(wolfguard_null PUBLIC ${WOLFGUARD_DIR})
    set_target_properties(wolfguard_null PROPERTIES
        POSITION_INDEPENDENT_CODE ON)

    set(VANGUARD_WOLFGUARD_TARGET wolfguard_null)
endif()

# ---------------------------------------------------------------------------
# Link helper for downstream targets
# ---------------------------------------------------------------------------

function(vanguard_link_wolfguard target)
    target_link_libraries(${target} PRIVATE ${VANGUARD_WOLFGUARD_TARGET})
    target_include_directories(${target} PRIVATE ${WOLFGUARD_DIR})
    if(VANGUARD_BUILD_PROTECTED)
        target_compile_definitions(${target} PRIVATE
            VANGUARD_PROTECTED_BUILD=1
            VANGUARD_HAS_WOLFGUARD=1)
    else()
        target_compile_definitions(${target} PRIVATE
            VANGUARD_PROTECTED_BUILD=0
            VANGUARD_HAS_WOLFGUARD=0)
    endif()
endfunction()
