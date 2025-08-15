#pragma once

#include <sysmlv2/service/interfaces/IProjectService.h>

namespace StructuraSystems::Server
{
	class DataBaseController;

	class ProjectService : public SysMLv2::API::IProjectService
	{
	public:
		ProjectService();
		virtual ~ProjectService() = default;

		std::vector<std::shared_ptr<SysMLv2::REST::Project>> getProjects() override;

		std::shared_ptr<SysMLv2::REST::Project> getProjectById(boost::uuids::uuid projectId) override;

		std::shared_ptr<SysMLv2::REST::Project> createProject(std::string projectName, std::string description) override;

		std::shared_ptr<SysMLv2::REST::Project> updateProject(boost::uuids::uuid projectId, std::string projectName, std::string description, std::shared_ptr<SysMLv2::REST::Branch> branch) override;

		std::shared_ptr<SysMLv2::REST::Project> deleteProject(boost::uuids::uuid projectId) override;

	private:
		std::vector<std::shared_ptr<SysMLv2::REST::Project>> Projects;

		DataBaseController* DBController;

	};
}