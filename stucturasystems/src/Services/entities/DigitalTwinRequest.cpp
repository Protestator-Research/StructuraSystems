//
// Created by Moritz Herzog on 08.10.26.
//

#include <utility>
#include <nlohmann/json.hpp>
#include <boost/uuid/string_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <sysmlv2/rest/entities/JSONEntities.h>

#include "DigitalTwinRequest.h"

namespace SysMLv2::REST {
    DigitalTwinRequest::DigitalTwinRequest(std::string name, boost::uuids::uuid referencedCommitId) {
        Name = std::move(name);
        ReferencedCommitId = referencedCommitId;
    }

    DigitalTwinRequest::DigitalTwinRequest(const std::string &jsonString) {
        DigitalTwinRequest::deserializeAndPopulate(jsonString);
    }

    std::string DigitalTwinRequest::serializeToJson() {
        nlohmann::json json;
        json[JSON_TYPE_ENTITY] = Type;
        json[JSON_NAME_ENTITY] = Name;
        json[JSON_REFERENCE_COMMIT][JSON_ID_ENTITY] = boost::uuids::to_string(ReferencedCommitId);
        return json.dump(JSON_INTENT);
    }

    std::string DigitalTwinRequest::name() const {
        return Name;
    }

    boost::uuids::uuid DigitalTwinRequest::referencedCommitId() const {
        return ReferencedCommitId;
    }

    void DigitalTwinRequest::deserializeAndPopulate(const std::string &jsonString) {
        const nlohmann::json json = nlohmann::json::parse(jsonString);
        Name = json.at(JSON_NAME_ENTITY).get<std::string>();
        ReferencedCommitId = boost::uuids::string_generator()(json.at(JSON_REFERENCE_COMMIT).at(JSON_ID_ENTITY).get<std::string>());
    }
} // SysMLv2::REST
