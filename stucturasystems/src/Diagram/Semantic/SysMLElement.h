//
// Semantic layer of the diagram editor: one SysML v2 element without any layout information.
//

#ifndef STRUCTURASYSTEMS_DIAGRAM_SYSMLELEMENT_H
#define STRUCTURASYSTEMS_DIAGRAM_SYSMLELEMENT_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace StructuraSystems::Diagram {

    using ElementId = std::uint64_t;
    using RelationshipId = std::uint64_t;

    /// Owner id of all top-level elements.
    inline constexpr ElementId RootId = 0;

    enum class ElementKind {
        Package,
        PartDefinition,
        PartUsage,
        Attribute,
        ActionPerformance,
        /// constraint def Name { in x : Real; x > 0 }   (KerML: predicate)
        ConstraintDefinition,
        /// calc def Name { in x : Real; return r : Real = x * 2; }   (KerML: function)
        CalcDefinition,
        /// [assert] constraint name [: Type] { expression }
        ConstraintUsage,
        /// calc name [: Type] { ... }
        CalcUsage,
        /// Any other statement kept word for word (assert constraint, function, constraint def, import,
        /// comment, ...). Shown in a compartment of its owner or as node; never interpreted.
        Textual
    };

    /// Relationships that are drawn as edges. Composition is NOT part of this list:
    /// it is ownership and therefore modelled as owner/child (nesting in the text).
    enum class RelationshipKind {
        Connection,
        Flow,
        Specialization,
        /// bind source = target;  between two attributes (also parameters of constraints / calculations)
        Binding
    };

    /// constraint def, calc def and their usages: own parameters (directed attributes) and a body expression.
    [[nodiscard]] inline bool isConstraintOrCalc(ElementKind kind) {
        return kind == ElementKind::ConstraintDefinition || kind == ElementKind::CalcDefinition ||
               kind == ElementKind::ConstraintUsage || kind == ElementKind::CalcUsage;
    }

    [[nodiscard]] inline bool isConstraintKind(ElementKind kind) {
        return kind == ElementKind::ConstraintDefinition || kind == ElementKind::ConstraintUsage;
    }

    [[nodiscard]] inline bool isDefinitionKind(ElementKind kind) {
        return kind == ElementKind::PartDefinition || kind == ElementKind::ConstraintDefinition ||
               kind == ElementKind::CalcDefinition;
    }

    /// SysML multiplicity, e.g. [6], [0..1], [1..*].
    struct Multiplicity {
        std::size_t Lower = 1;
        std::optional<std::size_t> Upper = 1; ///< std::nullopt means '*'

        static Multiplicity exactly(std::size_t count);
        static Multiplicity range(std::size_t lower, std::optional<std::size_t> upper);

        [[nodiscard]] std::string toString() const;

        bool operator==(const Multiplicity &other) const = default;
    };

    /// How an ActionPerformance is written.
    enum class PerformStyle {
        DeclareAction,   ///< perform action name;   (self-contained, always valid)
        ReferenceAction  ///< perform name;          (name must resolve to an existing action)
    };

    class SysMLElement {
    public:
        SysMLElement(ElementId id, ElementKind kind, std::string name, ElementId owner);

        [[nodiscard]] ElementId id() const { return Id; }
        [[nodiscard]] ElementKind kind() const { return Kind; }
        [[nodiscard]] const std::string &name() const { return Name; }
        [[nodiscard]] ElementId owner() const { return Owner; }
        [[nodiscard]] const std::vector<ElementId> &ownedElements() const { return OwnedElements; }

        /// Type of a usage, e.g. "Engine" for "part engine : Engine". Empty = untyped.
        [[nodiscard]] const std::string &typeName() const { return TypeName; }
        [[nodiscard]] const std::optional<Multiplicity> &multiplicity() const { return MultiplicityValue; }
        [[nodiscard]] PerformStyle performStyle() const { return Perform; }

        /// Words written before the declaration keyword, in their order, e.g. {"derived", "#measurable"}.
        [[nodiscard]] const std::vector<std::string> &prefixes() const { return Prefixes; }
        /// Value of a usage ("= decodeFuelTrim(VariableA)"): operator ("=", ":=", "default", ...) and expression.
        [[nodiscard]] const std::string &valueOperator() const { return ValueOperator; }
        [[nodiscard]] const std::string &value() const { return Value; }

        /// Textual only: the complete statement (indented relative to its first line, no trailing newline)
        /// and its leading keywords, e.g. "assert constraint", "function", "constraint def", "comment".
        [[nodiscard]] const std::string &text() const { return Text; }
        [[nodiscard]] const std::string &keyword() const { return Keyword; }
        /// Constraint/calc only: the body expression (constraint condition or result expression of a calc),
        /// written last in the body without ';'. Several lines possible, indented relative to the first one.
        [[nodiscard]] const std::string &expression() const { return Expression; }
        /// Keyword as written, if it differs from the default of the kind: "function" (KerML) instead of
        /// "calc def", "assert constraint" instead of "constraint", "feature" or "" (just "in x : Real")
        /// instead of "attribute". std::nullopt = default keyword.
        [[nodiscard]] const std::optional<std::string> &declaredKeyword() const { return DeclaredKeyword; }
        /// Specializations whose target is not part of the model (e.g. a library type), as written after ":>".
        [[nodiscard]] const std::vector<std::string> &externalSpecializations() const { return ExternalSpecializations; }

    private:
        friend class SemanticModel;

        ElementId Id;
        ElementKind Kind;
        std::string Name;
        ElementId Owner;
        std::vector<ElementId> OwnedElements;
        std::string TypeName;
        std::optional<Multiplicity> MultiplicityValue;
        PerformStyle Perform = PerformStyle::DeclareAction;
        std::vector<std::string> Prefixes;
        std::string ValueOperator;
        std::string Value;
        std::string Text;
        std::string Keyword;
        std::vector<std::string> ExternalSpecializations;
        std::string Expression;
        std::optional<std::string> DeclaredKeyword;
    };

    struct Relationship {
        RelationshipId Id;
        RelationshipKind Kind;
        ElementId Source;
        ElementId Target;
    };

    [[nodiscard]] std::string toString(ElementKind kind);
    [[nodiscard]] std::string toString(RelationshipKind kind);
}

#endif //STRUCTURASYSTEMS_DIAGRAM_SYSMLELEMENT_H
