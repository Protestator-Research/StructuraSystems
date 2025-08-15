#pragma once

#include <QHttpServer>
#include <QString>
#include <functional>
#include "../Templates/CrudApi.hpp"
#include "../Templates/SessionAPI.hpp"


namespace StructuraSystems::Server
{
	class BaseController
	{
	public:
		BaseController() = delete;

		BaseController(QHttpServer* httpServer)
		{
			HttpServer = httpServer;
		}

		virtual ~BaseController() = default;

	protected:
		virtual void generateRoutes() = 0; 

		QHttpServer* HttpServer;
	};
}