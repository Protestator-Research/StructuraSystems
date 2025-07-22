//---------------------------------------------------------
// Created by Moritz Herzog on 13.12.23.
//---------------------------------------------------------
//---------------------------------------------------------
// Constants, Definitions, Pragmas
//---------------------------------------------------------
#pragma once
//---------------------------------------------------------
// External Classes
//---------------------------------------------------------
#include <vector>
#include <string>
#include <boost/uuid/uuid.hpp>
//---------------------------------------------------------
// Internal Classes
//---------------------------------------------------------
//---------------------------------------------------------
// Forwarding
//---------------------------------------------------------
namespace SysMLv2::REST {
    class IEntity;
    class Commit;
    class Branch;
    class Project;
    class DigitalTwin;
    class Element;
}

namespace SysMLv2::API{
    class SysMLAPIImplementation;
}

namespace StructuraSystems::Client {
    /**
     * Abstracts the communication to an interface for the programmer. Allows the communication with the backend.
     * Hereby a connection to the server is opened. With this connection the REST protocol is generated and sent.
     * REST API Specification is here <a href="https://github.com/Systems-Modeling/SysML-v2-API-Services">https://github.com/Systems-Modeling/SysML-v2-API-Services</a>
     * @author Moritz Herzog <herzogm@rptu.de>
     * @version 1.0
     */
    class CommunicationService {
    public:
        /**
         * Constructor allows for the settings for the connection to the AGILA Backend.
         * @param serverAddress Address of the Server including the port and folder. The server address can be an IP address or a url.
         */
        CommunicationService(std::string serverAddress);

        /**
         * D-Tor
         */
        virtual ~CommunicationService() = default;

        /**
         * Creates a connection to the REST endpoint of the given AGILA Backend server,  to get all Elements of a Commit and Project, distinguished by its uuid.
         * @param commitId UUID of the commit
         * @param projectId UUID of the project
         * @return An std::vector of the Elements
         * @see uuid_t
         * @see std::vector
         * @see ENTITIES::Element
         */
        std::vector<std::shared_ptr<SysMLv2::REST::Element>> getAllElements(boost::uuids::uuid commitId, boost::uuids::uuid projectId);

        /**
         * Creates a connection to the REST endpoint of the given AGILA Backend sever, to get all saved projects within the instance of the Backend
         * @return An std::vector of the Projects
         */
        std::vector<std::shared_ptr<SysMLv2::REST::Project>> getAllProjects();

        /**
         * Creates online a project with the given Properties.
         * @param projectName The name of the Project.
         * @param projectDescription
         * @param defaultBranchName
         * @return
         */
        std::shared_ptr<SysMLv2::REST::Project> postProject(std::string projectName, std::string projectDescription, std::string defaultBranchName);

        /**
         * Download all branches for a project, with the projects id.
         * @param projectId UUID ("@id") of the project.
         * @return All Branches of the Project.
         */
        std::vector<std::shared_ptr<SysMLv2::REST::Branch>> getAllBranchesForProjectWithID(boost::uuids::uuid projectId);

        /**
         * Download the commits with its commit id and project id identifying the specific commit.
         * @param projectId UUID ("@id") of the project.
         * @param commitId UUID ("@id") of the commit.
         * @return The complete Commit
         * @see ENTITIES::Commit
         */
        std::shared_ptr<SysMLv2::REST::Commit> getCommitWithId(boost::uuids::uuid projectId, boost::uuids::uuid commitId);

        /**
         * Posts a new Commit to the Backend.
         * @param projectId 
         * @param commit 
         * @return 
         */
        std::shared_ptr<SysMLv2::REST::Commit> postCommitWithId(boost::uuids::uuid projectId, std::shared_ptr<SysMLv2::REST::Commit> commit);


        /**
         * Downloads all the elements of a specific commit.
         * @param projectId UUID ("@id") of the project.Í
         * @param commitId UUID ("@id") of the commit.
         * @return All elements of the specific commit.
         * @see ENTITIES::Element
         */
        std::vector<std::shared_ptr<SysMLv2::REST::Element>> getAllElementsOfCommit(boost::uuids::uuid projectId, boost::uuids::uuid commitId);

        /**
         * Sets and checks internally the user, that the server is connected to.
         * @param username The username or email of the user to login.
         * @param password The password of the user to login.x
         * @return
         */
        bool setUserForLoginInBackend(std::string username, std::string password);


        std::shared_ptr<SysMLv2::REST::DigitalTwin> postDigitalTwinToProject(boost::uuids::uuid projectId, std::shared_ptr<SysMLv2::REST::DigitalTwin> digitalTwin);

    private:
        std::string ServerAddress;

        std::string BarrierString;

        SysMLv2::API::SysMLAPIImplementation* APIImplementation;
    };
}
