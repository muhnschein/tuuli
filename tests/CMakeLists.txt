# Host test suite. Everything here runs without a device, an SDK, or network.
find_package(Qt5 5.6 REQUIRED COMPONENTS Test Gui Quick)

# --- Silica stub module -------------------------------------------------------------
# The QML stubs are copied into the build tree so they sit next to the compiled plugin
# that provides the attached property and enum holders pure QML cannot express.
set(STUBS_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/silica-stubs)
set(STUBS_DIR ${CMAKE_BINARY_DIR}/silica-stubs)
file(GLOB_RECURSE STUB_FILES RELATIVE ${STUBS_SOURCE_DIR} ${STUBS_SOURCE_DIR}/Sailfish/*)
foreach(stub ${STUB_FILES})
    configure_file(${STUBS_SOURCE_DIR}/${stub} ${STUBS_DIR}/${stub} COPYONLY)
endforeach()

add_library(silicastubsplugin MODULE
    silica-stubs/plugin/enterkey.cpp
    silica-stubs/plugin/enterkey.h
    silica-stubs/plugin/enums.h
    silica-stubs/plugin/plugin.cpp
)
target_link_libraries(silicastubsplugin PRIVATE Qt5::Qml Qt5::Quick)
set_target_properties(silicastubsplugin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${STUBS_DIR}/Sailfish/Silica
    PREFIX "lib"
)

# --- Tests ------------------------------------------------------------------------
function(tuuli_add_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE tuuli-core Qt5::Test Qt5::Gui Qt5::Quick)
    target_compile_definitions(${name} PRIVATE
        TUULI_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        TUULI_STUBS_DIR="${STUBS_DIR}"
    )
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

foreach(test
    tst_storage
    tst_tabpersistence
    tst_tabmodel
    tst_historymodel
    tst_bookmarkmodel
    tst_settings
    tst_enginemessages
    tst_core
    tst_qmlload
    tst_qmlstatic
)
    tuuli_add_test(${test})
endforeach()
add_dependencies(tst_qmlload silicastubsplugin)
