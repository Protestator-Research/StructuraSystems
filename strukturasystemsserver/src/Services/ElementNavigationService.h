#pragma once

#include <map>
#include <sysmlv2/service/interfaces/IElementNavigationService.h>

namespace StructuraSystems::Server {
	class ElementNavigationService : public SysMLv2::API::IElementNavigationService
	{
	public:
		static std::shared_ptr<ElementNavigationService> getInstance();

		virtual ~ElementNavigationService() = default;

		std::vector<std::shared_ptr<KerML::Entities::Element>> getElements(std::shared_ptr<SysMLv2::REST::Project> project, std::shared_ptr<SysMLv2::REST::Commit> commit) override;

		std::shared_ptr<KerML::Entities::Element> getElementById(std::shared_ptr<SysMLv2::REST::Project> project, std::shared_ptr<SysMLv2::REST::Commit> commit, boost::uuids::uuid elementId) override;

		std::vector<std::shared_ptr<KerML::Entities::Relationship>> getRelationshipsByRelatedElement(std::shared_ptr<SysMLv2::REST::Project> project, std::shared_ptr<SysMLv2::REST::Commit> commit, boost::uuids::uuid elementId, int direction) override;

		std::vector<std::shared_ptr<KerML::Entities::Element>> getRootElements(std::shared_ptr<SysMLv2::REST::Project> project, std::shared_ptr<SysMLv2::REST::Commit> commit) override;

	private:
		ElementNavigationService() = default;

		static std::shared_ptr<ElementNavigationService> Instance;
	};
}