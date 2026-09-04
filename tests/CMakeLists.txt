# Host-side Qt tests (spec 13): shim logic only, no engine, no device.

set(TUULI_TESTS
  tst_cssgeometry
  tst_touchconverter
  tst_gesturearbiter
  tst_textdiff
  tst_inputmethodproxy
  tst_sessionstore
  tst_tabmodel
  tst_historymodel
  tst_bookmarkmodel
  tst_permissionstore
  tst_downloadmanager
  tst_cosmeticfilter
  tst_searchengines
  tst_preferences
  tst_connmanproxy
  tst_useragent
)

foreach(test ${TUULI_TESTS})
  add_executable(${test} ${test}.cpp)
  target_link_libraries(${test} PRIVATE tuuli Qt5::Test Qt5::Gui)
  add_test(NAME ${test} COMMAND ${test})
  set_tests_properties(${test} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endforeach()

# Plugin smoke test: needs the built QML plugin and an offscreen GL context.
add_executable(tst_plugin tst_plugin.cpp)
target_link_libraries(tst_plugin PRIVATE tuuli Qt5::Test Qt5::Gui Qt5::Quick Qt5::Qml)
target_compile_definitions(tst_plugin PRIVATE TUULI_QML_IMPORT_PATH="${CMAKE_BINARY_DIR}/qml")
add_dependencies(tst_plugin tuuliqmlplugin)
# With xvfb-run and Mesa present the test drives the real
# QQuickFramebufferObject render path (engine init on the render thread);
# otherwise it falls back to initialising the mock engine directly.
find_program(XVFB_RUN xvfb-run)
if(XVFB_RUN)
  add_test(NAME tst_plugin COMMAND ${XVFB_RUN} -a -s "-screen 0 1080x2260x24" $<TARGET_FILE:tst_plugin>)
  set_tests_properties(tst_plugin PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=xcb;LIBGL_ALWAYS_SOFTWARE=1")
else()
  add_test(NAME tst_plugin COMMAND tst_plugin)
  set_tests_properties(tst_plugin PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
