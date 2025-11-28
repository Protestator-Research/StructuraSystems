#pragma once

#include <QJsonDocument>
#include <boost/lexical_cast.hpp>

#include "BaseController.hpp"
#include "../Services/AuthenticationService.h"
#include "../Json/entities/AuthenticationRequest.h"
#include "../Json/entities/AuthenticationResponse.h"

namespace StructuraSystems::Server
{
	class AuthenticationController : public BaseController
	{
	public:
		AuthenticationController() = delete;

		AuthenticationController(QHttpServer* httpServer) : BaseController(httpServer)
		{
			AuthService = AuthenticationService::getInstance();
			generateRoutes();
		}

		~AuthenticationController() override = default;

	protected:
		void generateRoutes() override
		{
			HttpServer->route("/login", QHttpServerRequest::Method::Post, [this](const QHttpServerRequest& request)->QHttpServerResponse {return login(request); });
			HttpServer->route("/login/", QHttpServerRequest::Method::Post, [this](const QHttpServerRequest& request)->QHttpServerResponse {return login(request); });
		}

	private:

		QHttpServerResponse login(const QHttpServerRequest& request)
		{
			const auto payload = QJsonDocument::fromJson(request.body());
			try {
				auto authenticationID = AuthService->authenticateUserWith(payload["username"].toString().toStdString(), payload["password"].toString().toStdString());
				auto bearrierString = QString::fromStdString(boost::lexical_cast<std::string>(authenticationID)).toUtf8().toBase64();
				auto authenticationResponse = AuthenticationResponse(bearrierString.toStdString());
				auto response = QHttpServerResponse(authenticationResponse.toJson());
				auto header = response.headers();

				header.append(QHttpHeaders::WellKnownHeader::WWWAuthenticate, QAnyStringView("Bearrier " + QString(bearrierString)));
				response.setHeaders(std::move(header));

				return response;
			}
			catch (...) {
				return QHttpServerResponse("text/plain", "Authentication Required\n", QHttpServerResponder::StatusCode::Unauthorized);
			}
		}

		std::shared_ptr<AuthenticationService> AuthService;
	};
}