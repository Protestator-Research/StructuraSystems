//
// Created by Moritz Herzog on 13.12.23.
//
//---------------------------------------------------------
// Constants, Definitions, Pragmas
//---------------------------------------------------------

//---------------------------------------------------------
// External Classes
//---------------------------------------------------------
#include <utility>
#include <sysmlv2/rest/entities/IEntity.h>
#include <sysmlv2/rest/entities/Project.h>
#include <sysmlv2/rest/entities/Commit.h>
#include <sysmlv2/rest/entities/Branch.h>
#include <sysmlv2/rest/entities/Element.h>
#include "entities/DigitalTwin.h"
#include <sysmlv2service/online/SysMLAPIImplementation.h>
#include <boost/uuid/uuid_io.hpp>
#include <boost/lexical_cast.hpp>
#include <memory>

//---------------------------------------------------------
// Internal Classes
//---------------------------------------------------------
#include "BECommunicationService.h"


namespace StructuraSystems::Client {
    CommunicationService::CommunicationService(std::string serverAddress) {
        ServerAddress = std::move(serverAddress);
        APIImplementation = new SysMLv2::API::SysMLAPIImplementation(ServerAddress);
    }

    std::vector<std::shared_ptr<SysMLv2::REST::Element>> CommunicationService::getAllElements(boost::uuids::uuid commitId, boost::uuids::uuid projectId) {
        auto entities = APIImplementation->getAllElementsFromCommit(boost::lexical_cast<std::string>(projectId),boost::lexical_cast<std::string>(commitId), BarrierString);
        std::vector<std::shared_ptr<SysMLv2::REST::Element>> elements;

        for(auto entitiy : entities)
            elements.push_back(dynamic_pointer_cast<SysMLv2::REST::Element>(entitiy));

        return elements;
    }

    std::vector<std::shared_ptr<SysMLv2::REST::Project>> CommunicationService::getAllProjects() {
        auto projects = APIImplementation->getAllProjects(BarrierString);
        std::vector<std::shared_ptr<SysMLv2::REST::Project>> returnValue;

        for(auto oldProject : projects)
            returnValue.push_back(dynamic_pointer_cast<SysMLv2::REST::Project>(oldProject));

        return returnValue;
    }

    std::shared_ptr<SysMLv2::REST::Commit> CommunicationService::getCommitWithId(boost::uuids::uuid projectId, boost::uuids::uuid commitId) {
        auto commit = APIImplementation->getCommit(boost::lexical_cast<std::string>(projectId), boost::lexical_cast<std::string>(commitId), BarrierString);
        return std::dynamic_pointer_cast<SysMLv2::REST::Commit>(commit);
    }

    std::shared_ptr<SysMLv2::REST::Commit> CommunicationService::postCommitWithId(boost::uuids::uuid projectId, std::shared_ptr<SysMLv2::REST::Commit> commit)
    {
        std::string projectdString = boost::uuids::to_string(projectId);
        auto commi = APIImplementation->postCommit(projectdString, commit, BarrierString);

        std::shared_ptr<SysMLv2::REST::Commit> returnValue = dynamic_pointer_cast<SysMLv2::REST::Commit>(commi);
        return returnValue;
    }


    bool CommunicationService::setUserForLoginInBackend(std::string username, std::string password) {
        BarrierString = APIImplementation->loginUserWithPassword(username,password);
        return !BarrierString.empty();
    }

    std::shared_ptr<SysMLv2::REST::DigitalTwin> CommunicationService::postDigitalTwinToProject(
        boost::uuids::uuid , std::shared_ptr<SysMLv2::REST::DigitalTwin> ) {
        return std::make_shared<SysMLv2::REST::DigitalTwin>("");
    }


    std::vector<std::shared_ptr<SysMLv2::REST::Branch>> CommunicationService::getAllBranchesForProjectWithID(boost::uuids::uuid projectId) {
        auto elements = APIImplementation->getAllBranchesFroProject(boost::lexical_cast<std::string>(projectId), BarrierString);

        std::vector<std::shared_ptr<SysMLv2::REST::Branch>> returnValue;

        for (auto elem : elements)
            returnValue.push_back(dynamic_pointer_cast<SysMLv2::REST::Branch>(elem));

        return returnValue;
    }

    std::vector<std::shared_ptr<SysMLv2::REST::Element>> CommunicationService::getAllElementsOfCommit(boost::uuids::uuid projectId, boost::uuids::uuid commitId) {
        auto elements = APIImplementation->getAllElementsFromCommit(boost::lexical_cast<std::string>(projectId),boost::lexical_cast<std::string>(commitId), BarrierString);
        std::vector<std::shared_ptr<SysMLv2::REST::Element>> returnValue;

        for(auto elem : elements)
            returnValue.push_back(dynamic_pointer_cast<SysMLv2::REST::Element>(elem));

        return returnValue;
    }

    std::shared_ptr<SysMLv2::REST::Project> CommunicationService::postProject(std::string projectName, std::string projectDescription, std::string defaultBranchName) {
        std::shared_ptr<SysMLv2::REST::Project> project = std::make_shared<SysMLv2::REST::Project>(projectName,projectDescription, defaultBranchName);
        project = dynamic_pointer_cast<SysMLv2::REST::Project>(APIImplementation->postProject(project, BarrierString));
        return project;
    }

}