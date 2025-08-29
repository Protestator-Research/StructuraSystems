#pragma once

#include <string>
#include <memory>

#include <mongocxx/client.hpp>
#include <mongocxx/instance.hpp>
#include <mongocxx/uri.hpp>

namespace SysMLv2::REST
{
	class Project;
}

namespace StructuraSystems::Server
{
	class DataBaseController
	{
	public:
		DataBaseController() = delete;
		static DataBaseController* createInstance(std::string dbAddress, std::string username, std::string password);
		static DataBaseController* getInstance();


		std::vector<std::shared_ptr<SysMLv2::REST::Project>> getAllProjects();
		void addMultibleProjects(std::vector<std::shared_ptr<SysMLv2::REST::Project>> projects);
		void addProject(std::shared_ptr<SysMLv2::REST::Project> project);
		void updateProject(std::shared_ptr<SysMLv2::REST::Project> project);
		bool deleteProject(std::shared_ptr<SysMLv2::REST::Project> project);



	private:
		DataBaseController(std::string dBAddress, std::string username, std::string password);
 
		static DataBaseController* Instance;

		mongocxx::instance instance;
		mongocxx::uri uri;
		mongocxx::client client;
		mongocxx::database database;

		void initializeDatabaseIfNotAvailable();

		void deleteDatabaseIfDebug();
	};
}

