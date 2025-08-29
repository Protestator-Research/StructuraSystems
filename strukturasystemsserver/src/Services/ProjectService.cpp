#include "ProjectService.h"

#include <sysmlv2/rest/entities/Branch.h>
#include <sysmlv2/rest/entities/Project.h>

#include "../Controller/DataBaseController.h"<

namespace StructuraSystems::Server
{
	ProjectService::ProjectService()
	{
		DBController = DataBaseController::getInstance();
		Projects = DBController->getAllProjects();
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

		DBController->addProject(newProject);

		Projects = DBController->getAllProjects();

		return newProject;
	}

	std::shared_ptr<SysMLv2::REST::Project> ProjectService::updateProject(boost::uuids::uuid projectId, std::string projectName, std::string description, std::shared_ptr<SysMLv2::REST::Branch> branch)
	{
		std::shared_ptr<SysMLv2::REST::Project> activeProject = nullptr;
		for (const auto& project : Projects)
			if (project->getId() == projectId)
				activeProject = project;

		if (activeProject != nullptr)
		{
			activeProject->setName(projectName);
			activeProject->setDescription(description);
			activeProject->setDefaultBranch(branch);

			DBController->updateProject(activeProject);
		}

		return activeProject;
	}

	std::shared_ptr<SysMLv2::REST::Project> ProjectService::deleteProject(boost::uuids::uuid projectId)
	{
		std::shared_ptr<SysMLv2::REST::Project> activeProject = nullptr;
		for (const auto& project : Projects)
			if (project->getId() == projectId)
				activeProject = project;

		if (activeProject != nullptr)
		{
			DBController->deleteProject(activeProject);
		}

		return nullptr;
	}
}
