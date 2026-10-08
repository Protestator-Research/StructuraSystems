//
// Semantic model -> SysML v2 textual notation (graphic -> text direction).
//

#ifndef STRUCTURASYSTEMS_DIAGRAM_SYSMLCODEGENERATOR_H
#define STRUCTURASYSTEMS_DIAGRAM_SYSMLCODEGENERATOR_H

#include "../Semantic/SemanticModel.h"

#include <string>

namespace StructuraSystems::Diagram {

    class SysMLCodeGenerator {
    public:
        struct Options {
            std::string Indent = "    ";
        };

        SysMLCodeGenerator() = default;
        explicit SysMLCodeGenerator(Options options);

        /// Generates the complete text for all top-level elements of the model.
        /// Composition (ownership) becomes nesting, connections and flows are placed in the
        /// innermost common owner of their ends, specializations become ":>" in the declaration.
        [[nodiscard]] std::string generate(const SemanticModel &model) const;

        /// Text of one element including everything it owns (e.g. for a tooltip), not indented.
        [[nodiscard]] std::string generate(const SemanticModel &model, ElementId id) const;

        /// Writes a name as SysML identifier; quotes it ('...') if it is not a basic
        /// identifier or collides with a SysML keyword.
        [[nodiscard]] static std::string formatName(const std::string &name);

    private:
        void generateElement(const SemanticModel &model, ElementId id, std::size_t depth, std::string &out) const;
        void generateOwnedRelationships(const SemanticModel &model, ElementId owner, std::size_t depth,
                                        std::string &out) const;
        [[nodiscard]] std::string declaration(const SemanticModel &model, ElementId id) const;
        [[nodiscard]] std::string indent(std::size_t depth) const;

        Options GeneratorOptions;
    };
}

#endif //STRUCTURASYSTEMS_DIAGRAM_SYSMLCODEGENERATOR_H
