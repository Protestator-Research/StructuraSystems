#include "AuthenticationResponse.h"

#include <sysmlv2/rest/entities/JSONEntities.h>

namespace StructuraSystems::Server
{
	AuthenticationResponse::AuthenticationResponse()
	{

	}

	QJsonObject AuthenticationResponse::toJson() const
	{
		return QJsonObject{
			{QString("barrierString"),""}
		};
	}
}
