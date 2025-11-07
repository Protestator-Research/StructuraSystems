#include "ElementNavigationService.h"

#include <qmetatype.h>
#include <sysmlv2/rest/entities/Commit.h>
#include <sysmlv2/rest/entities/Data.h>
#include <sysmlv2/rest/entities/DataVersion.h>
#include <sysmlv2/rest/entities/Project.h>
#include <sysmlv2/rest/entities/Element.h>

namespace StructuraSystems::Server
{
	std::shared_ptr<ElementNavigationService> ElementNavigationService::Instance = nullptr;


	std::shared_ptr<ElementNavigationService> ElementNavigationService::getInstance()
	{
		if (Instance == nullptr) {
			struct concreteElementService : public  ElementNavigationService {};
			Instance = std::make_shared<concreteElementService>();
		}

		return Instance;
	}

	std::vector<std::shared_ptr<SysMLv2::REST::Element>> ElementNavigationService::getElements(std::shared_ptr<SysMLv2::REST::Project> , std::shared_ptr<SysMLv2::REST::Commit> commit)
	{
		//Todo check
		std::vector<std::shared_ptr<SysMLv2::REST::Element>> elements;
		for (const auto& version : commit->getDataVersion()) {
			const auto &payload = version->getPayload();
			if (std::dynamic_pointer_cast<SysMLv2::REST::Element>(payload).use_count() > 0) {
				elements.push_back(std::dynamic_pointer_cast<SysMLv2::REST::Element>(payload));
			}
		}
		return elements;
	}

	std::shared_ptr<SysMLv2::REST::Element> ElementNavigationService::getElementById(std::shared_ptr<SysMLv2::REST::Project>, std::shared_ptr<SysMLv2::REST::Commit> commit, boost::uuids::uuid elementId){
		for (const auto& version : commit->getDataVersion()) {
			const auto &payload = version->getPayload();
			if ((std::dynamic_pointer_cast<SysMLv2::REST::Element>(payload).use_count() > 0)&&(payload->getId()==elementId)) {
				return std::dynamic_pointer_cast<SysMLv2::REST::Element>(payload);
			}
		}
		return nullptr;
	}

	std::vector<std::shared_ptr<SysMLv2::REST::Relationship>> ElementNavigationService::getRelationshipsByRelatedElement(std::shared_ptr<SysMLv2::REST::Project> , std::shared_ptr<SysMLv2::REST::Commit> , boost::uuids::uuid , int )
	{
		return std::vector<std::shared_ptr<SysMLv2::REST::Relationship>>();
	}

	std::vector<std::shared_ptr<SysMLv2::REST::Element>> ElementNavigationService::getRootElements(std::shared_ptr<SysMLv2::REST::Project> , std::shared_ptr<SysMLv2::REST::Commit> )
	{
		return std::vector<std::shared_ptr<SysMLv2::REST::Element>>();
	}
}
