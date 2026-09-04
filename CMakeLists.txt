# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

cmake_minimum_required(VERSION 3.10)
project(tuuli VERSION 0.1.0 LANGUAGES CXX)

# Qt 5.6 is the platform (spec 3.2).  Newer Qt is only used to build and run
# the host-side tests; nothing may depend on post-5.6 API.
set(TUULI_QT_MIN 5.6)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

include(GNUInstallDirs)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# ---- Options ---------------------------------------------------------------

# servo : link the real engine through servo_capi (needs libservo-devel, or
#         TUULI_SERVO_STUB=ON to link the no-op stub for compile checks)
# mock  : in-process fake engine; host builds, emulator, UI iteration
set(TUULI_ENGINE "auto" CACHE STRING "Web engine backend: auto|servo|mock")
set_property(CACHE TUULI_ENGINE PROPERTY STRINGS auto servo mock)
option(TUULI_SERVO_STUB "Build and link the no-op servo_capi stub instead of libservo" OFF)
option(TUULI_BUILD_TESTS "Build host-side unit tests" ON)
option(TUULI_BUILD_APP "Build the tuuli-browser executable" ON)
option(TUULI_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)

# ---- Dependencies ----------------------------------------------------------

find_package(Qt5 ${TUULI_QT_MIN} REQUIRED COMPONENTS Core Gui Qml Quick Sql DBus)
if(TUULI_BUILD_TESTS)
  find_package(Qt5 ${TUULI_QT_MIN} REQUIRED COMPONENTS Test)
endif()

if(TUULI_ENGINE STREQUAL "auto")
  if(TUULI_SERVO_STUB)
    set(TUULI_ENGINE servo)
  else()
    find_package(ServoCapi QUIET)
    if(ServoCapi_FOUND)
      set(TUULI_ENGINE servo)
    else()
      set(TUULI_ENGINE mock)
    endif()
  endif()
endif()

if(TUULI_ENGINE STREQUAL "servo")
  if(TUULI_SERVO_STUB)
    add_subdirectory(servo/capi/stub)
  else()
    find_package(ServoCapi REQUIRED)
  endif()
elseif(NOT TUULI_ENGINE STREQUAL "mock")
  message(FATAL_ERROR "TUULI_ENGINE must be auto, servo or mock (got ${TUULI_ENGINE})")
endif()
message(STATUS "Tuuli engine backend: ${TUULI_ENGINE}")

# ---- Compiler flags --------------------------------------------------------

add_compile_options(-Wall -Wextra -Wno-unused-parameter)
if(TUULI_WARNINGS_AS_ERRORS)
  add_compile_options(-Werror)
endif()
# Host Qt (5.15) deprecates API that is the only option on Qt 5.6.
add_compile_options(-Wno-deprecated-declarations)

# ---- Layout ----------------------------------------------------------------

set(TUULI_QML_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/qt5/qml" CACHE PATH "Qt QML import root")
set(TUULI_DATA_INSTALL_DIR "${CMAKE_INSTALL_DATADIR}/tuuli-browser")

add_subdirectory(src/lib)
add_subdirectory(src/plugin)
if(TUULI_BUILD_APP)
  add_subdirectory(src/app)
endif()
if(TUULI_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# ---- Packaging data --------------------------------------------------------

install(FILES src/app/tuuli-browser.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
foreach(size 86 108 128 172)
  if(EXISTS "${CMAKE_SOURCE_DIR}/icons/${size}x${size}/tuuli-browser.png")
    install(FILES "icons/${size}x${size}/tuuli-browser.png"
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps)
  endif()
endforeach()
install(DIRECTORY src/qml/ DESTINATION ${TUULI_DATA_INSTALL_DIR}/qml FILES_MATCHING PATTERN "*.qml" PATTERN "*.js")
install(FILES tools/filters/README.md DESTINATION ${TUULI_DATA_INSTALL_DIR}/filters)
