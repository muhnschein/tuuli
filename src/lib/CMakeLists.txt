# libtuuli: everything below the QML plugin (packaged in libtuuli-qml).

set(TUULI_LIB_SOURCES
  browsercontext.cpp
  engine/engine.cpp
  engine/enginefactory.cpp
  engine/mockengine.cpp
  input/touchconverter.cpp
  input/gesturearbiter.cpp
  input/textdiff.cpp
  input/inputmethodproxy.cpp
  model/tab.cpp
  model/tabmodel.cpp
  model/sessionstore.cpp
  model/historymodel.cpp
  model/bookmarkmodel.cpp
  model/permissionstore.cpp
  model/downloadmanager.cpp
  platform/connmanproxy.cpp
  platform/transferengine.cpp
  platform/clipboardbridge.cpp
  prefs/preferences.cpp
  prefs/searchengines.cpp
  prefs/useragent.cpp
  blocking/cosmeticfilter.cpp
  perf/perflog.cpp
  view/qtrenderingcontext.cpp
  view/webviewrenderer.cpp
  view/tuuliwebview.cpp
  view/imageprovider.cpp
)

set(TUULI_LIB_HEADERS
  tuuli_global.h
  browsercontext.h
  engine/engine.h
  engine/enginefactory.h
  engine/mockengine.h
  input/cssgeometry.h
  input/touchconverter.h
  input/gesturearbiter.h
  input/textdiff.h
  input/inputmethodproxy.h
  model/tab.h
  model/tabmodel.h
  model/sessionstore.h
  model/historymodel.h
  model/bookmarkmodel.h
  model/permissionstore.h
  model/downloadmanager.h
  platform/connmanproxy.h
  platform/transferengine.h
  platform/clipboardbridge.h
  prefs/preferences.h
  prefs/servoprefs.h
  prefs/searchengines.h
  prefs/useragent.h
  blocking/cosmeticfilter.h
  perf/perflog.h
  view/qtrenderingcontext.h
  view/webviewrenderer.h
  view/tuuliwebview.h
  view/imageprovider.h
)

if(TUULI_ENGINE STREQUAL "servo")
  list(APPEND TUULI_LIB_SOURCES engine/servoengine.cpp engine/servowebview.cpp)
  list(APPEND TUULI_LIB_HEADERS engine/servoengine.h engine/servowebview.h)
endif()

add_library(tuuli SHARED ${TUULI_LIB_SOURCES} ${TUULI_LIB_HEADERS})
set_target_properties(tuuli PROPERTIES
  VERSION ${PROJECT_VERSION}
  SOVERSION 0)
target_include_directories(tuuli PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tuuli PUBLIC Qt5::Core Qt5::Gui Qt5::Qml Qt5::Quick Qt5::Sql Qt5::DBus)
target_compile_definitions(tuuli PUBLIC TUULI_VERSION="${PROJECT_VERSION}")

if(TUULI_ENGINE STREQUAL "servo")
  target_link_libraries(tuuli PRIVATE ServoCapi::servo)
  target_compile_definitions(tuuli PUBLIC TUULI_ENGINE_SERVO=1)
endif()

install(TARGETS tuuli LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
