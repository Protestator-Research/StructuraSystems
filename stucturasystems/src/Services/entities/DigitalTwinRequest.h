//
// Created by Moritz Herzog on 08.10.26.
//

#ifndef DIGITALTWINREQUEST_H
#define DIGITALTWINREQUEST_H

#include <memory>
#include <string>
#include <boost/uuid/uuid.hpp>
#include <sysmlv2/rest/entities/IEntity.h>

namespace SysMLv2::REST {
    class Commit;

    /**
     * Request body for creating a digital twin on the Structura Systems server (POST /projects/{projectId}/twins).
     * Mirrors the TwinRequest of the server API: a name and the referenced commit, identified by its id.
     * @class DigitalTwinRequest
     * @author Moritz Herzog <herzogm@rptu.de>
     * @version 1.0
     */
    class DigitalTwinRequest : public IEntity {
    public:
        DigitalTwinRequest() = delete;

        /**
         * @param name Name of the digital twin.
         * @param referencedCommitId Id of the commit the digital twin is created from.
         */
        DigitalTwinRequest(std::string name, boost::uuids::uuid referencedCommitId);

        /**
         * Parses a request from its JSON representation.
         * @param jsonString The given string.
         */
        explicit DigitalTwinRequest(const std::string &jsonString);

        ~DigitalTwinRequest() override = default;

        std::string serializeToJson() override;

        [[nodiscard]] std::string name() const;
        [[nodiscard]] boost::uuids::uuid referencedCommitId() const;

    protected:
        void deserializeAndPopulate(const std::string &jsonString) override;

    private:
        std::string Name;
        std::string Type = "TwinRequest";
        boost::uuids::uuid ReferencedCommitId{};
    };
} // SysMLv2::REST

#endif //DIGITALTWINREQUEST_H
