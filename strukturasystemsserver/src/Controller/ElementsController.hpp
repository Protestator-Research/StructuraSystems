#pragma once

#include <boost/lexical_cast.hpp>
#include <boost/uuid/string_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <kerml/root/elements/Element.h>

#include "BaseController.hpp"
#include "../Services/ProjectVersioningService.h"
#include "../Services/ServerProjectService.h"
#include "../Services/ElementNavigationService.h"


namespace StructuraSystems::Server
{
	class ElementsController : public BaseController
	{
	public:
		ElementsController() = delete;

		ElementsController(QHttpServer* httpServer) : BaseController(httpServer)
		{
			ElementNavService = ElementNavigationService::getInstance();
			ProjectService = ServerProjectService::getInstance();
			ProjectVerService = ProjectVersioningService::getInstance();
			generateRoutes();
		}
	protected:
		void generateRoutes() override
		{
			HttpServer->route("/projects/<arg>/commits/<arg>/elements", QHttpServerRequest::Method::Get, [this](QString projectId, QString commitId, const QHttpServerRequest& request)->QHttpServerResponse {return getElements(projectId, commitId, request); });
			HttpServer->route("/projects/<arg>/commits/<arg>/elements/", QHttpServerRequest::Method::Get, [this](QString projectId, QString commitId, const QHttpServerRequest& request)->QHttpServerResponse {return getElements(projectId, commitId, request); });

			HttpServer->route("/projects/<arg>/commits/<arg>/elements/<arg>", QHttpServerRequest::Method::Get, [this](QString projectId, QString commitId, QString elementId, const QHttpServerRequest& request)->QHttpServerResponse {return getElementWithId(projectId, commitId, elementId, request); });
			HttpServer->route("/projects/<arg>/commits/<arg>/elements/<arg>/", QHttpServerRequest::Method::Get, [this](QString projectId, QString commitId, QString elementId, const QHttpServerRequest& request)->QHttpServerResponse {return getElementWithId(projectId, commitId, elementId, request); });

			HttpServer->route("/projects/<arg>/commits/<arg>/elements/<arg>/relationships", QHttpServerRequest::Method::Get, [this](QString projectId, QString commitId, QString releatedElementId, const QHttpServerRequest& request)->QHttpServerResponse {return getRelationshipsByRelatedElement(projectId, commitId, releatedElementId, request); });
			HttpServer->route("/projects/<arg>/commits/<arg>/elements/<arg>/relationships/", QHttpServerRequest::Method::Get, [this](QString projectId, QString commitId, QString releatedElementId, const QHttpServerRequest& request)->QHttpServerResponse {return getRelationshipsByRelatedElement(projectId, commitId, releatedElementId, request); });

			HttpServer->route("/projects/<arg>/commits/<arg>/root", QHttpServerRequest::Method::Get, [this](QString projectId, QString commitId, const QHttpServerRequest& request)->QHttpServerResponse {return getRootElements(projectId, commitId, request); });
			HttpServer->route("/projects/<arg>/commits/<arg>/root/", QHttpServerRequest::Method::Get, [this](QString projectId, QString commitId, const QHttpServerRequest& request)->QHttpServerResponse {return getRootElements(projectId, commitId, request); });
		}

	private:
		QHttpServerResponse getElements(const QString& projectId, const QString& commitId,const QHttpServerRequest& )
		{
			auto string_generator = boost::uuids::string_generator();
			const auto project = ProjectService->getProjectById(string_generator(projectId.toStdString()));
			const auto commit = ProjectVerService->getCommitById(project, string_generator(commitId.toStdString()));
			const auto elements = ElementNavService->getElements(project, commit);
			
			std::string returnValue = "[\r\n";
			for (size_t i = 0; i < elements.size(); i++)
			{
				returnValue += elements[i]->serializeToJson();
				if (i < (elements.size() - 1))
					returnValue += ",\r\n";
			}
			returnValue += "]";

			return QHttpServerResponse(QString::fromStdString(returnValue));
		}

		QHttpServerResponse getElementWithId(QString projectId, QString commitId, QString elementId, const QHttpServerRequest&)
		{
			auto string_generator = boost::uuids::string_generator();

			const auto project = ProjectService->getProjectById(string_generator(projectId.toStdString()));
			const auto commit = ProjectVerService->getCommitById(project, string_generator(commitId.toStdString()));
			const auto element = ElementNavService->getElementById(project,commit,boost::uuids::string_generator()(elementId.toStdString()));

			return QHttpServerResponse(QString::fromStdString(element->serializeToJson()));
		}

		QHttpServerResponse getRelationshipsByRelatedElement(QString , QString , QString , const QHttpServerRequest& )
		{
			//TODO Not yet implemented.
			return QHttpServerResponse("TODO Not yet implemented", QHttpServerResponder::StatusCode::NotImplemented);
		}

		QHttpServerResponse getRootElements(QString , QString , const QHttpServerRequest& )
		{
			//TODO Not yet implemented.
			return QHttpServerResponse("TODO Not yet implemented", QHttpServerResponder::StatusCode::NotImplemented);
		}

		std::shared_ptr<ElementNavigationService> ElementNavService;
		std::shared_ptr<ServerProjectService> ProjectService;
		std::shared_ptr<ProjectVersioningService> ProjectVerService;
	};
}