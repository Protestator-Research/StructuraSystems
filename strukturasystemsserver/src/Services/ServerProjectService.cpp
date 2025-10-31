#include "ServerProjectService.h"

#include "../Controller/DataBaseController.h"

namespace StructuraSystems::Server
{
	ServerProjectService::ServerProjectService() : ProjectService()
	{
		//DBController = DataBaseController::getInstance();
	}
}
