#include "DataBaseController.h"

#include <bsoncxx/builder/basic/document.hpp>
#include <bsoncxx/json.hpp>

#include <stdexcept>

namespace StructuraSystems::Server
{
	DataBaseController* DataBaseController::Instance = nullptr;

	DataBaseController* DataBaseController::getInstance()
	{
		if (!Instance)
			throw std::runtime_error("No Instance available");

		return Instance;
	}

	DataBaseController* DataBaseController::createInstance(std::string dbAddress, std::string username, std::string password)
	{
		if (Instance)
			return Instance;

		Instance = new DataBaseController(dbAddress, username, password);
	}

	DataBaseController::DataBaseController(std::string dBAddress, std::string username, std::string password)
	{
		std::string uri_string = "";

		if (username.empty())
			uri_string += "mongodb://" + dBAddress;
		else
			uri_string += "mongodb + srv://" + username + ":" + password + "@" + dBAddress;


		uri = mongocxx::uri(uri_string);
		client = mongocxx::client(uri);

		

		database = client["structura_systems"];

#ifdef _DEBUG
		deleteDatabaseIfDebug();
#endif
		initializeDatabaseIfNotAvailable();
	}

	void DataBaseController::initializeDatabaseIfNotAvailable()
	{
		if (database.list_collection_names().size()>0)
			return;
		
		database.create_collection("projects");
		database.create_collection("data_elements");
		database.create_collection("commits");
		database.create_collection("tags");
		database.create_collection("branches");
	}

	void DataBaseController::deleteDatabaseIfDebug()
	{
		database.drop();
	}
}
