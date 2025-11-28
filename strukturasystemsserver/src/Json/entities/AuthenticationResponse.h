#pragma once

#include "../Jsonable.hpp"
#include "../Updatable.hpp"
#include <string>

namespace StructuraSystems::Server
{
	class AuthenticationResponse : public Jsonable
	{
	public:
		AuthenticationResponse() = delete;
		AuthenticationResponse(std::string barrierString);
		~AuthenticationResponse() override = default;

		QJsonObject toJson() const override;

	private:
		std::string BarrierString;
	};
}