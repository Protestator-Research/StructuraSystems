//
// SysML v2 / KerML textual notation -> semantic model (text -> graphic direction).
//
// Reads the part of the language the diagram models: packages, part definitions, parts, attributes (with
// prefixes, metadata and values), performed actions, constraint and calc definitions and usages (with their
// parameters and expression; KerML function / predicate), specializations (:>), connections and flows.
// Every other statement (requirements, imports, comments, ...) is kept word for word as Textual element,
// so a block survives "replace with diagram" completely. Only what cannot be kept at all is listed in the
// report.
//
// The importer is the inverse of SysMLCodeGenerator: generate(import(generate(model))) == generate(model).
//

#ifndef STRUCTURASYSTEMS_DIAGRAM_SYSMLTEXTIMPORTER_H
#define STRUCTURASYSTEMS_DIAGRAM_SYSMLTEXTIMPORTER_H

#include "../Semantic/SemanticModel.h"

#include <cstddef>
#include <optional>
#include <utility>
#include <string>
#include <vector>

namespace StructuraSystems::Diagram {

    struct ImportReport {
        /// One construct of the text that the diagram does not contain.
        struct Skipped {
            std::string What; ///< e.g. "constraint ByteRange", "value of attribute EngineLoadPercent"
            std::size_t Line = 0; ///< 1-based
        };

        std::size_t ImportedElements = 0;
        std::size_t ImportedRelationships = 0;
        std::size_t Comments = 0;
        std::vector<Skipped> SkippedConstructs;

        /// true if the diagram contains everything of the text (only formatting would change on replace).
        [[nodiscard]] bool lossless() const { return SkippedConstructs.empty() && Comments == 0; }
        /// Short, human readable list; repeated kinds are grouped, e.g.
        /// "metadata #measurable (6x), assert constraint ByteRange, computed value (3x) (+2 more)".
        [[nodiscard]] std::string summary(std::size_t maximumItems = 5) const;
        /// Every skipped construct on its own line with its line number ("line 4: assert constraint ByteRange").
        [[nodiscard]] std::string details() const;
    };

    /// What a Textual element is about, for the diagram (compartment entries, nodes).
    struct StatementSummary {
        enum class Category {
            Constraint, ///< (assert/assume/require) constraint, constraint def
            Function,   ///< function, calc, predicate
            Comment,    ///< comment, doc: kept, not shown
            Other
        };
        Category Kind = Category::Other;
        std::string Display;                 ///< one line, e.g. "decodeFuelTrim(raw : Integer) : Rational"
        std::vector<std::string> Parameters; ///< "raw : Integer" (directions other than "in" are written)
        std::string Result;                  ///< "trimPercent : Rational"
        std::string ResultType;              ///< "Rational"
        std::string Expression;              ///< body expression on one line
    };

    /// One argument of a call: "VariableA" or (named) "raw = VariableA".
    struct CallArgument {
        std::string Name;  ///< empty for a positional argument
        std::string Value; ///< expression on one line
        bool operator==(const CallArgument &other) const = default;
    };

    /// An expression that is exactly one invocation: "decodeFuelTrim(VariableA)", "Calcs::Power(f, v)".
    struct CallExpression {
        std::string Function; ///< as written, e.g. "Calcs::Power"
        std::vector<CallArgument> Arguments;
        bool operator==(const CallExpression &other) const = default;
    };

    class SysMLTextImporter {
    public:
        enum class Dialect {
            SysML,
            KerML
        };

        /// Adds the elements of text to model (normally an empty one). Never throws for bad input.
        static ImportReport importText(const std::string &text, SemanticModel &model, Dialect dialect = Dialect::SysML);

        /// Removes all elements and relationships of model.
        static void clear(SemanticModel &model);

        /// Leading keywords and name of a statement, e.g. {"assert constraint", "ByteRange"}; {"comment", ""}.
        static std::pair<std::string, std::string> describeStatement(const std::string &text);
        /// Display information of a Textual element.
        static StatementSummary summarize(const SysMLElement &element);
        /// Display information of a constraint / calc element (definition or usage) from its parameters
        /// (directed attributes), its result ("return") and its expression.
        static StatementSummary summarize(const SemanticModel &model, ElementId id);
        /// Expression text on one line (line breaks and comments removed).
        static std::string oneLine(const std::string &expression);

        // --- expressions (used by the parametric view) ---

        /// The expression if it is exactly one call "f(a, b)"; std::nullopt otherwise.
        static std::optional<CallExpression> parseCall(const std::string &expression);
        /// "f(a, b)" / "f(raw = a)".
        static std::string formatCall(const CallExpression &call);
        /// The expression if it is exactly one reference ("VariableA", "calc1.result", "'my value'"), as written.
        static std::optional<std::string> parseReference(const std::string &expression);
        /// Names referenced by the expression, as written and in order of appearance, each once ("a", "b.c").
        /// Called functions, keywords (and, or, not, true, ...) and units in [ ] are not references.
        static std::vector<std::string> references(const std::string &expression);
        /// Splits a reference as written into its segments: "a::'b c'.d" -> {"a", "b c", "d"}.
        static std::vector<std::string> splitReference(const std::string &reference);
    };
}

#endif //STRUCTURASYSTEMS_DIAGRAM_SYSMLTEXTIMPORTER_H
