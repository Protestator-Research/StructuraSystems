#pragma once

#include "BaseController.hpp"

namespace StructuraSystems::Server
{
	class AuthenticationController : public BaseController
	{
	public:
		AuthenticationController() = delete;
		AuthenticationController(QHttpServer* httpServer) : BaseController(httpServer)
		{
			generateRoutes();
		}

	protected:
		void generateRoutes() override
		{
			HttpServer->
		}
	private:
		
	};
}