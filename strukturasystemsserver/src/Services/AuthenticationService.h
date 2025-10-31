
#pragma once

#include <string>
#include <map>
#include <boost/uuid/uuid.hpp>

namespace StructuraSystems::Server
{
	class AuthenticationService
	{
	public:
		AuthenticationService();

		boost::uuids::uuid authenticateUserWith(std::string name, std::string password);

		void addUser(std::string username, std::string password);

	private:
		std::string hashSHA256From(std::string value);

		std::map<std::string, std::string> UsernameHashMap;
		std::map<std::string, boost::uuids::uuid> UsernameInstanceMap;
	};
}