#pragma once

#include <sysmlv2/service/implementation/ProjectService.h>

namespace StructuraSystems::Server
{
	//class DataBaseController;

	class ServerProjectService : public SysMLv2::API::ProjectService
	{
	public:
		static std::shared_ptr<ServerProjectService> getInstance();
		~ServerProjectService() override = default;

	private:
		ServerProjectService();
		static std::shared_ptr<ServerProjectService> Instance;

		//DataBaseController* DBController;

	};
}