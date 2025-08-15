#pragma once

#include <sysmlv2/rest/entities/Project.h>
#include <boost/uuid/string_generator.hpp>
#include <QJsonObject>
#include <QJsonDocument>

#include "BaseController.hpp"
#include "../Services/ProjectService.h"

namespace StructuraSystems::Server
{
	class ProjectController : BaseController
	{
	public:
		ProjectController() = delete;
		ProjectController(QHttpServer* httpServer) : BaseController(httpServer)
		{
			generateRoutes();
			_ProjectService = new ProjectService();
		}

		~ProjectController() override = default;

	protected:
		void generateRoutes() override
		{
			HttpServer->route("/projects", QHttpServerRequest::Method::Get, [this]()
				{
					const auto& projects = _ProjectService->getProjects();
					std::string returnValue = "[\r\n";
					for (int i = 0; i < projects.size(); i++)
					{
						returnValue += projects[i]->serializeToJson();
						if (i < (projects.size() - 1))
							returnValue += ",\r\n";
					}
					returnValue += "]";
					return QString::fromStdString(returnValue);
				}
			);

			HttpServer->route("/projects/<arg>", QHttpServerRequest::Method::Get, [this](const QString projectId)
				{
					const auto stringValue = _ProjectService->getProjectById(boost::uuids::string_generator()(projectId.toStdString()))->serializeToJson();
					return QString::fromStdString(stringValue);
				}
			);

			HttpServer->route("/projects/<arg>", QHttpServerRequest::Method::Delete, [this](const QString projectId)
				{
					const auto stringValue = _ProjectService->deleteProject(boost::uuids::string_generator()(projectId.toStdString()))->serializeToJson();
					return QString::fromStdString(stringValue);
				}
			);

			HttpServer->route("/projects", QHttpServerRequest::Method::Post, [this](const QHttpServerRequest& request)
				{
					auto jsonDocument = QJsonDocument::fromJson(request.body());
					QJsonObject requestObject = jsonDocument.object();
					const auto newProject = _ProjectService->createProject(requestObject["name"].toString().toStdString(), requestObject["description"].toString().toStdString());

					return QHttpServerResponse(QString::fromStdString(newProject->serializeToJson()), QHttpServerResponse::StatusCode::Created);
				}
			);
		}

	private:
		ProjectService* _ProjectService;

	};
}
