//
// Created by Moritz Herzog on 22.07.25.
//

#ifndef DIGITALTWIN_H
#define DIGITALTWIN_H

#include <string>
#include <sysmlv2/rest/entities/Tag.h>

namespace SysMLv2::REST {

    /**
        * Represents a digital twin as returned by the Structura Systems server (TwinResponse of the API).
        * A digital twin is a tag with the type "Twin", that references the commit of the project it is created from.
        * New digital twins are requested with DigitalTwinRequest.
        * @class DigitalTwin
        * @author Moritz Herzog <herzogm@rptu.de>
        * @version 1.0
        * @see Tag
        * @see DigitalTwinRequest
        */
    class DigitalTwin : public Tag {
    public:
        DigitalTwin() = delete;

        /**
         * Parses the digital twin from the JSON sent by the server.
         * @param jsonString The given string.
         */
        explicit DigitalTwin(const std::string &jsonString);

        ~DigitalTwin() override = default;
    };

} // SysMLv2::REST

#endif //DIGITALTWIN_H
