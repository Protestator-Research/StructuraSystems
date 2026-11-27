/*
Implementing identifieing root package, navigating relationships, collecting textual representations, handleing exceptional cases without redundancy to DigitalTwinLib/src/Model/ModelBlocks.cpp 
Solving 'The model has no root package, the order of the blocks is not known. The order given by the server (sorted by UUID) is used'
*/

#include "DocumentStructure.h"

#include <set>
#include <map>
#include <iostream>
#include <boost/uuid/uuid.hpp>
#include <kerml/root/elements/Element.h>
#include <kerml/root/elements/Relationship.h>
#include <kerml/root/namespaces/Membership.h>
#include <kerml/root/annotations/TextualRepresentation.h>

#include <stdexcept>
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <nlohmann/json.hpp>
#include <sysml/model/ElementFactory.h>

namespace StructuraSystems::Client{
    // Reuse relationship resolution from SysMLLib
    namespace {
        // Only direct, unambiguous document memberships are reused.
        std::map<boost::uuids::uuid, boost::uuids::uuid> documentMembershipIds(
            const std::shared_ptr<KerML::Entities::Element>& root
        ) {
            using namespace KerML::Entities;
            std::map<boost::uuids::uuid, boost::uuids::uuid> ids;
            if (!root)
                return ids;
            for (const auto& relationship : root->ownedRelationship()) {
                if (!relationship || relationship->getType() != "OwningMembership")
                    continue;
                auto owned = relationship->ownedRelatedElement();
                if (owned.empty()) {
                    if (auto membership = std::dynamic_pointer_cast<Membership>(relationship))
                        owned.push_back(membership->memberElement());
                }
                if (owned.size() == 1 && std::dynamic_pointer_cast<TextualRepresentation>(owned.front()))
                    ids.emplace(owned.front()->getId(), relationship->getId());
            }
            return ids;
        }
        /**
         * @brief Reporting document structure warning.
         * @param warnings container for warnings (optional).
         * @param message Warning message.
         */
        void report(std::vector<std::string>* warnings, const std::string& message){
            if (warnings)
                warnings->push_back(message);
            else
                std::cerr<<message<<std::endl;
        }
    }
    std::vector<std::shared_ptr<KerML::Entities::TextualRepresentation>>orderedDocumentBlocks(const std::vector<std::shared_ptr<KerML::Entities::Element>>& elements, std::vector<std::string>*warnings){
        using namespace KerML::Entities;
        std::vector<std::shared_ptr<TextualRepresentation>> blocks;
        std::shared_ptr<Element> rootPackage;
        std::size_t rootCount = 0;

        // collecting all textual blocks and identifying the root package.
        for (const auto& element:elements){
            if (!element)
                continue;
            std::cerr << "[DocumentStructure]"
                    << " Type: " << element->getType()
                    << " | Has owner: " << bool(element->owner())
                    << " | Has owning relationship: "
                    << bool(element->owningRelationship())
                    << " | Owned relationships: "
                    << element->ownedRelationship().size()
                    << std::endl;
            if(auto block = std::dynamic_pointer_cast<TextualRepresentation>(element))
                blocks.push_back(block);
            // If no owning element / relationship for a root package
            if (element->getType()=="Package"&& !element->owner() && !element->owningRelationship()){
                ++rootCount;
            // Selecting package with lowest UUID
            if(!rootPackage || element->getId() < rootPackage->getId())
                rootPackage = element;
            }
        }
        // Prefer a package that actually owns textual document blocks.
        const auto documentRoot = findDocumentRootPackage(elements);
        if (documentRoot)
            rootPackage = documentRoot;
        // Without root package, preserve given block order.
        if (!rootPackage){
            report(warnings, "The model given from the server has no root package, the order of the blocks is not known. The order given by the server (by UUID) is used and a root package will be generated locally.");
            return blocks;
        }
        if (rootCount > 1)
            report(warnings, documentRoot
                ? "Multiple root packages found. The first document root by UUID is used."
                : "Multiple root packages found. The first one by UUID is used.");
        // When incomplete object structure.
        if (rootPackage->hasUnresolvedJsonReferences()){
            report(warnings, "The root package contains unresolved references. The given block order is used.");
            return blocks;
        }

        std::vector<std::shared_ptr<TextualRepresentation>> orderedBlocks;
        std::set<boost::uuids::uuid> remaining;

        // Tracking blocks not yet added.
        for (const auto& block:blocks)
            remaining.insert(block->getId());
        // Adding only document blocks that haven't been added before.
        const auto appendBlock = [&](const std::shared_ptr<Element>& element){
            if(auto block = std::dynamic_pointer_cast<TextualRepresentation>(element)){
                if (remaining.erase(block->getId()))
                    orderedBlocks.push_back(block);
            }
        };
        // SysMLLib preserves order of owned relationships. Retrieving document blocks.
        for (const auto& relationship : rootPackage->ownedRelationship()){
            if(!relationship)
                continue;
            if(relationship->hasUnresolvedJsonReferences()){
                report(warnings, "A relationship contains unresolved references. The given block order is used.");
                return blocks;
            }
            const auto owned = relationship->ownedRelatedElement();

            for (const auto& element:owned)
                appendBlock(element);
            // Fall back to memberElement, if no owned elements available.
            if (owned.empty()){
                if (auto membership = std::dynamic_pointer_cast<Membership>(relationship))
                    appendBlock(membership->memberElement());
            }
        }
        // Preserve blocks that aren't referenced by the root package.
        if (!remaining.empty()){
            report(warnings, "Some blocks are not referenced by the root package and are appended at the end.");
            for (const auto& block:blocks)
                appendBlock(block);
        }
        return orderedBlocks;

    }

    std::shared_ptr<KerML::Entities::Element> findDocumentRootPackage(
        const std::vector<std::shared_ptr<KerML::Entities::Element>>& elements
    ) {
        using namespace KerML::Entities;
        std::set<boost::uuids::uuid> blockIds;
        for (const auto& element : elements)
            if (std::dynamic_pointer_cast<TextualRepresentation>(element))
                blockIds.insert(element->getId());

        std::shared_ptr<Element> root;
        for (const auto& element : elements) {
            if (!element || element->getType() != "Package" || element->owner() ||
                element->owningRelationship() || element->hasUnresolvedJsonReferences())
                continue;
            for (const auto& entry : documentMembershipIds(element)) {
                if (blockIds.count(entry.first) && (!root || element->getId() < root->getId()))
                    root = element;
            }
        }
        return root;
    }

    std::vector<std::shared_ptr<KerML::Entities::Element>>
    createDocumentStructure(
        const std::string& packageName,
        const std::vector<std::shared_ptr<KerML::Entities::TextualRepresentation>>& blocks,
        const std::shared_ptr<KerML::Entities::Element>& previousRoot
    ) {
        using nlohmann::json;

        boost::uuids::random_generator generateUuid;

        const auto previousMemberships = documentMembershipIds(previousRoot);
        const auto packageId = boost::uuids::to_string(
            previousRoot ? previousRoot->getId() : generateUuid());

        const json packageReference = {{"@id", packageId}};

        // Store the membership references in document order.
        json relationships = json::array();

        // Reserve the first entry for the root package.
        std::vector<std::string> jsons(1);

        std::set<boost::uuids::uuid> blockIds;

        for (const auto& block : blocks) {

            // Prevent invalid or duplicate document blocks.
            if (!block || !blockIds.insert(block->getId()).second)
                throw std::invalid_argument("Invalid or duplicate document block.");

            // Preserve the existing UUID of the block.
            const auto blockId = boost::uuids::to_string(block->getId());

            // Reuse the membership of a surviving block.
            const auto previous = previousMemberships.find(block->getId());
            const auto membershipId = boost::uuids::to_string(
                previous == previousMemberships.end() ? generateUuid() : previous->second);

            const json blockReference = {{"@id", blockId}};
            const json membershipReference = {{"@id", membershipId}};

            // Register the membership in the original document order.
            relationships.push_back(membershipReference);

            // Create the owning membership between package and block.
            const json membership = {
                {"@type", "OwningMembership"},
                {"@id", membershipId},
                {"elementId", membershipId},
                {"source", json::array({packageReference})},
                {"target", json::array({blockReference})},
                {"owningRelatedElement", packageReference},
                {"ownedRelatedElement", json::array({blockReference})},
                {"memberElement", blockReference}
            };

            // Copy the block and assign its ownership references.
            // The original C++ object remains unchanged.
            auto blockJson = json::parse(block->serializeToJson());

            blockJson["owner"] = packageReference;
            blockJson["owningRelationship"] = membershipReference;

            jsons.push_back(membership.dump());
            jsons.push_back(blockJson.dump());
        }

        // Create the root package with its ordered memberships.
        const json package = {
            {"@type", "Package"},
            {"@id", packageId},
            {"elementId", packageId},
            {"declaredName", previousRoot ? previousRoot->declaredName().value_or(packageName) : packageName},
            {"ownedRelationship", relationships}
        };

        jsons.front() = package.dump();

        // Reuse SysMLLib to create typed elements, resolve references and rebuild their derived properties.
        return SysMLv2::Entities::createElementsFromJson(jsons);
    }
}