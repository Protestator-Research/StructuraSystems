#pragma once

#include <sysmlv2/service/interfaces/IElementNavigationService.h>

namespace StructuraSystems::Server {
	class ElementNavigationService : public SysMLv2::API::IElementNavigationService
	{
	public:
		ElementNavigationService();
		virtual ~ElementNavigationService() = default;

		std::vector<std::shared_ptr<SysMLv2::REST::Element>> getElements(std::shared_ptr<SysMLv2::REST::Project> project, std::shared_ptr<SysMLv2::REST::Commit> commit) override;

		std::shared_ptr<SysMLv2::REST::Element> getElementById(std::shared_ptr<SysMLv2::REST::Project> project, std::shared_ptr<SysMLv2::REST::Commit> commit, boost::uuids::uuid elementId) override;

		std::vector<std::shared_ptr<SysMLv2::REST::Relationship>> getRelationshipsByRelatedElement(std::shared_ptr<SysMLv2::REST::Project> project, std::shared_ptr<SysMLv2::REST::Commit> commit, boost::uuids::uuid elementId, int direction) override;

		std::vector<std::shared_ptr<SysMLv2::REST::Element>> getRootElements(std::shared_ptr<SysMLv2::REST::Project> project, std::shared_ptr<SysMLv2::REST::Commit> commit) override;

	private:

	};
}