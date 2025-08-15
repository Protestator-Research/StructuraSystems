#pragma once

#include <QJsonObject>

namespace StructuraSystems::Server
{
	struct Jsonable
	{
		virtual QJsonObject toJson() const = 0;
		virtual ~Jsonable() = default;
	};
}
