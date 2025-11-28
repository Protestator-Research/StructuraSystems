#include "AuthenticationRequest.h"
#include <sysmlv2/rest/entities/JSONEntities.h>

namespace StructuraSystems::Server
{
	AuthenticationRequest::AuthenticationRequest()
	{

	}

	bool AuthenticationRequest::update(const QJsonObject& json)
	{
		Username = json["username"].toString().toStdString();
		Password = json["password"].toString().toStdString();
		return true;
	}

	void AuthenticationRequest::updateFields(const QJsonObject& json)
	{
		Username = json["username"].toString().toStdString();
		Password = json["password"].toString().toStdString();
	}

	QJsonObject AuthenticationRequest::toJson() const
	{
		return QJsonObject{
			{"username",QString::fromStdString(Username)},
			{"password", QString::fromStdString(Password)}
		};
	}

	std::string AuthenticationRequest::username() const
	{
		return Username;
	}

	std::string AuthenticationRequest::password() const
	{
		return Password;
	}
}
