//
// Central place for "what may be connected / nested with what".
//

#include "RelationshipRules.h"
#include "SemanticModel.h"

#include <set>

namespace StructuraSystems::Diagram {

    namespace {
        bool isPartLike(ElementKind kind) {
            return kind == ElementKind::PartDefinition || kind == ElementKind::PartUsage;
        }

        /// true if 'from' specializes 'to' directly or indirectly.
        bool specializesTransitively(const SemanticModel &model, ElementId from, ElementId to) {
            std::set<ElementId> visited;
            std::vector<ElementId> open{from};
            while (!open.empty()) {
                const ElementId current = open.back();
                open.pop_back();
                if (!visited.insert(current).second)
                    continue;
                for (const auto &relationship: model.relationships()) {
                    if (relationship.Kind != RelationshipKind::Specialization || relationship.Source != current)
                        continue;
                    if (relationship.Target == to)
                        return true;
                    open.push_back(relationship.Target);
                }
            }
            return false;
        }

        bool relationshipExists(const SemanticModel &model, RelationshipKind kind, ElementId source,
                                ElementId target, bool ignoreDirection) {
            for (const auto &relationship: model.relationships()) {
                if (relationship.Kind != kind)
                    continue;
                if (relationship.Source == source && relationship.Target == target)
                    return true;
                if (ignoreDirection && relationship.Source == target && relationship.Target == source)
                    return true;
            }
            return false;
        }
    }

    RuleResult RelationshipRules::canOwn(const SemanticModel &model, ElementId ownerId, ElementKind childKind) {
        if (ownerId == RootId) {
            if (childKind == ElementKind::Package || isPartLike(childKind) || isConstraintOrCalc(childKind) ||
                childKind == ElementKind::Textual)
                return RuleResult::ok();
            return RuleResult::denied(toString(childKind) + " needs a part or part definition as owner");
        }

        if (!model.contains(ownerId))
            return RuleResult::denied("Owner does not exist");

        const ElementKind ownerKind = model.element(ownerId).kind();
        switch (ownerKind) {
            case ElementKind::Package:
                if (childKind == ElementKind::Package || isPartLike(childKind) || isConstraintOrCalc(childKind) ||
                    childKind == ElementKind::Textual)
                    return RuleResult::ok();
                return RuleResult::denied("A package can only own packages, parts, constraints and calculations");
            case ElementKind::PartDefinition:
            case ElementKind::PartUsage:
                if (childKind == ElementKind::PartUsage || childKind == ElementKind::Attribute ||
                    childKind == ElementKind::ActionPerformance || isConstraintOrCalc(childKind) ||
                    childKind == ElementKind::Textual)
                    return RuleResult::ok();
                return RuleResult::denied("A part can only own parts, attributes, actions, constraints and calculations");
            case ElementKind::ConstraintDefinition:
            case ElementKind::CalcDefinition:
            case ElementKind::ConstraintUsage:
            case ElementKind::CalcUsage:
                if (childKind == ElementKind::Attribute || isConstraintOrCalc(childKind) ||
                    childKind == ElementKind::Textual)
                    return RuleResult::ok();
                return RuleResult::denied(toString(ownerKind) + " can only own parameters, attributes, constraints and calculations");
            case ElementKind::Attribute:
            case ElementKind::ActionPerformance:
            case ElementKind::Textual:
                return RuleResult::denied(toString(ownerKind) + " cannot own other elements");
        }
        return RuleResult::denied("Unknown owner kind");
    }

    RuleResult RelationshipRules::canReparent(const SemanticModel &model, ElementId element, ElementId newOwner) {
        if (!model.contains(element))
            return RuleResult::denied("Element does not exist");
        if (element == newOwner)
            return RuleResult::denied("An element cannot own itself");
        if (newOwner != RootId && model.isAncestor(element, newOwner))
            return RuleResult::denied("An element cannot be moved below one of its own children");
        return canOwn(model, newOwner, model.element(element).kind());
    }

    RuleResult RelationshipRules::canRelate(const SemanticModel &model, RelationshipKind kind,
                                            ElementId source, ElementId target) {
        if (!model.contains(source) || !model.contains(target))
            return RuleResult::denied("Source or target does not exist");
        if (source == target)
            return RuleResult::denied("Source and target must be different elements");

        const ElementKind sourceKind = model.element(source).kind();
        const ElementKind targetKind = model.element(target).kind();

        switch (kind) {
            case RelationshipKind::Connection:
            case RelationshipKind::Flow:
                if (sourceKind != ElementKind::PartUsage || targetKind != ElementKind::PartUsage)
                    return RuleResult::denied(toString(kind) + " is only possible between two parts");
                if (model.isAncestor(source, target) || model.isAncestor(target, source))
                    return RuleResult::denied(toString(kind) + " between a part and its own sub-part is not supported");
                if (relationshipExists(model, kind, source, target, kind == RelationshipKind::Connection))
                    return RuleResult::denied(toString(kind) + " already exists");
                return RuleResult::ok();

            case RelationshipKind::Specialization:
                if (sourceKind != targetKind || !(isPartLike(sourceKind) || isConstraintOrCalc(sourceKind)))
                    return RuleResult::denied(
                            "Specialization is only possible between two elements of the same kind "
                            "(parts, part definitions, constraints, calculations)");
                if (model.isAncestor(source, target) || model.isAncestor(target, source))
                    return RuleResult::denied("Specialization between an element and its own owner is not supported");
                if (relationshipExists(model, kind, source, target, false))
                    return RuleResult::denied("Specialization already exists");
                if (specializesTransitively(model, target, source))
                    return RuleResult::denied("Specialization would create a cycle");
                return RuleResult::ok();

            case RelationshipKind::Binding:
                if (sourceKind != ElementKind::Attribute || targetKind != ElementKind::Attribute)
                    return RuleResult::denied("Binding is only possible between two attributes or parameters");
                if (relationshipExists(model, kind, source, target, true))
                    return RuleResult::denied("Binding already exists");
                return RuleResult::ok();
        }
        return RuleResult::denied("Unknown relationship kind");
    }

    std::vector<RelationshipKind> RelationshipRules::allowedRelationshipKinds(const SemanticModel &model,
                                                                              ElementId source, ElementId target) {
        std::vector<RelationshipKind> result;
        for (const auto kind: {RelationshipKind::Connection, RelationshipKind::Flow, RelationshipKind::Specialization,
                               RelationshipKind::Binding}) {
            if (canRelate(model, kind, source, target))
                result.push_back(kind);
        }
        return result;
    }
}
