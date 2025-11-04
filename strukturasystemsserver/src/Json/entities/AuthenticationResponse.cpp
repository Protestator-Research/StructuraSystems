#include "AuthenticationResponse.h"

#include <sysmlv2/rest/entities/JSONEntities.h>

namespace StructuraSystems::Server
{
	AuthenticationResponse::AuthenticationResponse(std::string barrierString)
	{
		BarrierString = barrierString;
	}

	QJsonObject AuthenticationResponse::toJson() const
	{
		return QJsonObject{
			{QString("barrierString"),QString::fromStdString(BarrierString)}
		};
	}
}
