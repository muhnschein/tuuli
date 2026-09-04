# Qt Quick plugin: `import Tuuli 1.0`

add_library(tuuliqmlplugin MODULE tuuliplugin.cpp tuuliplugin.h)
target_link_libraries(tuuliqmlplugin PRIVATE tuuli Qt5::Qml Qt5::Quick)
set_target_properties(tuuliqmlplugin PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/Tuuli)
configure_file(qmldir ${CMAKE_BINARY_DIR}/qml/Tuuli/qmldir COPYONLY)

install(TARGETS tuuliqmlplugin LIBRARY DESTINATION ${TUULI_QML_INSTALL_DIR}/Tuuli)
install(FILES qmldir DESTINATION ${TUULI_QML_INSTALL_DIR}/Tuuli)
