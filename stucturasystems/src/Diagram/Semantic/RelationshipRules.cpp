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
        // Definitions (and packages) live in packages, on the top level and inside parts / part definitions.
        const bool definition = isDefinitionKind(childKind) || childKind == ElementKind::Package;
        const bool behavior = childKind == ElementKind::ActionUsage || childKind == ElementKind::StateUsage ||
                              childKind == ElementKind::OccurrenceUsage;
        // requirements, use cases and verifications (usages) on the top level, in packages and parts
        const bool caseUsage = childKind == ElementKind::RequirementUsage || childKind == ElementKind::UseCaseUsage ||
                               childKind == ElementKind::VerificationUsage || childKind == ElementKind::AnalysisUsage;
        if (ownerId == RootId) {
            if (definition || behavior || caseUsage || isPartLike(childKind) || isConstraintOrCalc(childKind) ||
                childKind == ElementKind::Textual)
                return RuleResult::ok();
            return RuleResult::denied(toString(childKind) + " needs a part or part definition as owner");
        }

        if (!model.contains(ownerId))
            return RuleResult::denied("Owner does not exist");

        const ElementKind ownerKind = model.element(ownerId).kind();
        const bool text = childKind == ElementKind::Textual;
        switch (ownerKind) {
            case ElementKind::Package:
                if (definition || behavior || caseUsage || isPartLike(childKind) || isConstraintOrCalc(childKind) || text)
                    return RuleResult::ok();
                return RuleResult::denied("A package can only own packages, definitions, parts, constraints and calculations");
            case ElementKind::PartDefinition:
            case ElementKind::PartUsage:
                if (childKind == ElementKind::PartUsage || childKind == ElementKind::Attribute ||
                    childKind == ElementKind::ActionPerformance || childKind == ElementKind::ItemUsage ||
                    childKind == ElementKind::PortUsage || isConstraintOrCalc(childKind) || behavior || caseUsage ||
                    childKind == ElementKind::EventOccurrence ||
                    (isDefinitionKind(childKind) && childKind != ElementKind::PartDefinition) || text)
                    return RuleResult::ok();
                return RuleResult::denied("A part can only own parts, attributes, actions, items, ports, events, "
                                          "constraints and calculations");
            case ElementKind::ConstraintDefinition:
            case ElementKind::CalcDefinition:
            case ElementKind::ConstraintUsage:
            case ElementKind::CalcUsage:
                if (childKind == ElementKind::Attribute || isConstraintOrCalc(childKind) || text)
                    return RuleResult::ok();
                return RuleResult::denied(toString(ownerKind) + " can only own parameters, attributes, constraints and calculations");
            case ElementKind::AttributeDefinition:
                if (childKind == ElementKind::Attribute || childKind == ElementKind::ConstraintUsage ||
                    childKind == ElementKind::CalcUsage || text)
                    return RuleResult::ok();
                return RuleResult::denied("An attribute definition can only own attributes, constraints and calculations");
            case ElementKind::EnumerationDefinition:
                if (childKind == ElementKind::EnumerationLiteral || text)
                    return RuleResult::ok();
                return RuleResult::denied("An enumeration definition can only own enumeration values");
            case ElementKind::ItemDefinition:
            case ElementKind::ItemUsage:
                if (childKind == ElementKind::Attribute || childKind == ElementKind::ItemUsage ||
                    childKind == ElementKind::PartUsage || childKind == ElementKind::ConstraintUsage ||
                    childKind == ElementKind::CalcUsage || childKind == ElementKind::EventOccurrence ||
                    childKind == ElementKind::OccurrenceUsage || text)
                    return RuleResult::ok();
                return RuleResult::denied("An item can only own attributes, items, parts, events, snapshots, time slices, "
                                          "constraints and calculations");
            case ElementKind::PortDefinition:
            case ElementKind::PortUsage:
                if (childKind == ElementKind::Attribute || childKind == ElementKind::ItemUsage ||
                    childKind == ElementKind::PortUsage || childKind == ElementKind::EventOccurrence || text)
                    return RuleResult::ok();
                return RuleResult::denied("A port can only own attributes, items, ports and events");
            case ElementKind::InterfaceDefinition:
                if (childKind == ElementKind::PortUsage || childKind == ElementKind::Attribute || text)
                    return RuleResult::ok();
                return RuleResult::denied("An interface definition can only own its ends (ports) and attributes");
            case ElementKind::ActionDefinition:
            case ElementKind::ActionUsage:
                if (childKind == ElementKind::ActionUsage || childKind == ElementKind::ControlNode ||
                    childKind == ElementKind::ActionPerformance || childKind == ElementKind::Attribute ||
                    childKind == ElementKind::ItemUsage || childKind == ElementKind::ConstraintUsage ||
                    childKind == ElementKind::CalcUsage || childKind == ElementKind::EventOccurrence || text)
                    return RuleResult::ok();
                return RuleResult::denied("An action can only own actions, control nodes, parameters, items and events");
            case ElementKind::StateDefinition:
            case ElementKind::StateUsage:
                if (childKind == ElementKind::StateUsage || childKind == ElementKind::ActionUsage ||
                    childKind == ElementKind::ControlNode || childKind == ElementKind::Attribute ||
                    childKind == ElementKind::ConstraintUsage || text)
                    return RuleResult::ok();
                return RuleResult::denied("A state can only own states, entry/do/exit actions and attributes");
            case ElementKind::RequirementDefinition:
            case ElementKind::RequirementUsage:
                if (childKind == ElementKind::Attribute || childKind == ElementKind::Subject ||
                    childKind == ElementKind::Actor || childKind == ElementKind::ConstraintUsage ||
                    childKind == ElementKind::CalcUsage || childKind == ElementKind::RequirementUsage ||
                    childKind == ElementKind::PartUsage || childKind == ElementKind::ItemUsage || text)
                    return RuleResult::ok();
                return RuleResult::denied("A requirement can only own its subject, stakeholders, attributes, "
                                          "constraints and requirements");
            case ElementKind::UseCaseDefinition:
            case ElementKind::UseCaseUsage:
            case ElementKind::VerificationDefinition:
            case ElementKind::VerificationUsage:
            case ElementKind::AnalysisDefinition:
            case ElementKind::AnalysisUsage:
                if (childKind == ElementKind::AnalysisUsage || childKind == ElementKind::StateUsage ||
                    childKind == ElementKind::Subject || childKind == ElementKind::Actor ||
                    childKind == ElementKind::RequirementUsage || childKind == ElementKind::Attribute ||
                    childKind == ElementKind::PartUsage || childKind == ElementKind::ItemUsage ||
                    childKind == ElementKind::ActionUsage || childKind == ElementKind::ControlNode ||
                    childKind == ElementKind::ActionPerformance || childKind == ElementKind::ConstraintUsage ||
                    childKind == ElementKind::CalcUsage || text ||
                    (childKind == ElementKind::UseCaseUsage && isUseCaseKind(ownerKind)))
                    return RuleResult::ok();
                return RuleResult::denied(std::string(isUseCaseKind(ownerKind) ? "A use case"
                                                      : isAnalysisKind(ownerKind) ? "An analysis" : "A verification") +
                                          " can only own its subject, actors, objective, actions" +
                                          (isUseCaseKind(ownerKind) ? ", included use cases" : "") + " and attributes");
            case ElementKind::OccurrenceDefinition:
            case ElementKind::OccurrenceUsage:
                // an interaction: lifelines (parts, items, occurrences) with their events, and the messages between them
                if (childKind == ElementKind::PartUsage || childKind == ElementKind::ItemUsage ||
                    childKind == ElementKind::OccurrenceUsage || childKind == ElementKind::EventOccurrence ||
                    childKind == ElementKind::PortUsage || childKind == ElementKind::Attribute ||
                    childKind == ElementKind::ActionUsage || childKind == ElementKind::ActionPerformance ||
                    childKind == ElementKind::StateUsage || childKind == ElementKind::ConstraintUsage ||
                    childKind == ElementKind::CalcUsage || text)
                    return RuleResult::ok();
                return RuleResult::denied("An occurrence can only own parts, items, occurrences, events, ports, "
                                          "attributes, actions and states");
            case ElementKind::AllocationDefinition:
                if (childKind == ElementKind::PartUsage || childKind == ElementKind::Attribute || text)
                    return RuleResult::ok();
                return RuleResult::denied("An allocation definition can only own its ends and attributes");
            case ElementKind::Attribute:
            case ElementKind::ActionPerformance:
            case ElementKind::EventOccurrence:
            case ElementKind::EnumerationLiteral:
            case ElementKind::ControlNode:
            case ElementKind::Subject:
            case ElementKind::Actor:
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
        if (source == target && kind != RelationshipKind::Transition)
            return RuleResult::denied("Source and target must be different elements");
        // unnamed elements cannot be referenced (the source of verify / include is the body they are written in)
        const auto named = [&](ElementId id) {
            for (const ElementId element: model.ownershipPath(id)) {
                if (model.element(element).name().empty())
                    return false;
            }
            return true;
        };
        if (!named(target) || (!named(source) && kind != RelationshipKind::Verify && kind != RelationshipKind::Include))
            return RuleResult::denied("An element without a name cannot be referenced");

        const ElementKind sourceKind = model.element(source).kind();
        const ElementKind targetKind = model.element(target).kind();

        switch (kind) {
            case RelationshipKind::Connection:
            case RelationshipKind::Flow:
            case RelationshipKind::Interface: {
                const auto connectable = [&](ElementKind end) {
                    return kind == RelationshipKind::Interface ? end == ElementKind::PortUsage
                                                               : end == ElementKind::PartUsage || end == ElementKind::PortUsage;
                };
                if (!connectable(sourceKind) || !connectable(targetKind))
                    return RuleResult::denied(kind == RelationshipKind::Interface
                                              ? std::string("An interface is only possible between two ports")
                                              : toString(kind) + " is only possible between parts and ports");
                if (model.isAncestor(source, target) || model.isAncestor(target, source))
                    return RuleResult::denied(toString(kind) + " between a part and its own sub-part is not supported");
                if (relationshipExists(model, kind, source, target, kind != RelationshipKind::Flow))
                    return RuleResult::denied(toString(kind) + " already exists");
                return RuleResult::ok();
            }

            case RelationshipKind::Specialization:
                if (sourceKind != targetKind ||
                    !(isPartLike(sourceKind) || isConstraintOrCalc(sourceKind) || isTypeDefinitionKind(sourceKind) ||
                      sourceKind == ElementKind::PortUsage || sourceKind == ElementKind::ItemUsage ||
                      isRequirementKind(sourceKind) || sourceKind == ElementKind::UseCaseDefinition ||
                      sourceKind == ElementKind::VerificationDefinition || isOccurrenceKind(sourceKind) ||
                      sourceKind == ElementKind::AllocationDefinition))
                    return RuleResult::denied(
                            "Specialization is only possible between two elements of the same kind "
                            "(parts, ports, items, definitions, constraints, calculations)");
                if (model.isAncestor(source, target) || model.isAncestor(target, source))
                    return RuleResult::denied("Specialization between an element and its own owner is not supported");
                if (relationshipExists(model, kind, source, target, false))
                    return RuleResult::denied("Specialization already exists");
                if (specializesTransitively(model, target, source))
                    return RuleResult::denied("Specialization would create a cycle");
                return RuleResult::ok();

            case RelationshipKind::Succession: {
                // snapshots and time slices of parts / items too ("snapshot part t0 : V; then snapshot part t1 : V;")
                const auto end = [&](ElementId id) {
                    const auto &element = model.element(id);
                    return isSuccessionEnd(element.kind()) ||
                           ((element.kind() == ElementKind::PartUsage || element.kind() == ElementKind::ItemUsage) &&
                            isTemporalKeyword(element.declaredKeyword()));
                };
                if (!end(source) || !end(target))
                    return RuleResult::denied("A succession is only possible between actions, control nodes, states and events");
                // events of different lifelines are ordered explicitly ("first driver.sent then vehicle.received;")
                const bool events = sourceKind == ElementKind::EventOccurrence && targetKind == ElementKind::EventOccurrence;
                if (model.element(source).owner() != model.element(target).owner() && !events)
                    return RuleResult::denied("A succession connects elements of the same action or state");
                if (events && (model.isAncestor(source, target) || model.isAncestor(target, source)))
                    return RuleResult::denied("A succession between an event and its own owner is not possible");
                return RuleResult::ok();
            }

            case RelationshipKind::Message:
                if (!isMessageEnd(sourceKind) || !isMessageEnd(targetKind))
                    return RuleResult::denied("A message goes from one event (or part, item, occurrence) to another");
                if (model.isAncestor(source, target) || model.isAncestor(target, source))
                    return RuleResult::denied("A message between an occurrence and its own owner is not possible");
                return RuleResult::ok();

            case RelationshipKind::Allocation: {
                const auto allocatable = [](ElementKind end) {
                    return isUsageKind(end) && end != ElementKind::EnumerationLiteral && end != ElementKind::ControlNode;
                };
                if (!allocatable(sourceKind) || !allocatable(targetKind))
                    return RuleResult::denied("Allocation is only possible between usages (parts, actions, ports, ...)");
                if (model.isAncestor(source, target) || model.isAncestor(target, source))
                    return RuleResult::denied("Allocation between an element and its own owner is not supported");
                return RuleResult::ok();
            }

            case RelationshipKind::Dependency:
                if (sourceKind == ElementKind::Textual || targetKind == ElementKind::Textual)
                    return RuleResult::denied("A dependency needs two model elements");
                if (model.isAncestor(source, target) || model.isAncestor(target, source))
                    return RuleResult::denied("A dependency between an element and its own owner is not supported");
                if (relationshipExists(model, kind, source, target, false))
                    return RuleResult::denied("Dependency already exists");
                return RuleResult::ok();

            case RelationshipKind::Transition:
                if (targetKind != ElementKind::StateUsage ||
                    !(sourceKind == ElementKind::StateUsage || sourceKind == ElementKind::ControlNode))
                    return RuleResult::denied("A transition is only possible between states");
                if (model.element(source).owner() != model.element(target).owner())
                    return RuleResult::denied("A transition connects states of the same state machine");
                return RuleResult::ok();

            case RelationshipKind::Satisfy:
                if (targetKind != ElementKind::RequirementUsage)
                    return RuleResult::denied("Satisfy needs a requirement (usage) as target, e.g. "
                                              "'requirement vehicleMass : MassRequirement'");
                if (sourceKind != ElementKind::PartUsage && sourceKind != ElementKind::PortUsage)
                    return RuleResult::denied("A requirement is satisfied by a part or port (usage)");
                if (model.isAncestor(target, source))
                    return RuleResult::denied("A requirement cannot be satisfied by an element it owns");
                if (relationshipExists(model, kind, source, target, false))
                    return RuleResult::denied("Satisfy already exists");
                return RuleResult::ok();

            case RelationshipKind::Verify:
                if (targetKind != ElementKind::RequirementUsage)
                    return RuleResult::denied("Verify needs a requirement (usage) as target, e.g. "
                                              "'requirement vehicleMass : MassRequirement'");
                if (!isVerificationKind(sourceKind))
                    return RuleResult::denied("A requirement is verified by a verification (definition or usage)");
                if (model.isAncestor(source, target))
                    return RuleResult::denied("A verification cannot verify a requirement it owns");
                if (relationshipExists(model, kind, source, target, false))
                    return RuleResult::denied("Verify already exists");
                return RuleResult::ok();

            case RelationshipKind::Include:
                if (!isUseCaseKind(sourceKind) || targetKind != ElementKind::UseCaseUsage)
                    return RuleResult::denied("A use case includes a use case (usage)");
                if (model.isAncestor(source, target) || model.isAncestor(target, source))
                    return RuleResult::denied("A use case cannot include its own owner or a use case it owns by reference");
                if (relationshipExists(model, kind, source, target, false))
                    return RuleResult::denied("Include already exists");
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
                               RelationshipKind::Binding, RelationshipKind::Interface, RelationshipKind::Succession,
                               RelationshipKind::Transition, RelationshipKind::Satisfy, RelationshipKind::Verify,
                               RelationshipKind::Include, RelationshipKind::Message, RelationshipKind::Allocation,
                               RelationshipKind::Dependency}) {
            if (canRelate(model, kind, source, target))
                result.push_back(kind);
        }
        return result;
    }
}
