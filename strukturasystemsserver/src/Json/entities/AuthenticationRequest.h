#pragma once

#include "../Updatable.hpp"
#include "../Jsonable.hpp"

namespace StructuraSystems::Server
{
	class AuthenticationRequest : public Updatable, public Jsonable
	{
	public:
		AuthenticationRequest();
		~AuthenticationRequest() override = default;

		bool update(const QJsonObject& json) override;
		void updateFields(const QJsonObject& json) override;
		QJsonObject toJson() const override;

		std::string username() const;
		std::string password() const;
	private:
		std::string Username;
		std::string Password;
	};
}
