
#pragma once

#include <string>
#include <map>
#include <boost/uuid/uuid.hpp>

namespace StructuraSystems::Server
{
	class DataBaseController;

	class AuthenticationService
	{
	public:
		virtual ~AuthenticationService() = default;

		static std::shared_ptr<AuthenticationService> getInstance();

		boost::uuids::uuid authenticateUserWith(std::string name, std::string password);

		void addUser(std::string username, std::string password);

	private:
		static std::shared_ptr<AuthenticationService> Instance;

		AuthenticationService();
		void readUserFromDatabase();

		std::map<std::string, std::string> UsernameHashMap;
		std::map<std::string, boost::uuids::uuid> UsernameInstanceMap;

		DataBaseController* DBController;
	};

}