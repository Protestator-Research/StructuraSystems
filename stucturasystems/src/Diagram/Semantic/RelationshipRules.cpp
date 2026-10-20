//
// Central place for "what may be connected / nested with what".
//

#include "RelationshipRules.h"
#include "SemanticModel.h"

#include <algorithm>
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
            // metadata ("@Safety;", "metadata m about a;"), imports and aliases: in any namespace
        const bool annotation = childKind == ElementKind::MetadataUsage || childKind == ElementKind::Textual;
        const bool namespaceMember = childKind == ElementKind::Import || childKind == ElementKind::Alias;
        // filters and views on package level
        const bool packageMember = childKind == ElementKind::Filter || childKind == ElementKind::ViewUsage;
        if (ownerId == RootId) {
            if (definition || behavior || caseUsage || isPartLike(childKind) || isConstraintOrCalc(childKind) ||
                annotation || namespaceMember || packageMember)
                return RuleResult::ok();
            return RuleResult::denied(toString(childKind) + " needs a part or part definition as owner");
        }

        if (!model.contains(ownerId))
            return RuleResult::denied("Owner does not exist");

        const ElementKind ownerKind = model.element(ownerId).kind();
        const bool text = childKind == ElementKind::Textual;
        switch (ownerKind) {
            case ElementKind::EnumerationLiteral:
            case ElementKind::ControlNode:
            case ElementKind::Textual:
            case ElementKind::Import:
            case ElementKind::Expose:
            case ElementKind::Render:
            case ElementKind::Filter:
                return RuleResult::denied(toString(ownerKind) + " cannot own other elements");
            case ElementKind::MetadataUsage:
                // "@ToolVariable { name = "deltaT"; }": the values of its attributes
                if (childKind == ElementKind::Attribute || text)
                    return RuleResult::ok();
                return RuleResult::denied("Metadata can only own the values of its attributes");
            case ElementKind::Alias:
                if (annotation)
                    return RuleResult::ok();
                return RuleResult::denied("An alias can only own comments and metadata");
            default:
                if (annotation || namespaceMember)
                    return RuleResult::ok();
                break;
        }
        switch (ownerKind) {
            case ElementKind::Package:
                if (definition || behavior || caseUsage || isPartLike(childKind) || isConstraintOrCalc(childKind) || text ||
                    packageMember)
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
            case ElementKind::ActionPerformance: // "perform action dynamics : D { in power = p; out x; }"
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
                    childKind == ElementKind::ActionPerformance || childKind == ElementKind::ConstraintUsage || text)
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
            case ElementKind::ConnectionDefinition:
            case ElementKind::FlowDefinition:
                // its ends ("end [1] part bead : TireBead;", "end port fuelOut : FuelOutPort;"), the payload of a flow
                // ("ref :>> payload : Fuel;") and attributes
                if (childKind == ElementKind::PartUsage || childKind == ElementKind::PortUsage ||
                    childKind == ElementKind::ItemUsage || childKind == ElementKind::Attribute ||
                    childKind == ElementKind::ConstraintUsage || text)
                    return RuleResult::ok();
                return RuleResult::denied(toString(ownerKind) + " can only own its ends, items and attributes");
            case ElementKind::MetadataDefinition:
                if (childKind == ElementKind::Attribute || text)
                    return RuleResult::ok();
                return RuleResult::denied("A metadata definition can only own attributes");
            case ElementKind::ViewDefinition:
            case ElementKind::ViewUsage:
                // "expose vehicle::**;" only in a view usage
                if (childKind == ElementKind::ViewUsage || childKind == ElementKind::Render ||
                    childKind == ElementKind::Filter || childKind == ElementKind::Attribute || text ||
                    (childKind == ElementKind::Expose && ownerKind == ElementKind::ViewUsage))
                    return RuleResult::ok();
                return RuleResult::denied(ownerKind == ElementKind::ViewUsage
                                          ? "A view can only own views, exposes, renderings, filters and attributes"
                                          : "A view definition can only own views, renderings, filters and attributes");
            case ElementKind::Attribute:
            case ElementKind::EventOccurrence:
            case ElementKind::Subject:
            case ElementKind::Actor:
            case ElementKind::EnumerationLiteral:
            case ElementKind::ControlNode:
            case ElementKind::Textual:
            case ElementKind::MetadataUsage:
            case ElementKind::Import:
            case ElementKind::Alias:
            case ElementKind::Expose:
            case ElementKind::Render:
            case ElementKind::Filter:
                return RuleResult::denied(toString(ownerKind) + " can only own comments, metadata, imports and aliases");
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
        // unnamed elements cannot be referenced (the source of verify / include is the body they are written in):
        // every element on the way from the innermost common owner to an end needs a name (all of them for a
        // specialization, written with its qualified name). A succession within one body is written right after
        // its source ("then b;", or "then if c { ... }" in front of an unnamed target): no names needed.
        const auto pathSource = model.ownershipPath(source);
        const auto pathTarget = model.ownershipPath(target);
        std::size_t common = 0;
        if (kind != RelationshipKind::Specialization) {
            while (common + 1 < pathSource.size() && common + 1 < pathTarget.size() && pathSource[common] == pathTarget[common])
                ++common;
        }
        const auto named = [&](const std::vector<ElementId> &path) {
            for (std::size_t i = common; i < path.size(); ++i) {
                if (model.element(path[i]).name().empty())
                    return false;
            }
            return true;
        };
        const bool withinBody = kind == RelationshipKind::Succession &&
                                model.element(source).owner() == model.element(target).owner();
        if (!withinBody && (!named(pathTarget) || (!named(pathSource) && kind != RelationshipKind::Verify &&
                                                   kind != RelationshipKind::Include && kind != RelationshipKind::Frame)))
            return RuleResult::denied("An element without a name cannot be referenced");
        // "then if c { ... }": an unnamed target is declared right behind its source (and only there)
        if (withinBody && model.element(target).name().empty() && previousSuccessionEnd(model, target) != source)
            return RuleResult::denied("An unnamed action can only follow the action declared right before it");
        // the else branch of an if action is part of it (written behind its body)
        if (model.element(source).isElseBranch() || model.element(target).isElseBranch())
            return RuleResult::denied("The else branch of an if action cannot be connected");

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
                if (sourceKind != ElementKind::PartUsage && sourceKind != ElementKind::PortUsage && !isViewKind(sourceKind))
                    return RuleResult::denied("A requirement is satisfied by a part or port (usage), a viewpoint by a view");
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

            case RelationshipKind::Frame:
                if (!isRequirementKind(sourceKind) || targetKind != ElementKind::RequirementUsage)
                    return RuleResult::denied("A requirement (a viewpoint, ...) frames a concern (requirement usage)");
                if (model.isAncestor(source, target) || model.isAncestor(target, source))
                    return RuleResult::denied("A requirement cannot frame its own owner or a concern it owns");
                if (relationshipExists(model, kind, source, target, false))
                    return RuleResult::denied("Frame already exists");
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

    std::optional<ElementId> RelationshipRules::previousSuccessionEnd(const SemanticModel &model, ElementId id) {
        if (!model.contains(id))
            return std::nullopt;
        const auto &siblings = model.children(model.element(id).owner());
        auto position = std::find(siblings.begin(), siblings.end(), id);
        while (position != siblings.begin()) {
            --position;
            const auto &candidate = model.element(*position);
            if (candidate.kind() == ElementKind::ControlNode && candidate.declaredKeyword() == "done")
                continue; // never written
            if (candidate.isElseBranch())
                continue;
            if (isSuccessionEnd(candidate.kind()) ||
                ((candidate.kind() == ElementKind::PartUsage || candidate.kind() == ElementKind::ItemUsage) &&
                 isTemporalKeyword(candidate.declaredKeyword())))
                return *position;
        }
        return std::nullopt;
    }

    std::vector<RelationshipKind> RelationshipRules::allowedRelationshipKinds(const SemanticModel &model,
                                                                              ElementId source, ElementId target) {
        std::vector<RelationshipKind> result;
        for (const auto kind: {RelationshipKind::Connection, RelationshipKind::Flow, RelationshipKind::Specialization,
                               RelationshipKind::Binding, RelationshipKind::Interface, RelationshipKind::Succession,
                               RelationshipKind::Transition, RelationshipKind::Satisfy, RelationshipKind::Verify,
                               RelationshipKind::Include, RelationshipKind::Message, RelationshipKind::Allocation,
                               RelationshipKind::Dependency, RelationshipKind::Frame}) {
            if (canRelate(model, kind, source, target))
                result.push_back(kind);
        }
        return result;
    }
}
