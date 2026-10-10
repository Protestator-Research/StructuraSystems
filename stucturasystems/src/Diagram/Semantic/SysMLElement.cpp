//
// Semantic layer of the diagram editor: one SysML v2 element without any layout information.
//

#include "SysMLElement.h"

#include <stdexcept>
#include <utility>

namespace StructuraSystems::Diagram {

    Multiplicity Multiplicity::exactly(std::size_t count) {
        return Multiplicity{count, count};
    }

    Multiplicity Multiplicity::range(std::size_t lower, std::optional<std::size_t> upper) {
        if (upper.has_value() && *upper < lower)
            throw std::invalid_argument("Multiplicity: upper bound is smaller than lower bound");
        return Multiplicity{lower, upper};
    }

    std::string Multiplicity::toString() const {
        if (!Upper.has_value()) {
            if (Lower == 0)
                return "[*]";
            return "[" + std::to_string(Lower) + "..*]";
        }
        if (*Upper == Lower)
            return "[" + std::to_string(Lower) + "]";
        return "[" + std::to_string(Lower) + ".." + std::to_string(*Upper) + "]";
    }

    SysMLElement::SysMLElement(ElementId id, ElementKind kind, std::string name, ElementId owner) :
            Id(id),
            Kind(kind),
            Name(std::move(name)),
            Owner(owner) {
    }

    std::string toString(ElementKind kind) {
        switch (kind) {
            case ElementKind::Package:
                return "Package";
            case ElementKind::PartDefinition:
                return "PartDefinition";
            case ElementKind::PartUsage:
                return "PartUsage";
            case ElementKind::Attribute:
                return "Attribute";
            case ElementKind::ActionPerformance:
                return "ActionPerformance";
            case ElementKind::ConstraintDefinition:
                return "ConstraintDefinition";
            case ElementKind::CalcDefinition:
                return "CalcDefinition";
            case ElementKind::ConstraintUsage:
                return "ConstraintUsage";
            case ElementKind::CalcUsage:
                return "CalcUsage";
            case ElementKind::AttributeDefinition:
                return "AttributeDefinition";
            case ElementKind::EnumerationDefinition:
                return "EnumerationDefinition";
            case ElementKind::EnumerationLiteral:
                return "EnumerationLiteral";
            case ElementKind::ItemDefinition:
                return "ItemDefinition";
            case ElementKind::ItemUsage:
                return "ItemUsage";
            case ElementKind::PortDefinition:
                return "PortDefinition";
            case ElementKind::PortUsage:
                return "PortUsage";
            case ElementKind::InterfaceDefinition:
                return "InterfaceDefinition";
            case ElementKind::ActionDefinition:
                return "ActionDefinition";
            case ElementKind::ActionUsage:
                return "ActionUsage";
            case ElementKind::ControlNode:
                return "ControlNode";
            case ElementKind::StateDefinition:
                return "StateDefinition";
            case ElementKind::StateUsage:
                return "StateUsage";
            case ElementKind::RequirementDefinition:
                return "RequirementDefinition";
            case ElementKind::RequirementUsage:
                return "RequirementUsage";
            case ElementKind::Subject:
                return "Subject";
            case ElementKind::Actor:
                return "Actor";
            case ElementKind::UseCaseDefinition:
                return "UseCaseDefinition";
            case ElementKind::UseCaseUsage:
                return "UseCaseUsage";
            case ElementKind::VerificationDefinition:
                return "VerificationDefinition";
            case ElementKind::VerificationUsage:
                return "VerificationUsage";
            case ElementKind::OccurrenceDefinition:
                return "OccurrenceDefinition";
            case ElementKind::OccurrenceUsage:
                return "OccurrenceUsage";
            case ElementKind::EventOccurrence:
                return "EventOccurrence";
            case ElementKind::AllocationDefinition:
                return "AllocationDefinition";
            case ElementKind::AnalysisDefinition:
                return "AnalysisDefinition";
            case ElementKind::AnalysisUsage:
                return "AnalysisUsage";
            case ElementKind::Textual:
                return "Textual";
        }
        return "Unknown";
    }

    std::string defaultKeyword(ElementKind kind) {
        switch (kind) {
            case ElementKind::Package:
                return "package";
            case ElementKind::PartDefinition:
                return "part def";
            case ElementKind::PartUsage:
                return "part";
            case ElementKind::Attribute:
                return "attribute";
            case ElementKind::ActionPerformance:
                return "perform action";
            case ElementKind::ConstraintDefinition:
                return "constraint def";
            case ElementKind::CalcDefinition:
                return "calc def";
            case ElementKind::ConstraintUsage:
                return "constraint";
            case ElementKind::CalcUsage:
                return "calc";
            case ElementKind::AttributeDefinition:
                return "attribute def";
            case ElementKind::EnumerationDefinition:
                return "enum def";
            case ElementKind::EnumerationLiteral:
                return "enum";
            case ElementKind::ItemDefinition:
                return "item def";
            case ElementKind::ItemUsage:
                return "item";
            case ElementKind::PortDefinition:
                return "port def";
            case ElementKind::PortUsage:
                return "port";
            case ElementKind::InterfaceDefinition:
                return "interface def";
            case ElementKind::ActionDefinition:
                return "action def";
            case ElementKind::ActionUsage:
                return "action";
            case ElementKind::ControlNode:
                return "decide";
            case ElementKind::StateDefinition:
                return "state def";
            case ElementKind::StateUsage:
                return "state";
            case ElementKind::RequirementDefinition:
                return "requirement def";
            case ElementKind::RequirementUsage:
                return "requirement";
            case ElementKind::Subject:
                return "subject";
            case ElementKind::Actor:
                return "actor";
            case ElementKind::UseCaseDefinition:
                return "use case def";
            case ElementKind::UseCaseUsage:
                return "use case";
            case ElementKind::VerificationDefinition:
                return "verification def";
            case ElementKind::VerificationUsage:
                return "verification";
            case ElementKind::OccurrenceDefinition:
                return "occurrence def";
            case ElementKind::OccurrenceUsage:
                return "occurrence";
            case ElementKind::EventOccurrence:
                return "event occurrence";
            case ElementKind::AllocationDefinition:
                return "allocation def";
            case ElementKind::AnalysisDefinition:
                return "analysis def";
            case ElementKind::AnalysisUsage:
                return "analysis";
            case ElementKind::Textual:
                break;
        }
        return {};
    }

    std::string toString(RelationshipKind kind) {
        switch (kind) {
            case RelationshipKind::Connection:
                return "Connection";
            case RelationshipKind::Flow:
                return "Flow";
            case RelationshipKind::Specialization:
                return "Specialization";
            case RelationshipKind::Binding:
                return "Binding";
            case RelationshipKind::Interface:
                return "Interface";
            case RelationshipKind::Succession:
                return "Succession";
            case RelationshipKind::Transition:
                return "Transition";
            case RelationshipKind::Satisfy:
                return "Satisfy";
            case RelationshipKind::Verify:
                return "Verify";
            case RelationshipKind::Include:
                return "Include";
            case RelationshipKind::Message:
                return "Message";
            case RelationshipKind::Allocation:
                return "Allocation";
            case RelationshipKind::Dependency:
                return "Dependency";
        }
        return "Unknown";
    }
}
