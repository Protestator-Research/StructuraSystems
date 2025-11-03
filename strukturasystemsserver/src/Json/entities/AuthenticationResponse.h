#pragma once

#include "../Jsonable.hpp"
#include "../Updatable.hpp"

namespace StructuraSystems::Server
{
	class AuthenticationResponse : public Jsonable
	{
	public:
		AuthenticationResponse();
		~AuthenticationResponse() override = default;

		QJsonObject toJson() const override;

	private:
		std::string BarrierString;
	};
}