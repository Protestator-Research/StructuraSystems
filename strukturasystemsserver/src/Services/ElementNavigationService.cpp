#include "ElementNavigationService.h"

namespace StructuraSystems::Server
{
	ElementNavigationService::ElementNavigationService()
	{

	}

	std::vector<std::shared_ptr<SysMLv2::REST::Element>> ElementNavigationService::getElements(std::shared_ptr<SysMLv2::REST::Project> project, std::shared_ptr<SysMLv2::REST::Commit> commit)
	{

	}

	std::shared_ptr<SysMLv2::REST::Element> ElementNavigationService::getElementById(std::shared_ptr<SysMLv2::REST::Project> project, std::shared_ptr<SysMLv2::REST::Commit> commit, boost::uuids::uuid elementId)
	{

	}

	std::vector<std::shared_ptr<SysMLv2::REST::Relationship>> ElementNavigationService::getRelationshipsByRelatedElement(std::shared_ptr<SysMLv2::REST::Project> project, std::shared_ptr<SysMLv2::REST::Commit> commit, boost::uuids::uuid elementId, int direction)
	{

	}

	std::vector<std::shared_ptr<SysMLv2::REST::Element>> ElementNavigationService::getRootElements(std::shared_ptr<SysMLv2::REST::Project> project, std::shared_ptr<SysMLv2::REST::Commit> commit)
	{

	}
}
