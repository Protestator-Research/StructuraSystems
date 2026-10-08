//
// Created by Moritz Herzog on 22.07.25.
//

#include "DigitalTwin.h"

namespace SysMLv2::REST{
    DigitalTwin::DigitalTwin(const std::string &jsonString) : Tag(jsonString) {
        Type = "Twin";
    }
} // SysMLv2::REST
