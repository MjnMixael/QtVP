# QtVP - a VP archive viewer for FreeSpace Open.
#
# Cross-platform (Windows + Linux) CMake build, replacing the former Qt VS Tools
# .vcxproj. The VP archive core and bundled lz4 are compiled directly into the
# app.

cmake_minimum_required(VERSION 3.21)

project(QtVP
    VERSION 1.0.0
    DESCRIPTION "VP archive viewer for FreeSpace Open"
    LANGUAGES C CXX)

find_package(Qt6 6.8 REQUIRED COMPONENTS
    Core Gui Widgets Multimedia MultimediaWidgets Concurrent)

qt_standard_project_setup()

# Bundled lz4 is plain C - no Qt autogen.
set_source_files_properties(QtVP/Dependencies/lz4/lz4.c PROPERTIES SKIP_AUTOGEN ON)

qt_add_executable(QtVP WIN32
    QtVP/main.cpp
    QtVP/Icons.cpp
    QtVP/Theme.cpp
    QtVP/Core/VpArchive.cpp
    QtVP/Core/VpDocument.cpp
    QtVP/Core/VpWriter.cpp
    QtVP/Dependencies/lz4/lz4.c
    QtVP/Models/FileListModel.cpp
    QtVP/Models/FileTypeIcons.cpp
    QtVP/Models/FolderTreeModel.cpp
    QtVP/Previews/AnimationDecoders.cpp
    QtVP/Previews/ImageDecoders.cpp
    QtVP/Previews/PreviewLoader.cpp
    QtVP/Previews/PreviewWidget.cpp
    QtVP/Windows/DocumentEditor.cpp
    QtVP/Windows/FileOpener.cpp
    QtVP/Windows/MainWindow.cpp
    QtVP/Windows/OptionsDialog.cpp
    QtVP/Windows/PreviewWindow.cpp
    # Q_OBJECT headers (AUTOMOC)
    QtVP/Models/FileListModel.h
    QtVP/Models/FolderTreeModel.h
    QtVP/Previews/PreviewWidget.h
    QtVP/Windows/DocumentEditor.h
    QtVP/Windows/MainWindow.h
    QtVP/Windows/OptionsDialog.h
    QtVP/Windows/PreviewWindow.h
    # Designer forms (AUTOUIC)
    QtVP/Forms/MainWindow.ui
    QtVP/Forms/OptionsDialog.ui
)

target_compile_features(QtVP PRIVATE cxx_std_17)

# Sources include each other (and the bundled deps) by path relative to QtVP/.
target_include_directories(QtVP PRIVATE QtVP)

# The .ui files live in Forms/, but the headers that include ui_*.h are in Windows/.
set_target_properties(QtVP PROPERTIES
    AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/QtVP/Forms")

target_link_libraries(QtVP PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::MultimediaWidgets
    Qt6::Concurrent
)

if(MSVC)
    target_compile_definitions(QtVP PRIVATE _CRT_SECURE_NO_WARNINGS UNICODE _UNICODE)
endif()

# F5 in the generated VS solution launches the app, not the ALL_BUILD target.
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT QtVP)

# Developer convenience: deploy the Qt runtime next to the exe (F5 in VS).
if(WIN32)
    get_target_property(_qmake_exe Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin_dir "${_qmake_exe}" DIRECTORY)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${_qt_bin_dir}")
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET QtVP POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}" --no-translations "$<TARGET_FILE:QtVP>"
            COMMENT "Deploying Qt runtime with windeployqt"
            VERBATIM)
    endif()
endif()
