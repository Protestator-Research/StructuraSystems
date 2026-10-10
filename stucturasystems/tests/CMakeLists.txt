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
