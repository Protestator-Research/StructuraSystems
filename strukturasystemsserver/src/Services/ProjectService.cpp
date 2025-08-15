#include "ProjectService.h"

#include <sysmlv2/rest/entities/Branch.h>
#include <sysmlv2/rest/entities/Project.h>


namespace StructuraSystems::Server
{
	ProjectService::ProjectService()
	{
		
	}

	std::vector<std::shared_ptr<SysMLv2::REST::Project>> ProjectService::getProjects()
	{
		return Projects;
	}

	std::shared_ptr<SysMLv2::REST::Project> ProjectService::getProjectById(boost::uuids::uuid projectId)
	{
		for (const auto& project : Projects)
			if (project->getId() == projectId)
				return project;

		return nullptr;
	}

	std::shared_ptr<SysMLv2::REST::Project> ProjectService::createProject(std::string projectName, std::string description)
	{
		auto newProject = std::make_shared<SysMLv2::REST::Project>(projectName);
		newProject->setDefaultBranch(std::make_shared<SysMLv2::REST::Branch>("Main"));
		newProject->setDescription(description);
		Projects.push_back(newProject);
		return newProject;
	}

	std::shared_ptr<SysMLv2::REST::Project> ProjectService::updateProject(boost::uuids::uuid projectId, std::string projectName, std::string description, std::shared_ptr<SysMLv2::REST::Branch> branch)
	{
		for (const auto& project : Projects)
			if (project->getId() == projectId)
				return project;

		return nullptr;
	}

	std::shared_ptr<SysMLv2::REST::Project> ProjectService::deleteProject(boost::uuids::uuid projectId)
	{
		for (const auto& project : Projects)
			if (project->getId() == projectId)
				return project;

		return nullptr;
	}
}
