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

        /// The indentation a text uses for one level: a tab, or the smallest indentation of its lines in
        /// spaces ("  ", "    ", ...); four spaces if no line is indented.
        [[nodiscard]] static std::string detectIndent(const std::string &text);
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
        /// ";" or the body in braces (with "until c;" / "else { ... }" behind it) and the line break.
        void generateBody(const SemanticModel &model, ElementId id, std::size_t depth, std::string &out) const;
        void generateSuccessionsFrom(const SemanticModel &model, ElementId source, std::size_t depth, std::string &out) const;
        void generateOwnedRelationships(const SemanticModel &model, ElementId owner, std::size_t depth,
                                        std::string &out) const;
        /// One relationship written in the body of owner (an allocation with the allocations refining it).
        void generateRelationship(const SemanticModel &model, const Relationship &relationship, ElementId owner,
                                  std::size_t depth, std::string &out) const;
        [[nodiscard]] std::string declaration(const SemanticModel &model, ElementId id) const;
        [[nodiscard]] std::string indent(std::size_t depth) const;

        Options GeneratorOptions;
    };
}

#endif //STRUCTURASYSTEMS_DIAGRAM_SYSMLCODEGENERATOR_H
