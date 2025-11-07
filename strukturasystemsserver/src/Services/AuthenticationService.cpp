#include "AuthenticationService.h"

#include <iomanip>
#include <boost/uuid/nil_generator.hpp>
#include <boost/uuid/random_generator.hpp>

#include <openssl/sha.h>

namespace StructuraSystems::Server {
	std::shared_ptr<AuthenticationService> AuthenticationService::Instance = nullptr;

	AuthenticationService::AuthenticationService() {
#ifndef NO_DEBUG
		addUser("admin", "admin");
#endif 
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
		auto hashedPassword = hashSHA256From(password);
		if (UsernameHashMap[name] == hashedPassword)
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
		UsernameHashMap[username] = hashSHA256From(password);
	}

	std::string AuthenticationService::hashSHA256From(std::string value)
	{
		unsigned char hash[SHA256_DIGEST_LENGTH];
		SHA256_CTX sha256;
		SHA256_Init(&sha256);
		SHA256_Update(&sha256, value.c_str(), value.size());
		SHA256_Final(hash, &sha256);

		std::stringstream ss;
		for (int i = 0; i < SHA256_DIGEST_LENGTH; i++)
		{
			ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
		}
		return ss.str();
	}
}
