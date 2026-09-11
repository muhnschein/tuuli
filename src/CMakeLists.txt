# Engine-independent core: models exposed to QML, plus the application entry point.
add_library(tuuli-core STATIC
    Core.cpp
    Core.h
    QmlTypes.cpp
    QmlTypes.h
    bookmarks/BookmarkModel.cpp
    bookmarks/BookmarkModel.h
    engine/EngineMessages.cpp
    engine/EngineMessages.h
    history/HistoryModel.cpp
    history/HistoryModel.h
    settings/Settings.cpp
    settings/Settings.h
    storage/Storage.cpp
    storage/Storage.h
    tabs/Tab.h
    tabs/TabModel.cpp
    tabs/TabModel.h
    tabs/TabPersistence.cpp
    tabs/TabPersistence.h
)
target_include_directories(tuuli-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tuuli-core PUBLIC Qt5::Core Qt5::Qml Qt5::Sql)
target_compile_definitions(tuuli-core PUBLIC
    QT_NO_CAST_FROM_ASCII
    QT_NO_CAST_TO_ASCII
    QT_NO_URL_CAST_FROM_STRING
    TUULI_VERSION="${TUULI_VERSION}"
)

add_executable(${PROJECT_NAME} main.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE tuuli-core Qt5::Gui Qt5::Quick)
# The Silica booster dlopen()s the binary and calls its exported main(): -pie -rdynamic
# is what libsailfishapp's qmake feature adds, reproduced here for CMake.
target_link_options(${PROJECT_NAME} PRIVATE -pie -rdynamic)

find_package(PkgConfig)
if(PkgConfig_FOUND)
    if(TUULI_REQUIRE_SAILFISHAPP)
        pkg_check_modules(SAILFISHAPP REQUIRED IMPORTED_TARGET sailfishapp)
    else()
        pkg_check_modules(SAILFISHAPP IMPORTED_TARGET sailfishapp)
    endif()
endif()

if(SAILFISHAPP_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE PkgConfig::SAILFISHAPP)
else()
    if(TUULI_REQUIRE_SAILFISHAPP)
        message(FATAL_ERROR "sailfishapp is required for a device build")
    endif()
    message(STATUS "sailfishapp not found: linking ${PROJECT_NAME} against tests/stubs/sailfishapp (host build)")
    target_sources(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/tests/stubs/sailfishapp/sailfishapp.cpp)
    target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/tests/stubs/sailfishapp)
    target_compile_definitions(${PROJECT_NAME} PRIVATE TUULI_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
endif()

install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)
