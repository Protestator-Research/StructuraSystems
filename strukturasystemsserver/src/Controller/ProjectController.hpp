#pragma once

#include <sysmlv2/rest/entities/Project.h>
#include <boost/uuid/string_generator.hpp>
#include <QJsonObject>
#include <QJsonDocument>

#include "BaseController.hpp"
#include "../Services/ServerProjectService.h"

namespace StructuraSystems::Server
{
	class ProjectController : BaseController
	{
	public:
		ProjectController() = delete;
		ProjectController(QHttpServer* httpServer) : BaseController(httpServer)
		{
			generateRoutes();
			_ProjectService = ServerProjectService::getInstance();
		}

		~ProjectController() override = default;

	protected:
		void generateRoutes() override
		{
			HttpServer->route("/projects", QHttpServerRequest::Method::Get, [this](const QHttpServerRequest& request)->QHttpServerResponse {return getProjects(request); });
			HttpServer->route("/projects/", QHttpServerRequest::Method::Get, [this](const QHttpServerRequest& request)->QHttpServerResponse {return getProjects(request); });

			HttpServer->route("/projects/<arg>", QHttpServerRequest::Method::Get, [this](QString projectId, const QHttpServerRequest& request)->QHttpServerResponse {return getProjectWithId(projectId, request); });
			HttpServer->route("/projects/<arg>/", QHttpServerRequest::Method::Get, [this](QString projectId, const QHttpServerRequest& request)->QHttpServerResponse {return getProjectWithId(projectId, request); });

			HttpServer->route("/projects/<arg>", QHttpServerRequest::Method::Delete, [this](QString projectId, const QHttpServerRequest& request)->QHttpServerResponse {return deleteProjectWithId(projectId, request); });
			HttpServer->route("/projects/<arg>/", QHttpServerRequest::Method::Delete, [this](QString projectId, const QHttpServerRequest& request)->QHttpServerResponse {return deleteProjectWithId(projectId, request); });

			HttpServer->route("/projects", QHttpServerRequest::Method::Post, [this](const QHttpServerRequest& request)->QHttpServerResponse {return createProject(request); });
			HttpServer->route("/projects/", QHttpServerRequest::Method::Post, [this](const QHttpServerRequest& request)->QHttpServerResponse {return createProject(request); });
		}

	private:
		QHttpServerResponse getProjects(const QHttpServerRequest& ) const
		{
			const auto& projects = _ProjectService->getProjects();
			std::string returnValue = "[\r\n";
			for (size_t i = 0; i < projects.size(); i++)
			{
				returnValue += projects[i]->serializeToJson();
				if (i < (projects.size() - 1))
					returnValue += ",\r\n";
			}
			returnValue += "]";
			QHttpServerResponse response(QString::fromStdString(returnValue));
			return response;
		}

		QHttpServerResponse getProjectWithId(const QString& projectId, const QHttpServerRequest& ) const
		{
			const auto stringValue = _ProjectService->getProjectById(boost::uuids::string_generator()(projectId.toStdString()))->serializeToJson();
			QHttpServerResponse response(QString::fromStdString(stringValue));
			return response;
		}

		QHttpServerResponse deleteProjectWithId(const QString& projectId, const QHttpServerRequest&) const
		{
			const auto stringValue = _ProjectService->deleteProject(boost::uuids::string_generator()(projectId.toStdString()))->serializeToJson();
			QHttpServerResponse response(QString::fromStdString(stringValue));
			return response;
		}

		QHttpServerResponse createProject(const QHttpServerRequest& request) const
		{
			auto jsonDocument = QJsonDocument::fromJson(request.body());
			QJsonObject requestObject = jsonDocument.object();
			const auto newProject = _ProjectService->createProject(requestObject["name"].toString().toStdString(), requestObject["description"].toString().toStdString());

			return QHttpServerResponse(QString::fromStdString(newProject->serializeToJson()), QHttpServerResponse::StatusCode::Created);
		}

		std::shared_ptr<ServerProjectService> _ProjectService;

	};
}
