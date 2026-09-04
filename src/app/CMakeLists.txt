# tuuli-browser executable.  Links sailfishapp when the SDK provides it;
# on plain hosts it falls back to a bare QQuickView for UI iteration.

find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
  pkg_check_modules(SAILFISHAPP QUIET sailfishapp)
endif()

add_executable(tuuli-browser main.cpp)
target_link_libraries(tuuli-browser PRIVATE tuuli Qt5::Gui Qt5::Quick Qt5::Qml)
target_compile_definitions(tuuli-browser PRIVATE
  TUULI_QML_DIR="${CMAKE_SOURCE_DIR}/src/qml"
  TUULI_QML_IMPORT_PATH="${CMAKE_BINARY_DIR}/qml")

if(SAILFISHAPP_FOUND)
  target_include_directories(tuuli-browser PRIVATE ${SAILFISHAPP_INCLUDE_DIRS})
  target_link_libraries(tuuli-browser PRIVATE ${SAILFISHAPP_LIBRARIES})
  target_compile_definitions(tuuli-browser PRIVATE TUULI_HAVE_SAILFISHAPP=1)
  message(STATUS "sailfishapp found; building the Sailfish entry point")
else()
  message(STATUS "sailfishapp not found; building the host QQuickView entry point")
endif()

install(TARGETS tuuli-browser RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
