#include "ServerProjectService.h"

#include "../Controller/DataBaseController.h"
#include <sysmlv2/rest/entities/Project.h>

namespace StructuraSystems::Server
{
	std::shared_ptr<ServerProjectService> ServerProjectService::Instance = nullptr;
	std::shared_ptr<ServerProjectService> ServerProjectService::getInstance()
	{
		if (Instance == nullptr) {
			struct concreteServerProjectService : public ServerProjectService {};
			Instance = std::make_shared<concreteServerProjectService>();
		}

		return Instance;
	}

	ServerProjectService::ServerProjectService() : ProjectService()
	{
		DBController = DataBaseController::getInstance();
		const auto projects = DBController->getAllProjects();
		for (const auto project : projects)
		{
			ProjectMap.insert(std::make_pair(project->getId(), project));
		}
	}
}
