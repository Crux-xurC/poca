# Poca: a thin CEF window for localhost development.
# Adapted from the CEF binary distribution's top-level CMakeLists.txt and
# tests/cefsimple/CMakeLists.txt (Copyright (c) 2014 The Chromium Embedded
# Framework Authors; BSD-style license in the distribution's LICENSE.txt).

cmake_minimum_required(VERSION 3.21)

set(CMAKE_CONFIGURATION_TYPES Debug Release CACHE STRING
    "Supported configurations" FORCE)

project(poca)

# CEF is shared with other programs (Xochipilli), not kept in this tree: one
# directory per distribution under ~/.local/share/cef/. A CEF program runs only
# with the exact version it was built against, so the name says which one.
# -DCEF_ROOT=... or the environment variable CEF_ROOT points elsewhere.
set(POCA_CEF_DIST "cef_binary_152.0.14+g8a0b5b9+chromium-152.0.7977.167_linux64")
if(DEFINED ENV{CEF_ROOT})
  set(_poca_cef_default "$ENV{CEF_ROOT}")
else()
  set(_poca_cef_default "$ENV{HOME}/.local/share/cef/${POCA_CEF_DIST}")
endif()
set(CEF_ROOT "${_poca_cef_default}" CACHE PATH "The CEF binary distribution")
if(NOT EXISTS "${CEF_ROOT}/cmake/FindCEF.cmake")
  message(FATAL_ERROR
    "CEF not found at ${CEF_ROOT}\n"
    "Download ${POCA_CEF_DIST} (https://cef-builds.spotifycdn.com/index.html) and "
    "expand it under ~/.local/share/cef/, or set CEF_ROOT. A build directory made "
    "before CEF moved keeps the old path: give -DCEF_ROOT=... once.")
endif()
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CEF_ROOT}/cmake")

# The SUID chrome-sandbox would need root to install; Poca only loads localhost.
set(USE_SANDBOX OFF CACHE BOOL "Use the CEF sandbox" FORCE)

find_package(CEF REQUIRED)

add_subdirectory(${CEF_LIBCEF_DLL_WRAPPER_PATH} libcef_dll_wrapper)

if(NOT OS_LINUX)
  message(FATAL_ERROR "Poca builds on Linux only.")
endif()

set(POCA_SRCS
  src/main.cc
  src/app.cc
  src/app.h
  src/client.cc
  src/client.h
  src/guard.cc
  src/guard.h
  src/programs.cc
  src/programs.h
  src/scheme.cc
  src/scheme.h
  src/window.cc
  src/window.h
  )

set(CEF_TARGET "poca")
ADD_LOGICAL_TARGET("libcef_lib" "${CEF_LIB_DEBUG}" "${CEF_LIB_RELEASE}")
SET_CEF_TARGET_OUT_DIR()

add_executable(${CEF_TARGET} ${POCA_SRCS})
SET_EXECUTABLE_TARGET_PROPERTIES(${CEF_TARGET})
target_include_directories(${CEF_TARGET} PRIVATE "${CEF_ROOT}" src)
add_dependencies(${CEF_TARGET} libcef_dll_wrapper)
target_link_libraries(${CEF_TARGET} libcef_lib libcef_dll_wrapper ${CEF_STANDARD_LIBS})

set_target_properties(${CEF_TARGET} PROPERTIES INSTALL_RPATH "$ORIGIN")
set_target_properties(${CEF_TARGET} PROPERTIES BUILD_WITH_INSTALL_RPATH TRUE)
set_target_properties(${CEF_TARGET} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CEF_TARGET_OUT_DIR})

COPY_FILES("${CEF_TARGET}" "${CEF_BINARY_FILES}" "${CEF_BINARY_DIR}" "${CEF_TARGET_OUT_DIR}")
COPY_FILES("${CEF_TARGET}" "${CEF_RESOURCE_FILES}" "${CEF_RESOURCE_DIR}" "${CEF_TARGET_OUT_DIR}")
if(EXISTS "${CEF_BINARY_DIR}/libminigbm.so")
  COPY_FILES("${CEF_TARGET}" "libminigbm.so" "${CEF_BINARY_DIR}" "${CEF_TARGET_OUT_DIR}")
endif()

# The pages served as poca:// live next to the executable.
add_custom_command(TARGET ${CEF_TARGET} POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CMAKE_CURRENT_SOURCE_DIR}/resources"
          "${CEF_TARGET_OUT_DIR}/poca_resources"
  COMMAND ${CMAKE_COMMAND} -E copy
          "${CMAKE_CURRENT_SOURCE_DIR}/poca.png"
          "${CEF_TARGET_OUT_DIR}/poca_resources/poca.png"
  # the window icon (small: see LoadIcon in src/window.cc)
  COMMAND ${CMAKE_COMMAND} -E copy
          "${CMAKE_CURRENT_SOURCE_DIR}/icon/poca-128.png"
          "${CMAKE_CURRENT_SOURCE_DIR}/icon/poca-256.png"
          "${CEF_TARGET_OUT_DIR}/poca_resources/"
  VERBATIM)
