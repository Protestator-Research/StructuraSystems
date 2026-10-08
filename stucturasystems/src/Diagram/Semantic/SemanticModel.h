//
// Semantic layer of the diagram editor: the SysML v2 element tree plus the relationships
// drawn as edges. Contains no layout information and has no Qt dependency, so the
// diagram editor (graphic -> text) and a later importer (text -> graphic) can share it.
//

#ifndef STRUCTURASYSTEMS_DIAGRAM_SEMANTICMODEL_H
#define STRUCTURASYSTEMS_DIAGRAM_SEMANTICMODEL_H

#include "SysMLElement.h"

#include <functional>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace StructuraSystems::Diagram {

    /// Thrown when a modification would violate the RelationshipRules or reference unknown ids.
    class ModelError : public std::runtime_error {
    public:
        using std::runtime_error::runtime_error;
    };

    struct ModelChange {
        enum class Type {
            ElementAdded,
            ElementRemoved,
            ElementChanged,
            ElementReparented,
            RelationshipAdded,
            RelationshipRemoved
        };

        Type ChangeType;
        std::uint64_t Id; ///< ElementId or RelationshipId, depending on ChangeType
    };

    class SemanticModel {
    public:
        using Listener = std::function<void(const ModelChange &)>;
        using ListenerToken = std::size_t;

        SemanticModel() = default;

        // --- elements -------------------------------------------------------------------
        /// Names must be unique among the siblings; Textual elements may have an empty name and are not checked.
        ElementId addElement(ElementKind kind, const std::string &name, ElementId owner = RootId);
        /// Removes the element, all owned elements (recursively) and every relationship touching them.
        void removeElement(ElementId id);
        void rename(ElementId id, const std::string &name);
        void setTypeName(ElementId id, const std::string &typeName);
        void setMultiplicity(ElementId id, std::optional<Multiplicity> multiplicity);
        void setPerformStyle(ElementId id, PerformStyle style);
        /// Words before the keyword, e.g. {"derived", "#measurable"} (each a non-empty single word).
        void setPrefixes(ElementId id, std::vector<std::string> prefixes);
        /// Value of a part or attribute; empty expression removes it. op: "=", ":=", "default", "default =", ...
        void setValue(ElementId id, const std::string &op, const std::string &expression);
        /// References after ":>" that are not elements of the model (written as they are).
        void setExternalSpecializations(ElementId id, std::vector<std::string> references);
        /// Constraint/calc only: body expression (empty removes it).
        void setExpression(ElementId id, const std::string &expression);
        /// Keyword as written instead of the default one of the kind (std::nullopt = default), e.g. "function",
        /// "assert constraint", "feature", "" (parameter without keyword). Letters and single spaces only.
        void setDeclaredKeyword(ElementId id, std::optional<std::string> keyword);
        /// Textual only: statement text, its keywords and its name (may be empty).
        void setText(ElementId id, const std::string &text, const std::string &keyword, const std::string &name);
        /// Composition: moves the element below a new owner (appended as last child).
        void reparent(ElementId id, ElementId newOwner);

        [[nodiscard]] bool contains(ElementId id) const;
        [[nodiscard]] const SysMLElement &element(ElementId id) const;
        /// Children of an owner in their textual order. RootId returns the top-level elements.
        [[nodiscard]] const std::vector<ElementId> &children(ElementId owner) const;
        /// e.g. "vehicle::engine::cylinders"
        [[nodiscard]] std::string qualifiedName(ElementId id) const;
        /// Ids from the top-level element down to id (inclusive).
        [[nodiscard]] std::vector<ElementId> ownershipPath(ElementId id) const;
        /// true if ancestor owns descendant directly or indirectly.
        [[nodiscard]] bool isAncestor(ElementId ancestor, ElementId descendant) const;
        [[nodiscard]] std::size_t elementCount() const { return Elements.size(); }

        // --- relationships --------------------------------------------------------------
        RelationshipId addRelationship(RelationshipKind kind, ElementId source, ElementId target);
        void removeRelationship(RelationshipId id);
        [[nodiscard]] bool containsRelationship(RelationshipId id) const;
        [[nodiscard]] const Relationship &relationship(RelationshipId id) const;
        /// All relationships in creation order.
        [[nodiscard]] const std::vector<Relationship> &relationships() const { return Relationships; }

        // --- change notification (later bridged to Qt signals by the editor model) ------
        ListenerToken addListener(Listener listener);
        void removeListener(ListenerToken token);

    private:
        SysMLElement &mutableElement(ElementId id);
        std::vector<ElementId> &mutableChildren(ElementId owner);
        void ensureUniqueSiblingName(ElementId owner, const std::string &name, ElementId ignore, ElementKind kind) const;
        void notify(ModelChange::Type type, std::uint64_t id) const;

        std::map<ElementId, SysMLElement> Elements;
        std::vector<ElementId> RootElements;
        std::vector<Relationship> Relationships;
        std::map<ListenerToken, Listener> Listeners;

        ElementId NextElementId = 1;
        RelationshipId NextRelationshipId = 1;
        ListenerToken NextListenerToken = 1;
    };
}

#endif //STRUCTURASYSTEMS_DIAGRAM_SEMANTICMODEL_H
