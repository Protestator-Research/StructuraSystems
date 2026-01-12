#include "AuthenticationService.h"

#include <tuple>
#include <iomanip>
#include <boost/uuid/nil_generator.hpp>
#include <boost/uuid/random_generator.hpp>

#include <sodium.h>

#include "../Controller/DataBaseController.h"

namespace StructuraSystems::Server {
	std::shared_ptr<AuthenticationService> AuthenticationService::Instance = nullptr;

	AuthenticationService::AuthenticationService() {
		DBController = DataBaseController::getInstance();
		readUserFromDatabase();

#ifndef NO_DEBUG
		if (UsernameHashMap.size()<=0)
			addUser("admin", "admin");
#endif 
	}

	void AuthenticationService::readUserFromDatabase()
	{
		const auto dbRead = DBController->getAllUser();
		for (const auto& element : dbRead)
		{
			const auto [username, securityString] = element;

			UsernameHashMap.insert(std::make_pair(username, securityString));
		}
	}

	std::shared_ptr<AuthenticationService> AuthenticationService::getInstance()
	{
		if (Instance == nullptr) {
			struct conreteAuthService : public AuthenticationService {};
			Instance = std::make_shared<conreteAuthService>();
		}

		return Instance;
	}

	boost::uuids::uuid AuthenticationService::authenticateUserWith(std::string name, std::string password) {

		if (!UsernameHashMap.contains(name))
			throw new std::runtime_error("Username or password wrong.");

		
		auto check_value = crypto_pwhash_str_verify(
			UsernameHashMap[name].c_str(),
			password.c_str(),
			password.size()
		);
		if (check_value==0)
		{
			if (UsernameInstanceMap[name]!=boost::uuids::nil_uuid())
			{
				return UsernameInstanceMap[name];
			}
			else
			{
				UsernameInstanceMap[name] = boost::uuids::random_generator()();
				return UsernameInstanceMap[name];
			}
		}
		throw new std::runtime_error("Username or password wrong.");
	}

	void AuthenticationService::addUser(std::string username, std::string password)
	{
		char securityString[crypto_pwhash_STRBYTES];
		if (crypto_pwhash_str(
			securityString,
			password.c_str(),
			password.size(),
			crypto_pwhash_OPSLIMIT_INTERACTIVE,
			crypto_pwhash_MEMLIMIT_INTERACTIVE) != 0) {
			std::cerr << "crypto_pwhash_str failed (out of memory?)\n";
			return;
		}
		UsernameHashMap[username] = std::string(securityString);
		DBController->addUser(username, UsernameHashMap[username]);
	}
}
