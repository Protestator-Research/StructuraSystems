//
// Created by Moritz Herzog on 23.04.25.
//

#include "ErrorItem.h"

namespace StructuraSystems::Client {
    ErrorItem::ErrorItem(boost::uuids::uuid elementId, std::string projectName, ErrorType type,
                             std::string description) {
        ElementId = elementId;
        ProjectName = projectName;
        Type = type;
        Description = description;
    }

    boost::uuids::uuid ErrorItem::getElementID() {
        return ElementId;
    }


} // StructuraSystem::Client