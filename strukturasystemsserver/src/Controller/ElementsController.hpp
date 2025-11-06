#pragma once

#include <boost/lexical_cast.hpp>
#include <boost/uuid/string_generator.hpp>
#include <boost/uuid/uuid_io.hpp>

#include "BaseController.hpp"
#include "../Services/ElementNavigationService.h"


namespace StructuraSystems::Server
{
	class ElementsController : public BaseController
	{
		ElementsController() = delete;

		ElementsController(QHttpServer* httpServer) : BaseController(httpServer)
		{
			ElementNavService = new ElementNavigationService();
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
			const auto elements = ElementNavService->getElements(string_generator(projectId.toStdString()), string_generator(commitId.toStdString()));
			


		}

		QHttpServerResponse getElementWithId(QString projectId, QString commitId, QString elementId, const QHttpServerRequest& request)
		{

		}

		QHttpServerResponse getRelationshipsByRelatedElement(QString projectId, QString commitId, QString relatedElementId, const QHttpServerRequest& request)
		{

		}

		QHttpServerResponse getRootElements(QString projectId, QString commitId, const QHttpServerRequest& request)
		{
			
		}

		ElementNavigationService* ElementNavService;
	};
}