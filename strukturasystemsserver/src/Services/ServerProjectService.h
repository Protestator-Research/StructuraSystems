#pragma once

#include <sysmlv2/service/implementation/ProjectService.h>

namespace StructuraSystems::Server
{
	//class DataBaseController;

	class ServerProjectService : public SysMLv2::API::ProjectService
	{
	public:
		ServerProjectService();
		~ServerProjectService() override = default;

	private:
		//DataBaseController* DBController;

	};
}