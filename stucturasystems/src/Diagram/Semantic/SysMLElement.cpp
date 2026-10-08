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
            case ElementKind::Textual:
                return "Textual";
        }
        return "Unknown";
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
        }
        return "Unknown";
    }
}
