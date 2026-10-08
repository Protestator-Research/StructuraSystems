cmake_minimum_required(VERSION 3.15)

qt_standard_project_setup(REQUIRES 6.8)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC OFF)
set(CMAKE_AUTORCC ON)

set(SOURCES
        src/main.cpp
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
        resources/Resources.qrc
        ../resources/sysml.qrc
        src/Models/SettingsModel.cpp
        src/Models/SettingsModel.h
        src/Services/BECommunicationService.h
        src/Services/BECommunicationService.cpp
        src/Services/entities/DigitalTwin.h
        src/Services/entities/DigitalTwin.cpp)


qt_add_executable(StructuraSystems ${SOURCES})

set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(StructuraSystems
        URI StructuraSystems
        VERSION 1.0
        QML_FILES
            qml/Main.qml
            qml/Theme.qml
            qml/components/DialogFooter.qml
            qml/components/Icon.qml
            qml/components/IconToolButton.qml
            qml/components/EmptyState.qml
            qml/components/InsertDivider.qml
            qml/components/LanguageChip.qml
            qml/components/LineNumberGutter.qml
            qml/components/LanguageMenu.qml
            qml/components/NavigationRail.qml
            qml/components/NavRailItem.qml
            qml/components/ProjectList.qml
            qml/components/SegmentedControl.qml
            qml/components/Snackbar.qml
            qml/views/DigitalTwinPane.qml
            qml/views/EditorView.qml
            qml/views/ElementCard.qml
            qml/views/ExplorerPane.qml
            qml/views/OnlinePane.qml
            qml/views/ProblemsSheet.qml
            qml/views/StatusBar.qml
            qml/dialogs/AboutDialog.qml
            qml/dialogs/CloseDocumentDialog.qml
            qml/dialogs/CommitDialog.qml
            qml/dialogs/DetailsDialog.qml
            qml/dialogs/DigitalTwinWizard.qml
            qml/dialogs/NewProjectDialog.qml
            qml/dialogs/QuitDialog.qml
            qml/dialogs/SettingsDialog.qml
        SOURCES
            src/ViewModels/SysMLHighlighter.cpp
            src/ViewModels/SysMLHighlighter.h
            src/ViewModels/AppController.cpp
            src/ViewModels/AppController.h
            src/ViewModels/DocumentModel.cpp
            src/ViewModels/DocumentModel.h
            src/ViewModels/OpenDocumentsModel.cpp
            src/ViewModels/OpenDocumentsModel.h
            src/ViewModels/ProblemListModel.cpp
            src/ViewModels/ProblemListModel.h
            src/ViewModels/ProjectFilterModel.cpp
            src/ViewModels/ProjectFilterModel.h
            src/ViewModels/SettingsController.cpp
            src/ViewModels/SettingsController.h
            src/Models/ItemModels/ProjectItemModel.cpp
            src/Models/ItemModels/ProjectItemModel.h)



# The QML type registration and moc include the ViewModel headers by their plain file name.
target_include_directories(StructuraSystems PUBLIC
        "${CMAKE_CURRENT_BINARY_DIR}/"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/ViewModels"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/Models/ItemModels")

target_link_libraries(StructuraSystems PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::Concurrent
        md4c::md4c
        sysmllib::sysmllib
        yaml-cpp::yaml-cpp)

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