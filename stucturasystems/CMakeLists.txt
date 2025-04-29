cmake_minimum_required(VERSION 3.15)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Gui Charts)
find_package(md4c REQUIRED)
find_package(sysmllib REQUIRED)
find_package(yaml-cpp REQUIRED)

qt_standard_project_setup()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC OFF)
set(CMAKE_AUTORCC ON)

set(SOURCES
        src/main.cpp
        src/Widgets/StructuraMainWindow.cpp
        src/Widgets/StructuraMainWindow.h
        src/Models/MainWindowModel.cpp
        src/Models/MainWindowModel.h
        src/Models/CodeWidgetModel.cpp
        src/Models/CodeWidgetModel.h
        src/Widgets/CodeWidget.h
        src/Widgets/CodeWidget.cpp
        src/Models/ItemModels/ProjectItemModel.cpp
        src/Models/ItemModels/ProjectItemModel.h
        src/Models/ItemModels/ProjectTreeViewItem.h
        src/Models/ItemModels/ProjectTreeViewItem.cpp
        src/Models/Parser/StructuraSystemsParser.cpp
        src/Models/Parser/StructuraSystemsParser.h
        src/Models/Parser/Markdown/MarkdownParser.cpp
        src/Models/Parser/Markdown/MarkdownParser.h
        src/Models/Parser/Markdown/CommonmarkParser/blocks.c
        src/Models/Parser/Markdown/CommonmarkParser/buffer.c
        src/Models/Parser/Markdown/CommonmarkParser/buffer.h
        src/Models/Parser/Markdown/CommonmarkParser/case_fold.inc
        src/Models/Parser/Markdown/CommonmarkParser/chunk.h
        src/Models/Parser/Markdown/CommonmarkParser/cmark.c
        src/Models/Parser/Markdown/CommonmarkParser/cmark.h
        src/Models/Parser/Markdown/CommonmarkParser/cmark_ctype.c
        src/Models/Parser/Markdown/CommonmarkParser/cmark_ctype.h
        src/Models/Parser/Markdown/CommonmarkParser/commonmark.c
        src/Models/Parser/Markdown/CommonmarkParser/entities.inc
        src/Models/Parser/Markdown/CommonmarkParser/houdini.h
        src/Models/Parser/Markdown/CommonmarkParser/houdini_href_e.c
        src/Models/Parser/Markdown/CommonmarkParser/houdini_html_e.c
        src/Models/Parser/Markdown/CommonmarkParser/houdini_html_u.c
        src/Models/Parser/Markdown/CommonmarkParser/html.c
        src/Models/Parser/Markdown/CommonmarkParser/inlines.c
        src/Models/Parser/Markdown/CommonmarkParser/inlines.h
        src/Models/Parser/Markdown/CommonmarkParser/iterator.h
        src/Models/Parser/Markdown/CommonmarkParser/iterator.c
        src/Models/Parser/Markdown/CommonmarkParser/latex.c
        src/Models/Parser/Markdown/CommonmarkParser/node.c
        src/Models/Parser/Markdown/CommonmarkParser/node.h
        src/Models/Parser/Markdown/CommonmarkParser/man.c
        src/Models/Parser/Markdown/CommonmarkParser/parser.h
        src/Models/Parser/Markdown/CommonmarkParser/references.c
        src/Models/Parser/Markdown/CommonmarkParser/references.h
        src/Models/Parser/Markdown/CommonmarkParser/render.c
        src/Models/Parser/Markdown/CommonmarkParser/render.h
        src/Models/Parser/Markdown/CommonmarkParser/scanners.c
        src/Models/Parser/Markdown/CommonmarkParser/scanners.h
        src/Models/Parser/Markdown/CommonmarkParser/scanners.re
        src/Models/Parser/Markdown/CommonmarkParser/utf8.c
        src/Models/Parser/Markdown/CommonmarkParser/utf8.h
        src/Models/Parser/Markdown/CommonmarkParser/xml.c
        src/Models/Delegates/HtmlDelegate.cpp
        src/Models/Delegates/HtmlDelegate.h
        src/Widgets/Dialogs/EditProjectDialog.h
        src/Widgets/Dialogs/EditProjectDialog.cpp
        resources/Resources.qrc
        src/Models/SettingsModel.cpp
        src/Models/SettingsModel.h
        src/Widgets/Dialogs/SettingsDialog.cpp
        src/Widgets/Dialogs/SettingsDialog.h
        src/Models/ItemModels/ErrorItemModel.cpp
        src/Models/ItemModels/ErrorItemModel.h
        src/Models/ItemModels/ErrorItem.cpp
        src/Models/ItemModels/ErrorItem.h
        src/Widgets/ListWidgets/AddElementWidget.h
        src/Widgets/ListWidgets/AddElementWidget.cpp
        src/Widgets/ListWidgets/MarkdownElement.h
        src/Widgets/ListWidgets/MarkdownElement.cpp
        src/Services/BECommunicationService.h
        src/Services/BECommunicationService.cpp)

qt_wrap_ui(SOURCES
        ui/MainWindow.ui
        ui/CodeWidget.ui
        ui/EditProjectDialog.ui
        ui/SettingsDialog.ui
        ui/MarkdownElement.ui
        ui/AddElementWidget.ui)

add_executable(StructuraSystems ${SOURCES})



target_include_directories(StructuraSystems PUBLIC
        "${CMAKE_CURRENT_BINARY_DIR}/")

target_link_libraries(StructuraSystems PUBLIC
        Qt6::Widgets
        Qt6::Gui
        Qt6::Charts
        Qt6::Core
        md4c::md4c
        sysmllib::sysmllib
        yaml-cpp::yaml-cpp
)

if (!UNIX)
set_target_properties(StructuraSystems PROPERTIES
        WIN32_EXECUTABLE ON
        MACOSX_BUNDLE ON
)
endif()

file(GLOB_RECURSE _source_list *.cpp* *.h* *.hpp*)
foreach(_source IN ITEMS ${_source_list})
    get_filename_component(_source_path "${_source}" PATH)
    string(REPLACE "${CMAKE_SOURCE_DIR}" "" _group_path "${_source_path}")
    string(REPLACE "/" "\\" _group_path "${_group_path}")
    source_group("${_group_path}" FILES "${_source}")
endforeach()


if(MSVC)
    target_compile_options(StructuraSystems PRIVATE /W4 /WX- /bigobj)
elseif (UNIX AND NOT APPLE)
    target_compile_options(StructuraSystems PRIVATE -Wall -Wextra -Wpedantic -Werror)
else ()
    target_compile_options(StructuraSystems PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()