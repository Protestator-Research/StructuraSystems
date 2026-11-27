# Core of the graphical SysML v2 editor: semantic model, rules and code generation.
# Plain C++20 without Qt, so it can be unit tested on its own and later moved into sysmllib.

add_library(StructuraDiagramCore STATIC
        Semantic/SysMLElement.h
        Semantic/SysMLElement.cpp
        Semantic/SemanticModel.h
        Semantic/SemanticModel.cpp
        Semantic/RelationshipRules.h
        Semantic/RelationshipRules.cpp
        Conversion/SysMLCodeGenerator.h
        Conversion/SysMLCodeGenerator.cpp
        Conversion/GeneratedBlockGuard.h
        Conversion/GeneratedBlockGuard.cpp
        Conversion/SysMLTextImporter.h
        Conversion/SysMLTextImporter.cpp)

# Include as "Diagram/Semantic/SemanticModel.h"
target_include_directories(StructuraDiagramCore PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/..")

target_compile_features(StructuraDiagramCore PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(StructuraDiagramCore PRIVATE /W4 /WX- /bigobj)
else()
    target_compile_options(StructuraDiagramCore PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# No Qt in this library: keep the Qt code generators (enabled in the parent directory) off.
set_target_properties(StructuraDiagramCore PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
