//
// Central place for "what may be connected / nested with what". Used by the SemanticModel
// to reject invalid changes and later by the editor to show only allowed options
// (e.g. the relationship popup when an arrow is released over a target).
//

#ifndef STRUCTURASYSTEMS_DIAGRAM_RELATIONSHIPRULES_H
#define STRUCTURASYSTEMS_DIAGRAM_RELATIONSHIPRULES_H

#include "SysMLElement.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace StructuraSystems::Diagram {
    class SemanticModel;

    struct RuleResult {
        bool Allowed = true;
        std::string Reason; ///< human readable, empty if allowed

        explicit operator bool() const { return Allowed; }

        static RuleResult ok() { return {true, {}}; }
        static RuleResult denied(std::string reason) { return {false, std::move(reason)}; }
    };

    class RelationshipRules {
    public:
        /// May an element of childKind be owned by ownerId (RootId = top level)?
        [[nodiscard]] static RuleResult canOwn(const SemanticModel &model, ElementId ownerId, ElementKind childKind);

        /// Composition via drag: may element be moved below newOwner?
        [[nodiscard]] static RuleResult canReparent(const SemanticModel &model, ElementId element, ElementId newOwner);

        [[nodiscard]] static RuleResult canRelate(const SemanticModel &model, RelationshipKind kind,
                                                  ElementId source, ElementId target);

        /// The element before id among its siblings that a "then" in front of id continues from: the nearest one
        /// that a succession can start at (not the never written "done", not an else branch); std::nullopt if none.
        /// An unnamed action / control node can only be the target of a succession from there ("then fork;").
        [[nodiscard]] static std::optional<ElementId> previousSuccessionEnd(const SemanticModel &model, ElementId id);

        /// Kinds offered in the popup when an arrow is dropped from source onto target.
        [[nodiscard]] static std::vector<RelationshipKind> allowedRelationshipKinds(const SemanticModel &model,
                                                                                    ElementId source,
                                                                                    ElementId target);
    };
}

#endif //STRUCTURASYSTEMS_DIAGRAM_RELATIONSHIPRULES_H
