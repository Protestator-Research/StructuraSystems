#include "DataBaseController.h"
#include "DataBaseController.h"

#include <bsoncxx/builder/basic/document.hpp>
#include <bsoncxx/json.hpp>

#include <stdexcept>
#include <boost/uuid/uuid_io.hpp>
#include <sysmlv2/rest/entities/JSONEntities.h>
#include <sysmlv2/rest/entities/Project.h>

namespace StructuraSystems::Server
{
	DataBaseController* DataBaseController::Instance = nullptr;

	DataBaseController* DataBaseController::getInstance()
	{
		if (!Instance)
			throw std::runtime_error("No Instance available");

		return Instance;
	}

	std::vector<std::shared_ptr<SysMLv2::REST::Project>> DataBaseController::getAllProjects()
	{
		auto collection = database["projects"];
		auto cursor = collection.find({});

		std::vector<std::shared_ptr<SysMLv2::REST::Project>> returnValue;
		for (auto && doc: cursor)
		{
			const auto project = std::make_shared<SysMLv2::REST::Project>(bsoncxx::to_json(doc));
			returnValue.push_back(project);
		}
		std::cout << returnValue.size() << " Projekts loaded from Database." << std::endl;
		return returnValue;
	}

	void DataBaseController::addMultibleProjects(std::vector<std::shared_ptr<SysMLv2::REST::Project>> projects)
	{
		std::vector<bsoncxx::document::value> dbProjects;
		for (const auto& project : projects)
		{
			dbProjects.push_back(bsoncxx::builder::basic::make_document(bsoncxx::from_json(project->serializeToJson())));
		}
		database["projects"].insert_many(dbProjects);
	}

	void DataBaseController::addProject(std::shared_ptr<SysMLv2::REST::Project> project)
	{
		database["projects"].insert_one(bsoncxx::builder::basic::make_document(bsoncxx::from_json(project->serializeToJson()))); 
	}

	void DataBaseController::updateProject(std::shared_ptr<SysMLv2::REST::Project> project)
	{
		auto query_filter = bsoncxx::builder::basic::make_document(bsoncxx::builder::basic::kvp(SysMLv2::REST::JSON_ID_ENTITY, boost::uuids::to_string(project->getId())));
		auto update_project = bsoncxx::builder::basic::make_document(bsoncxx::builder::basic::kvp("$set",bsoncxx::builder::basic::make_document(bsoncxx::from_json(project->serializeToJson()))));

		auto result = database["projects"].update_one(query_filter.view(), update_project.view());
	}

	bool DataBaseController::deleteProject(std::shared_ptr<SysMLv2::REST::Project> project)
	{
		auto result = database["projects"].delete_one(bsoncxx::builder::basic::make_document(bsoncxx::from_json(project->serializeToJson())));
		return (result.has_value() && (result.value().deleted_count() > 0));
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

		std::vector<std::shared_ptr<SysMLv2::REST::Project>> projects = {
			std::make_shared<SysMLv2::REST::Project>("ISO26262", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("SysMLv2 Introduction", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("SI", "Preloaded Project", "Main")
		};

		addMultibleProjects(projects);
	}

	void DataBaseController::deleteDatabaseIfDebug()
	{
		database.drop();
	}
}
