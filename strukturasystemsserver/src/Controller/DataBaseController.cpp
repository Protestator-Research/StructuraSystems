#include "DataBaseController.h"
#include "DataBaseController.h"

#include <bsoncxx/builder/basic/document.hpp>
#include <bsoncxx/json.hpp>
#include <nlohmann/json.hpp>

#include <vector>
#include <memory>
#include <QFile>
#include <qlogging.h>
#include <QString>
#include <QFile>
#include <stdexcept>
#include <boost/uuid/string_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <kerml/root/annotations/TextualRepresentation.h>
#include <nlohmann/json_fwd.hpp>
#include <nlohmann/detail/meta/std_fs.hpp>
#include <sysmlv2/rest/entities/Branch.h>
#include <sysmlv2/rest/entities/JSONEntities.h>
#include <sysmlv2/rest/entities/Project.h>
#include <sysmlv2/rest/entities/Branch.h>
#include <sysmlv2/rest/entities/Commit.h>
#include <sysmlv2/rest/entities/DataVersion.h>
#include <sysmlv2/rest/serialization/Utilities.h>
#include <sysmlv2/service/implementation/ElementNavigationService.h>

#include "CommitController.hpp"

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
		for (auto&& doc : cursor)
		{
			std::string dbString = bsoncxx::to_json(doc);
			replace(dbString, "_id", "@id");
			std::cout << dbString << std::endl;
			const auto project = std::make_shared<SysMLv2::REST::Project>(dbString);
			returnValue.push_back(project);
		}
		std::cout << returnValue.size() << " Projects loaded from Database." << std::endl;
		return returnValue;
	}

	void DataBaseController::addMultibleProjects(std::vector<std::shared_ptr<SysMLv2::REST::Project>> projects)
	{
		std::vector<bsoncxx::document::value> dbProjects;
		for (const auto& project : projects)
		{
			dbProjects.push_back(bsoncxx::builder::basic::make_document(bsoncxx::builder::basic::kvp("_id", boost::uuids::to_string(project->getId())), bsoncxx::builder::basic::kvp("name", project->getName()), bsoncxx::builder::basic::kvp("description", project->getDescription()), bsoncxx::builder::basic::kvp("defaultBranch", "{\"@id\":" + boost::uuids::to_string(project->getDefaultBranch()->getId()) + "}")));
		}
		database["projects"].insert_many(dbProjects);
	}

	void DataBaseController::addProject(std::shared_ptr<SysMLv2::REST::Project> project)
	{
		database["projects"].insert_one(bsoncxx::builder::basic::make_document(bsoncxx::builder::basic::kvp("_id", boost::uuids::to_string(project->getId())), bsoncxx::builder::basic::kvp("name", project->getName()), bsoncxx::builder::basic::kvp("description", project->getDescription()), bsoncxx::builder::basic::kvp("defaultBranch", "{\"@id\":" + boost::uuids::to_string(project->getDefaultBranch()->getId()) + "}")));
	}

	void DataBaseController::updateProject(std::shared_ptr<SysMLv2::REST::Project> project)
	{
		auto query_filter = bsoncxx::builder::basic::make_document(bsoncxx::builder::basic::kvp("_id", boost::uuids::to_string(project->getId())));
		auto update_project = bsoncxx::builder::basic::make_document(bsoncxx::builder::basic::kvp("$set",
			bsoncxx::builder::basic::make_document(bsoncxx::builder::basic::kvp("_id", boost::uuids::to_string(project->getId())), bsoncxx::builder::basic::kvp("name", project->getName()), bsoncxx::builder::basic::kvp("description", project->getDescription()), bsoncxx::builder::basic::kvp("defaultBranch", "{\"@id\":" + boost::uuids::to_string(project->getDefaultBranch()->getId()) + "}"))));

		auto result = database["projects"].update_one(query_filter.view(), update_project.view());
	}

	bool DataBaseController::deleteProject(std::shared_ptr<SysMLv2::REST::Project> project)
	{
		auto result = database["projects"].delete_one(bsoncxx::builder::basic::make_document(bsoncxx::builder::basic::kvp("_id", boost::uuids::to_string(project->getId())), bsoncxx::builder::basic::kvp("name", project->getName()), bsoncxx::builder::basic::kvp("description", project->getDescription()), bsoncxx::builder::basic::kvp("defaultBranch", "{\"@id\":" + boost::uuids::to_string(project->getDefaultBranch()->getId()) + "}")));
		return (result.has_value() && (result.value().deleted_count() > 0));
	}

	void DataBaseController::addMultibleBranches(
		std::map<boost::uuids::uuid, std::shared_ptr<SysMLv2::REST::Branch>> projectBranchMap)
	{
		std::vector<bsoncxx::document::value> dbBranches;
		for (const auto element : projectBranchMap)
		{
			dbBranches.push_back(bsoncxx::builder::basic::make_document(
				bsoncxx::builder::basic::kvp("_id", boost::uuids::to_string(element.second->getId())),
				bsoncxx::builder::basic::kvp("name", element.second->getName()),
				bsoncxx::builder::basic::kvp("head", element.second->getHead()->serializeIdentification()),
				bsoncxx::builder::basic::kvp("created", SysMLv2::REST::Utilities::toIso8601(element.second->created())),
				bsoncxx::builder::basic::kvp("deleted", SysMLv2::REST::Utilities::toIso8601(element.second->deleted())),
				bsoncxx::builder::basic::kvp("_project_id",boost::uuids::to_string(element.first))			
			)); 
		}
		database["branches"].insert_many(dbBranches);
	}

	void DataBaseController::addBranch(boost::uuids::uuid projectId, std::shared_ptr<SysMLv2::REST::Branch> branch)
	{
		database["branches"].insert_one(bsoncxx::builder::basic::make_document(
			bsoncxx::builder::basic::kvp("_id", boost::uuids::to_string(branch->getId())),
			bsoncxx::builder::basic::kvp("name", branch->getName()),
			bsoncxx::builder::basic::kvp("head", branch->getHead()->serializeIdentification()),
			bsoncxx::builder::basic::kvp("created", SysMLv2::REST::Utilities::toIso8601(branch->created())),
			bsoncxx::builder::basic::kvp("deleted", SysMLv2::REST::Utilities::toIso8601(branch->deleted())),
			bsoncxx::builder::basic::kvp("_project_id", boost::uuids::to_string(projectId))
		));
	}

	std::map<boost::uuids::uuid, std::shared_ptr<SysMLv2::REST::Branch>> DataBaseController::getAllBranches()
	{
		std::map<boost::uuids::uuid, std::shared_ptr<SysMLv2::REST::Branch>> returnValue;

		auto collection = database["branches"];
		auto cursor = collection.find({});

		for (auto&& doc : cursor)
		{
			std::string dbString = bsoncxx::to_json(doc);
			replace(dbString, "_id", "@id");
			std::cout << dbString << std::endl;
			nlohmann::json json = nlohmann::json::parse(dbString);
			auto projectID = boost::uuids::string_generator()(json["_project_id"].get<std::string>());
			json.erase("_project_id");
			const auto branch = std::make_shared<SysMLv2::REST::Branch>(json.dump());
			returnValue.insert(std::make_pair(projectID,branch));
		}
		std::cout << returnValue.size() << " Branches loaded from Database." << std::endl;
		return returnValue;
	}

	void DataBaseController::addMultibleCommits(std::map<boost::uuids::uuid, std::shared_ptr<SysMLv2::REST::Commit>> projectCommitMap)
	{
		std::vector<bsoncxx::document::value> dbCommits;
		for (const auto& [proj_id, commit] : projectCommitMap)
		{
			std::string dataVersionString = "[\r\n";
			for (size_t i = 0; i < commit->getDataVersion().size(); i++)
			{
				dataVersionString += commit->getDataVersion()[i]->serializeIdentification();
				if (i != (commit->getDataVersion().size() - 1))
					dataVersionString += ",\r\n";
			}
			dataVersionString += "]\r\n";

			std::string jsonElementsPrevCommits = "[\r\n";
			for (size_t i = 0; i < commit->getPreviusCommits().size(); i++) {
				jsonElementsPrevCommits += commit->getPreviusCommits()[i]->serializeIdentification();

				if (i != (commit->getPreviusCommits().size() - 1))
					jsonElementsPrevCommits += ",\r\n";
			}
			jsonElementsPrevCommits += "]\r\n";

			dbCommits.push_back(bsoncxx::builder::basic::make_document(
				bsoncxx::builder::basic::kvp("_id", boost::uuids::to_string(commit->getId())),
				bsoncxx::builder::basic::kvp(SysMLv2::REST::JSON_DESCRIPTION_ENTITY, commit->getDescription()),
				bsoncxx::builder::basic::kvp(SysMLv2::REST::JSON_CHANGE_ENTITY, dataVersionString),
				bsoncxx::builder::basic::kvp(SysMLv2::REST::JSON_OWNING_PROJECT, commit->getOwningProject()->serializeIdentification()),
				bsoncxx::builder::basic::kvp(SysMLv2::REST::JSON_TIMESTAMP_ENTITY, SysMLv2::REST::Utilities::toIso8601(commit->getCreated())),
				bsoncxx::builder::basic::kvp(SysMLv2::REST::JSON_PREV_COMMITS, jsonElementsPrevCommits)
			));
		}
		database["commits"].insert_many(dbCommits);
	}

	void DataBaseController::addCommit(std::shared_ptr<SysMLv2::REST::Commit> commit)
	{
		std::string dataVersionString = "[\r\n";
		for (size_t i = 0; i < commit->getDataVersion().size(); i++)
		{
			dataVersionString += commit->getDataVersion()[i]->serializeIdentification();
			if (i != (commit->getDataVersion().size() - 1))
				dataVersionString += ",\r\n";
		}
		dataVersionString += "]\r\n";

		std::string jsonElementsPrevCommits = "[\r\n";
		for (size_t i = 0; i < commit->getPreviusCommits().size(); i++) {
			jsonElementsPrevCommits += commit->getPreviusCommits()[i]->serializeIdentification();

			if (i != (commit->getPreviusCommits().size() - 1))
				jsonElementsPrevCommits += ",\r\n";
		}
		jsonElementsPrevCommits += "]\r\n";

		database["commits"].insert_one(bsoncxx::builder::basic::make_document(
			bsoncxx::builder::basic::kvp("_id", boost::uuids::to_string(commit->getId())),
			bsoncxx::builder::basic::kvp(SysMLv2::REST::JSON_DESCRIPTION_ENTITY, commit->getDescription()),
			bsoncxx::builder::basic::kvp(SysMLv2::REST::JSON_CHANGE_ENTITY, dataVersionString),
			bsoncxx::builder::basic::kvp(SysMLv2::REST::JSON_OWNING_PROJECT, commit->getOwningProject()->serializeIdentification()),
			bsoncxx::builder::basic::kvp(SysMLv2::REST::JSON_TIMESTAMP_ENTITY, SysMLv2::REST::Utilities::toIso8601(commit->getCreated())),
			bsoncxx::builder::basic::kvp(SysMLv2::REST::JSON_PREV_COMMITS, jsonElementsPrevCommits)
		));
	}

	std::map<boost::uuids::uuid, std::shared_ptr<SysMLv2::REST::Commit>> DataBaseController::getAllCommits()
	{
		std::map<boost::uuids::uuid, std::shared_ptr<SysMLv2::REST::Commit>> returnValue;

		auto collection = database["commits"];
		auto cursor = collection.find({});

		for (auto&& doc : cursor)
		{
			std::string dbString = bsoncxx::to_json(doc);
			replace(dbString, "_id", "@id");
			std::cout << dbString << std::endl;
			nlohmann::json json = nlohmann::json::parse(dbString);
			const auto commit = std::make_shared<SysMLv2::REST::Commit>(json.dump());
			returnValue.insert(std::make_pair(commit->getOwningProject()->getId(), commit));
		}
		std::cout << returnValue.size() << " Commits loaded from Database." << std::endl;
		return returnValue;
	}

	void DataBaseController::addUser(std::string username, std::string securityString)
	{
		database["users"].insert_one(bsoncxx::builder::basic::make_document(
			bsoncxx::builder::basic::kvp("_id",username),
			bsoncxx::builder::basic::kvp("securityString", securityString)
		));
	}

	std::map<std::string, std::string> DataBaseController::getAllUser()
	{
		auto collection = database["users"];
		auto cursor = collection.find({});
		std::map<std::string, std::string> returnValue;
		for (auto&& doc : cursor)
		{
			std::string dbString = bsoncxx::to_json(doc);
			nlohmann::json json = nlohmann::json::parse(dbString);
			returnValue.insert(std::make_pair(
				json["_id"].get<std::string>(),
				json["securityString"].get<std::string>()
			));
		}
		return returnValue;
	}


	DataBaseController* DataBaseController::createInstance(std::string dbAddress, std::string username, std::string password)
	{
		if (Instance)
			return Instance;

		Instance = new DataBaseController(dbAddress, username, password);
		return Instance;
	}

	DataBaseController::DataBaseController(std::string dBAddress, std::string username, std::string password)
	{
		std::string uri_string = "";

		if (username.empty())
			uri_string += "mongodb://" + dBAddress;
		else
			uri_string += "mongodb://" + username + ":" + password + "@" + dBAddress;


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
		try
		{
			if (database.list_collection_names().size() > 0)
				return;
		}
		catch (...)
		{
		}

		database.create_collection("projects");
		database.create_collection("data_elements");
		database.create_collection("commits");
		database.create_collection("data_versions");
		database.create_collection("tags");
		database.create_collection("branches");
		database.create_collection("users");

		std::vector<std::shared_ptr<SysMLv2::REST::Project>> projects = {
			std::make_shared<SysMLv2::REST::Project>("AnalysisTooling", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("SampledFunctions", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("StateSpaceRepresentation", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("TradeStudies", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("CausationConnections", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("CauseAndEffect", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("ShapeItems", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("SpatialItems", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("ImageMetadata", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("ModelingMetadata", "Preloaded Project", "Main"),

			std::make_shared<SysMLv2::REST::Project>("ParametersOfInterest", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("RiskMetadata", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("ISQ", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("ISQAcoustics", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("ISQAtomicNuclear", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("ISQBase", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("ISQCharacteristicNumbers", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("ISQChemistryMolecular", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("ISQCondensedMatter", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("ISQElectromagnetism", "Preloaded Project", "Main"),

			std::make_shared<SysMLv2::REST::Project>("ISQInformation", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("ISQLight", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("ISQMechanics", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("ISQSpaceTime", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("ISQThermodynamics", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("MeasurementRefCalculations", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("MeasurementReferences", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("Quantities", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("QuantityCalculations", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("SI", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("SIPrefixes", "Preloaded Project", "Main"),

			std::make_shared<SysMLv2::REST::Project>("TensorCalculations", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("Time", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("USCustomaryUnits", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("VectorCalculations", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("DerivationConnections", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("RequirementDerivation", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("Collections", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("ScalarValues", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("VectorValues", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("BaseFunctions", "Preloaded Project", "Main"),

			std::make_shared<SysMLv2::REST::Project>("BooleanFunctions", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("CollectionFunctions", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("ComplexFunctions", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("ControlFunctions", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("DataFunctions", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("IntegerFunctions", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("NaturalFunctions", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("NumericalFunctions", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("OccurrenceFunctions", "Preloaded Project", "Main"),

			std::make_shared<SysMLv2::REST::Project>("RationalFunctions", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("RealFunctions", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("ScalarFunctions", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("SequenceFunctions", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("StringFunctions", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("TrigFunctions", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("VectorFunctions", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("Base", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("Clocks", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("ControlPerformances", "Preloaded Project", "Main"),

			std::make_shared<SysMLv2::REST::Project>("FeatureReferencingPerformances", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("KerML", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("Links", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("Metaobjects", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("Objects", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("Observation", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("Occurrences", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("Performances", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("SpatialFrames", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("StatePerformances", "Preloaded Project", "Main"),

			std::make_shared<SysMLv2::REST::Project>("Transfers", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("TransitionPerformances", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("Triggers", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("Actions", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("Allocations", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("AnalysisCases", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("Attributes", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("Calculations", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("Cases", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("Connections", "Preloaded Project", "Main"),

			std::make_shared<SysMLv2::REST::Project>("Constraints", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("Flows", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("Interfaces", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("Items", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("Metadata", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("Parts", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("Ports", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("Requirements", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("StandardViewDefinitions", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("States", "Preloaded Project", "Main"),

			std::make_shared<SysMLv2::REST::Project>("SysML", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("UseCases", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("VerificationCases", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("Views", "Preloaded Project", "Main")
		};
		addMultibleProjects(projects);

		std::map<boost::uuids::uuid, std::shared_ptr<SysMLv2::REST::Branch>> projectBranchMap;
		for (const auto& project : projects)
		{
			projectBranchMap.insert(std::make_pair(project->getId(), project->getDefaultBranch()));
		}
		

		std::map<boost::uuids::uuid, std::shared_ptr<SysMLv2::REST::Commit>> commitProjectMap;
		for (const auto& project : projects)
		{

			QFile file(QString::fromStdString(":/sysml/" + project->getName()));
			if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
			{
				std::cerr << "Could not open file: " << project->getName() << std::endl;
				break;
			}
			QTextStream in(&file);
			QString readFile = in.readAll();

			auto commit = std::make_shared<SysMLv2::REST::Commit>("Initial Commit from Structura Systems",project);
			auto change = std::make_shared<SysMLv2::REST::DataVersion>();
			change->setPayload(std::make_shared<KerML::Entities::TextualRepresentation>("SysML v2", readFile.toStdString()));
			commit->addChange(change);
			commitProjectMap.insert(std::make_pair(project->getId(), commit));
			project->getDefaultBranch()->setHead(commit);
		}

		addMultibleBranches(projectBranchMap);
		addMultibleCommits(commitProjectMap);
	}

	void DataBaseController::deleteDatabaseIfDebug()
	{
		std::cout << "\033[31m" << "----------------------------------------------------------------------" << std::endl;
		std::cout << "       Deleting Database." << std::endl;
		std::cout << "----------------------------------------------------------------------" << "\033[0m" << std::endl;

		try
		{
			database.drop();
		}
		catch (...)
		{
			std::cout << "Could not delete DB, since no DB avaiable" << std::endl;
		}
	}

	bool DataBaseController::replace(std::string& str, const std::string& from, const std::string& to)
	{
		size_t start_pos = str.find(from);

		if (start_pos == std::string::npos)
			return false;

		str.replace(start_pos, from.length(), to);

		return true;
	}
}
