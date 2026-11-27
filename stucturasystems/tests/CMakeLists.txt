cmake_minimum_required(VERSION 3.15)

# Unit tests without external test framework (no additional Conan dependency needed).
# Run with:  ctest --test-dir <build-dir> --output-on-failure

add_executable(StructuraDiagramCoreTests
        TestFramework.h
        TestMain.cpp
        Diagram/SemanticModelTest.cpp
        Diagram/RelationshipRulesTest.cpp
        Diagram/SysMLCodeGeneratorTest.cpp
        Diagram/GeneratedBlockGuardTest.cpp
        Diagram/SysMLTextImporterTest.cpp)

target_link_libraries(StructuraDiagramCoreTests PRIVATE StructuraDiagramCore)

if(MSVC)
    target_compile_options(StructuraDiagramCoreTests PRIVATE /W4 /WX-)
else()
    target_compile_options(StructuraDiagramCoreTests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_test(NAME StructuraDiagramCoreTests COMMAND StructuraDiagramCoreTests)

set_target_properties(StructuraDiagramCoreTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

# View model tests of the graphical editor (controller, code generation into the document's block).
# Need Qt Core only: the document is a fake, so neither sysmllib nor a QML engine is involved.
set(DIAGRAM_VIEWMODEL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../src/ViewModels/Diagram")
add_executable(StructuraDiagramViewModelTests
        TestFramework.h
        ViewModels/ViewModelTestMain.cpp
        ViewModels/QtPrinting.h
        ViewModels/DiagramControllerTest.cpp
        ViewModels/DiagramEditorModelTest.cpp
        ViewModels/DiagramPreviewModelTest.cpp
        ${DIAGRAM_VIEWMODEL_DIR}/DiagramTypes.h
        ${DIAGRAM_VIEWMODEL_DIR}/DiagramGeometry.h
        ${DIAGRAM_VIEWMODEL_DIR}/SysMLBlockTarget.h
        ${DIAGRAM_VIEWMODEL_DIR}/DiagramNodeModel.h
        ${DIAGRAM_VIEWMODEL_DIR}/DiagramNodeModel.cpp
        ${DIAGRAM_VIEWMODEL_DIR}/DiagramEdgeModel.h
        ${DIAGRAM_VIEWMODEL_DIR}/DiagramEdgeModel.cpp
        ${DIAGRAM_VIEWMODEL_DIR}/DiagramController.h
        ${DIAGRAM_VIEWMODEL_DIR}/DiagramLayout.h
        ${DIAGRAM_VIEWMODEL_DIR}/EdgeRouting.h
        ${DIAGRAM_VIEWMODEL_DIR}/DiagramController.cpp
        ${DIAGRAM_VIEWMODEL_DIR}/DiagramEditorModel.h
        ${DIAGRAM_VIEWMODEL_DIR}/DiagramEditorModel.cpp
        ${DIAGRAM_VIEWMODEL_DIR}/DiagramPreviewModel.h
        ${DIAGRAM_VIEWMODEL_DIR}/DiagramPreviewModel.cpp
        ${DIAGRAM_VIEWMODEL_DIR}/ParametricController.h
        ${DIAGRAM_VIEWMODEL_DIR}/ParametricController.cpp
        ${DIAGRAM_VIEWMODEL_DIR}/BehaviorController.h
        ${DIAGRAM_VIEWMODEL_DIR}/BehaviorController.cpp
        ${DIAGRAM_VIEWMODEL_DIR}/UseCaseController.h
        ${DIAGRAM_VIEWMODEL_DIR}/UseCaseController.cpp
        ${DIAGRAM_VIEWMODEL_DIR}/SequenceController.h
        ${DIAGRAM_VIEWMODEL_DIR}/SequenceController.cpp
        ViewModels/ParametricControllerTest.cpp
        ViewModels/BehaviorControllerTest.cpp
        ViewModels/UseCaseControllerTest.cpp
        ViewModels/SequenceControllerTest.cpp)

target_include_directories(StructuraDiagramViewModelTests PRIVATE "${DIAGRAM_VIEWMODEL_DIR}")
# Qt6::Qml only for the QML registration macros in the headers.
target_link_libraries(StructuraDiagramViewModelTests PRIVATE StructuraDiagramCore Qt6::Core Qt6::Qml)
set_target_properties(StructuraDiagramViewModelTests PROPERTIES AUTOMOC ON AUTOUIC OFF AUTORCC OFF)

if(MSVC)
    target_compile_options(StructuraDiagramViewModelTests PRIVATE /W4 /WX-)
else()
    target_compile_options(StructuraDiagramViewModelTests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_test(NAME StructuraDiagramViewModelTests COMMAND StructuraDiagramViewModelTests)

# ---------------------------------------------------------------- tests from NewUI (Qt Test)
# Qt Test is used (not GTest): Qt is already a dependency of the project, and the code under test uses Qt types
# (QString, QDir, QFile). The tests only link the library resolution code, not the QML application.
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(SRC "${CMAKE_CURRENT_SOURCE_DIR}/../src")

# The Markdown parser (cmark) reads the code blocks of .md library files.
file(GLOB CMARK_SOURCES "${SRC}/Models/Parser/Markdown/CommonmarkParser/*.c")
list(FILTER CMARK_SOURCES EXCLUDE REGEX "main\\.c$")

# (The standard library comes from sysmllib: StandardLibraryResources.h, no resource file here.)
qt_add_executable(LibraryWorkspaceTests
        tst_LibraryWorkspace.cpp
        ${SRC}/Models/Libraries/CodeScanner.cpp
        ${SRC}/Models/Libraries/CodeScanner.h
        ${SRC}/Models/Libraries/DocumentWorkspace.cpp
        ${SRC}/Models/Libraries/DocumentWorkspace.h
        ${SRC}/Models/Libraries/LibraryWorkspace.cpp
        ${SRC}/Models/Libraries/LibraryWorkspace.h
        ${SRC}/Models/Libraries/RemoteLibraries.cpp
        ${SRC}/Models/Libraries/RemoteLibraries.h
        ${SRC}/Models/Libraries/WorkspaceConfig.cpp
        ${SRC}/Models/Libraries/WorkspaceConfig.h
        ${SRC}/Models/Parser/Markdown/MarkdownParser.cpp
        # DocumentWorkspace builds the name index of the code completion.
        ${SRC}/ViewModels/CompletionEngine.cpp
        ${SRC}/ViewModels/LanguageKeywords.cpp
        # The communication with the server (the project usages are read from and written to commits).
        ${SRC}/Services/BECommunicationService.cpp
        ${SRC}/Services/BECommunicationService.h
        ${SRC}/Services/CommitDelta.cpp
        ${SRC}/Services/PagedRequest.cpp
        ${SRC}/Services/entities/DigitalTwin.cpp
        ${SRC}/Services/entities/DigitalTwinRequest.cpp
        ${CMARK_SOURCES})

target_link_libraries(LibraryWorkspaceTests PRIVATE
        Qt6::Core
        Qt6::Test
        sysmllib::sysmllib
        CURL::libcurl
        nlohmann_json::nlohmann_json)

add_test(NAME LibraryWorkspaceTests COMMAND LibraryWorkspaceTests)
# Parsing the standard library takes about a minute in a debug build.
set_tests_properties(LibraryWorkspaceTests PROPERTIES TIMEOUT 900)

# Benchmark of DocumentWorkspace (warm-up, first update, update after a change, name index). Not a test: no add_test, run it by hand.
qt_add_executable(DocumentWorkspaceBenchmark
        tst_DocumentWorkspaceBenchmark.cpp
        ${SRC}/Models/Libraries/CodeScanner.cpp
        ${SRC}/Models/Libraries/CodeScanner.h
        ${SRC}/Models/Libraries/DocumentWorkspace.cpp
        ${SRC}/Models/Libraries/DocumentWorkspace.h
        ${SRC}/Models/Libraries/LibraryWorkspace.cpp
        ${SRC}/Models/Libraries/LibraryWorkspace.h
        ${SRC}/Models/Libraries/RemoteLibraries.cpp
        ${SRC}/Models/Libraries/RemoteLibraries.h
        ${SRC}/Models/Libraries/WorkspaceConfig.cpp
        ${SRC}/Models/Libraries/WorkspaceConfig.h
        ${SRC}/Models/Parser/Markdown/MarkdownParser.cpp
        ${SRC}/ViewModels/CompletionEngine.cpp
        ${SRC}/ViewModels/LanguageKeywords.cpp
        ${SRC}/Services/BECommunicationService.cpp
        ${SRC}/Services/BECommunicationService.h
        ${SRC}/Services/CommitDelta.cpp
        ${SRC}/Services/PagedRequest.cpp
        ${SRC}/Services/entities/DigitalTwin.cpp
        ${SRC}/Services/entities/DigitalTwinRequest.cpp
        ${CMARK_SOURCES})

target_link_libraries(DocumentWorkspaceBenchmark PRIVATE
        Qt6::Core
        Qt6::Test
        sysmllib::sysmllib
        CURL::libcurl
        nlohmann_json::nlohmann_json)

# DocumentWorkspace::warmUp(): steps, progress, isWarm(), cancellation. Loads the standard library several times (about 7 s each in a debug build).
qt_add_executable(WarmUpTests
        tst_WarmUp.cpp
        ${SRC}/Models/Libraries/CodeScanner.cpp
        ${SRC}/Models/Libraries/CodeScanner.h
        ${SRC}/Models/Libraries/DocumentWorkspace.cpp
        ${SRC}/Models/Libraries/DocumentWorkspace.h
        ${SRC}/Models/Libraries/LibraryWorkspace.cpp
        ${SRC}/Models/Libraries/LibraryWorkspace.h
        ${SRC}/Models/Libraries/RemoteLibraries.cpp
        ${SRC}/Models/Libraries/RemoteLibraries.h
        ${SRC}/Models/Libraries/WorkspaceConfig.cpp
        ${SRC}/Models/Libraries/WorkspaceConfig.h
        ${SRC}/Models/Parser/Markdown/MarkdownParser.cpp
        ${SRC}/ViewModels/CompletionEngine.cpp
        ${SRC}/ViewModels/LanguageKeywords.cpp
        ${SRC}/Services/BECommunicationService.cpp
        ${SRC}/Services/BECommunicationService.h
        ${SRC}/Services/CommitDelta.cpp
        ${SRC}/Services/PagedRequest.cpp
        ${SRC}/Services/entities/DigitalTwin.cpp
        ${SRC}/Services/entities/DigitalTwinRequest.cpp
        ${CMARK_SOURCES})

target_link_libraries(WarmUpTests PRIVATE
        Qt6::Core
        Qt6::Test
        sysmllib::sysmllib
        CURL::libcurl
        nlohmann_json::nlohmann_json)

add_test(NAME WarmUpTests COMMAND WarmUpTests)
set_tests_properties(WarmUpTests PROPERTIES TIMEOUT 900)

# Synchronisation with the server (SysML v2 API): delta commits, branches without head and pagination. The answers of the server are
# faked, so no network is needed. The online DocumentModel is part of the test to check that a branch without head can be opened.
qt_add_executable(OnlineSyncTests
        tst_OnlineSync.cpp
        ${SRC}/Services/BECommunicationService.cpp
        ${SRC}/Services/CommitDelta.cpp
        ${SRC}/Services/PagedRequest.cpp
        ${SRC}/Services/entities/DigitalTwin.cpp
        ${SRC}/Services/entities/DigitalTwinRequest.cpp)

target_link_libraries(OnlineSyncTests PRIVATE
        Qt6::Core
        Qt6::Test
        sysmllib::sysmllib)

add_test(NAME OnlineSyncTests COMMAND OnlineSyncTests)
set_tests_properties(OnlineSyncTests PROPERTIES TIMEOUT 120)

# The online DocumentModel: a project without commits is an empty document, opening and committing changes nothing.
find_package(yaml-cpp REQUIRED)
qt_add_executable(OnlineDocumentTests
        tst_OnlineDocument.cpp
        ${SRC}/ViewModels/DocumentModel.cpp
        ${SRC}/ViewModels/ElementTreeModel.cpp
        ${SRC}/ViewModels/ProblemListModel.cpp
        # The names of the model for the code completion (DocumentWorkspace builds the index, DocumentModel keeps it).
        ${SRC}/ViewModels/CompletionEngine.cpp
        ${SRC}/ViewModels/LanguageKeywords.cpp
        ${SRC}/Services/CommitDelta.cpp
        ${SRC}/Models/Libraries/CodeScanner.cpp
        ${SRC}/Models/Libraries/DocumentWorkspace.cpp
        ${SRC}/Models/Libraries/LibraryWorkspace.cpp
        ${SRC}/Models/Libraries/RemoteLibraries.cpp
        ${SRC}/Models/Libraries/WorkspaceConfig.cpp
        ${SRC}/Models/Parser/StructuraSystemsParser.cpp
        ${SRC}/Models/Parser/Markdown/MarkdownParser.cpp
        ${CMARK_SOURCES})

# The headers of the view models use the QML registration macros, but nothing of QtQml is needed at run time. Linking Qt6::Qml
# would scan the sources for QML imports, so only its headers are used.
target_include_directories(OnlineDocumentTests PRIVATE $<TARGET_PROPERTY:Qt6::Qml,INTERFACE_INCLUDE_DIRECTORIES>)

target_link_libraries(OnlineDocumentTests PRIVATE
        Qt6::Core
        Qt6::Test
        md4c::md4c
        yaml-cpp::yaml-cpp
        sysmllib::sysmllib)

add_test(NAME OnlineDocumentTests COMMAND OnlineDocumentTests)
set_tests_properties(OnlineDocumentTests PROPERTIES TIMEOUT 120)

# The editing help of the code editor (indentation, bracket pairs, tab handling) is pure text logic.
qt_add_executable(CodeEditAssistTests
        tst_CodeEditAssist.cpp
        ${SRC}/ViewModels/CodeEditAssist.cpp
        ${SRC}/ViewModels/CodeEditAssist.h)

# Only the headers of QtQml are needed for the QML registration macros, see OnlineDocumentTests.
target_include_directories(CodeEditAssistTests PRIVATE $<TARGET_PROPERTY:Qt6::Qml,INTERFACE_INCLUDE_DIRECTORIES>)

target_link_libraries(CodeEditAssistTests PRIVATE
        Qt6::Core
        Qt6::Test)

add_test(NAME CodeEditAssistTests COMMAND CodeEditAssistTests)
set_tests_properties(CodeEditAssistTests PROPERTIES TIMEOUT 60)

# The code completion: pure text logic (context analysis, filtering, snippets, name index). The keywords come from the ANTLR lexers of
# sysmllib, so no QtQml is needed.
qt_add_executable(CompletionEngineTests
        tst_CompletionEngine.cpp
        ${SRC}/ViewModels/CompletionEngine.cpp
        ${SRC}/ViewModels/CompletionEngine.h
        ${SRC}/ViewModels/LanguageKeywords.cpp
        ${SRC}/ViewModels/LanguageKeywords.h)

target_link_libraries(CompletionEngineTests PRIVATE
        Qt6::Core
        Qt6::Test
        sysmllib::sysmllib)

add_test(NAME CompletionEngineTests COMMAND CompletionEngineTests)
set_tests_properties(CompletionEngineTests PROPERTIES TIMEOUT 60)

# The standard library of sysmllib in the text and in the XMI format: the same diagnostics and names for a set of documents. Loads both
# formats (parsing the text takes about a minute in a debug build).
qt_add_executable(StandardFormatTests
        tst_StandardFormat.cpp
        ${SRC}/Models/Libraries/CodeScanner.cpp
        ${SRC}/Models/Libraries/CodeScanner.h
        ${SRC}/Models/Libraries/DocumentWorkspace.cpp
        ${SRC}/Models/Libraries/DocumentWorkspace.h
        ${SRC}/Models/Libraries/LibraryWorkspace.cpp
        ${SRC}/Models/Libraries/LibraryWorkspace.h
        ${SRC}/Models/Libraries/RemoteLibraries.cpp
        ${SRC}/Models/Libraries/RemoteLibraries.h
        ${SRC}/Models/Libraries/WorkspaceConfig.cpp
        ${SRC}/Models/Libraries/WorkspaceConfig.h
        ${SRC}/Models/Parser/Markdown/MarkdownParser.cpp
        ${SRC}/ViewModels/CompletionEngine.cpp
        ${SRC}/ViewModels/LanguageKeywords.cpp
        ${SRC}/Services/BECommunicationService.cpp
        ${SRC}/Services/BECommunicationService.h
        ${SRC}/Services/CommitDelta.cpp
        ${SRC}/Services/PagedRequest.cpp
        ${SRC}/Services/entities/DigitalTwin.cpp
        ${SRC}/Services/entities/DigitalTwinRequest.cpp
        ${CMARK_SOURCES})

target_link_libraries(StandardFormatTests PRIVATE
        Qt6::Core
        Qt6::Test
        sysmllib::sysmllib
        CURL::libcurl
        nlohmann_json::nlohmann_json)

add_test(NAME StandardFormatTests COMMAND StandardFormatTests)
set_tests_properties(StandardFormatTests PROPERTIES TIMEOUT 900)

# The name index in two stages (libraries and document) against the flat index.
qt_add_executable(NameIndexStageTests
        tst_NameIndexStages.cpp
        ${SRC}/ViewModels/CompletionEngine.cpp
        ${SRC}/ViewModels/CompletionEngine.h
        ${SRC}/ViewModels/LanguageKeywords.cpp
        ${SRC}/ViewModels/LanguageKeywords.h)

target_link_libraries(NameIndexStageTests PRIVATE
        Qt6::Core
        Qt6::Test
        sysmllib::sysmllib)

add_test(NAME NameIndexStageTests COMMAND NameIndexStageTests)
set_tests_properties(NameIndexStageTests PROPERTIES TIMEOUT 60)

# ctest starts the executable without the environment of the Conan build, so the shared libraries it needs (sysmllib and its
# dependencies, Qt) are put on the PATH of the test. The directories come from the variables the Conan CMake generator sets
# for every package (<package>_BIN_DIRS_<CONFIG>); only directories that exist are used.
if(WIN32 AND CMAKE_VERSION VERSION_GREATER_EQUAL 3.22)
    # Only the directories of the configuration that runs the test (ctest -C <config>) are used.
    set(_modifications "")
    get_cmake_property(_all_variables VARIABLES)
    foreach(_variable IN LISTS _all_variables)
        if(_variable MATCHES "_BIN_DIRS_(DEBUG|RELEASE|RELWITHDEBINFO|MINSIZEREL)$")
            string(TOUPPER "${CMAKE_MATCH_1}" _config_key)
            set(_config_name "")
            if(_config_key STREQUAL "DEBUG")
                set(_config_name "Debug")
            elseif(_config_key STREQUAL "RELEASE")
                set(_config_name "Release")
            elseif(_config_key STREQUAL "RELWITHDEBINFO")
                set(_config_name "RelWithDebInfo")
            else()
                set(_config_name "MinSizeRel")
            endif()
            foreach(_dir IN LISTS ${_variable})
                if(IS_DIRECTORY "${_dir}")
                    list(APPEND _modifications "PATH=path_list_prepend:$<$<CONFIG:${_config_name}>:${_dir}>")
                endif()
            endforeach()
        endif()
    endforeach()
    # Qt: <prefix>/lib/cmake/Qt6 -> <prefix>/bin
    get_filename_component(_qt_bin "${Qt6_DIR}/../../../bin" ABSOLUTE)
    if(IS_DIRECTORY "${_qt_bin}")
        list(APPEND _modifications "PATH=path_list_prepend:${_qt_bin}")
    endif()
    list(REMOVE_DUPLICATES _modifications)
    set_tests_properties(LibraryWorkspaceTests OnlineSyncTests OnlineDocumentTests PROPERTIES ENVIRONMENT_MODIFICATION "${_modifications}")
endif()



# The status of the warm-up of the standard library shown in the status bar (texts, progress, hand-over from the warm-up thread).
qt_add_executable(LibraryWarmUpStatusTests
        tst_LibraryWarmUpStatus.cpp
        ${SRC}/ViewModels/LibraryWarmUpStatus.cpp
        ${SRC}/ViewModels/LibraryWarmUpStatus.h)

target_link_libraries(LibraryWarmUpStatusTests PRIVATE
        Qt6::Core
        Qt6::Qml
        Qt6::Test
        sysmllib::sysmllib)

add_test(NAME LibraryWarmUpStatusTests COMMAND LibraryWarmUpStatusTests)
