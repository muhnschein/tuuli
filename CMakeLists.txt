# harbour-tuuli — Silica web browser for Sailfish OS.
#
# Two build flavours share this file:
#   * device RPM (rpm/harbour-tuuli.spec): TUULI_BUILD_TESTS=OFF, sailfishapp required.
#   * host `make check`: tests on, sailfishapp replaced by tests/stubs/sailfishapp.
cmake_minimum_required(VERSION 3.13)
project(harbour-tuuli VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(TUULI_BUILD_TESTS "Build the host test suite" ON)
option(TUULI_COVERAGE "Instrument with gcov" OFF)
option(TUULI_REQUIRE_SAILFISHAPP "Fail instead of falling back to the host stub" OFF)
set(TUULI_VERSION "${PROJECT_VERSION}" CACHE STRING "Version stamped into the binary")

find_package(Qt5 5.6 REQUIRED COMPONENTS Core Gui Qml Quick Sql)

add_compile_options(-Wall -Wextra -Wpedantic -Werror)
if(TUULI_COVERAGE)
    add_compile_options(--coverage -O0 -g)
    add_link_options(--coverage)
endif()

add_subdirectory(src)
add_subdirectory(translations)

if(TUULI_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

install(DIRECTORY qml DESTINATION share/${PROJECT_NAME})
install(FILES ${PROJECT_NAME}.desktop DESTINATION share/applications)
foreach(size 86x86 108x108 128x128 172x172)
    install(FILES icons/${size}/${PROJECT_NAME}.png DESTINATION share/icons/hicolor/${size}/apps)
endforeach()
