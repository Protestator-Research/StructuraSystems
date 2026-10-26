//---------------------------------------------------------
// External Classes
//---------------------------------------------------------
#include <algorithm>
#include <map>
#include <set>
#include <boost/uuid/uuid.hpp>
#include <nlohmann/json.hpp>
#include <kerml/root/elements/Element.h>
#include <sysmlv2/rest/entities/CommitRequest.h>
#include <sysmlv2/rest/entities/DataIdentity.h>
#include <sysmlv2/rest/entities/DataVersion.h>
#include <sysmlv2/rest/entities/JSONEntities.h>
#include <sysmlv2/rest/serialization/SysMLv2Deserializer.h>

//---------------------------------------------------------
// Internal Classes
//---------------------------------------------------------
#include "CommitDelta.h"

namespace StructuraSystems::Client {
    namespace {
        bool isTextualRepresentation(const std::shared_ptr<KerML::Entities::Element> &element) {
            auto type = element->getType();
            std::transform(type.begin(), type.end(), type.begin(), ::tolower);
            return type == SysMLv2::REST::TEXTUAL_REPRESENTATION_TYPE;
        }

        /**
         * The serialization as JSON value, so that the order of the keys and the formatting do not matter. The text is read
         * and written once more by the deserializer of the sysmllib, the way the elements from the server were created, so
         * that an element of the document and the same element from the server have the same form.
         */
        nlohmann::json serialized(const std::shared_ptr<KerML::Entities::Element> &element) {
            std::string text = element->serializeToJson();
            try {
                if (const auto reread = std::dynamic_pointer_cast<KerML::Entities::Element>(SysMLv2::SysMLv2Deserializer::deserializeJsonString(text)))
                    text = reread->serializeToJson();
            } catch (const std::exception &) {
                // The text of the element itself is compared.
            }
            auto json = nlohmann::json::parse(text, nullptr, false);
            // An element that cannot be serialized is treated as changed, so that the server can reject it.
            return json.is_discarded() ? nlohmann::json(nullptr) : json;
        }
    }

    CommitDelta computeCommitDelta(const std::vector<std::shared_ptr<KerML::Entities::Element>> &base,
                                   const std::vector<std::shared_ptr<KerML::Entities::Element>> &document) {
        CommitDelta delta;

        std::map<boost::uuids::uuid, std::shared_ptr<KerML::Entities::Element>> baseById;
        for (const auto &element : base)
            if (element != nullptr)
                baseById[element->getId()] = element;

        std::set<boost::uuids::uuid> inDocument;
        std::vector<std::shared_ptr<SysMLv2::REST::DataVersion>> updates;
        for (const auto &element : document) {
            if (element == nullptr || !inDocument.insert(element->getId()).second)
                continue;

            const auto baseElement = baseById.find(element->getId());
            if (baseElement == baseById.end()) {
                delta.Changes.push_back(std::make_shared<SysMLv2::REST::DataVersion>(element));
                ++delta.Created;
            } else if (const auto current = serialized(element); current.is_null() || serialized(baseElement->second) != current) {
                auto identity = std::make_shared<SysMLv2::REST::DataIdentity>(element->getId());
                updates.push_back(std::make_shared<SysMLv2::REST::DataVersion>(element, identity));
                ++delta.Updated;
            }
        }
        delta.Changes.insert(delta.Changes.end(), updates.begin(), updates.end());

        // Deletions in the order of the base.
        for (const auto &element : base) {
            if (element == nullptr || inDocument.count(element->getId()) != 0 || !isTextualRepresentation(element))
                continue;
            auto identity = std::make_shared<SysMLv2::REST::DataIdentity>(element->getId());
            delta.Changes.push_back(std::make_shared<SysMLv2::REST::DataVersion>(nullptr, identity));
            ++delta.Deleted;
        }
        return delta;
    }

    std::shared_ptr<SysMLv2::REST::CommitRequest> buildCommitRequest(const std::string &description, const CommitDelta &delta) {
        return std::make_shared<SysMLv2::REST::CommitRequest>(description, delta.Changes);
    }

    std::string commitEndpoint(const std::string &projectId, const std::string &branchId) {
        return "projects/" + projectId + "/commits?branchId=" + branchId;
    }
}
