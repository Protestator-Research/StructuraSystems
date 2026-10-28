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
#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <vector>
#include <string>
#include <boost/uuid/uuid.hpp>
//---------------------------------------------------------
// Internal Classes
//---------------------------------------------------------
#include "PagedRequest.h"
//---------------------------------------------------------
// Forwarding
//---------------------------------------------------------
namespace SysMLv2::REST {
    class IEntity;
    class Commit;
    class Branch;
    class Project;
    class DigitalTwin;
    class DigitalTwinRequest;
    class CommitRequest;
}

namespace KerML::Entities {
    class Element;
}

namespace SysMLv2::API{
    class SysMLAPIImplementation;
}

namespace StructuraSystems::Client {
    /**
     * Information about the logged in user, as answered by GET me of the StructuraSystemsServer.
     * This is an extension of the StructuraSystemsServer and not part of the SysML v2 standard.
     */
    struct UserInfo {
        std::string Username;
        std::string Group;
        int Role = 0;
        bool MustChangePassword = false;
    };

    /**
     * The state of a branch at its head, as loaded from the server.
     */
    struct BranchState {
        std::shared_ptr<SysMLv2::REST::Branch> Branch;
        /** The head commit, nullptr for a branch without commits (a new project). */
        std::shared_ptr<SysMLv2::REST::Commit> Commit;
        /** The elements of the head commit, empty for a branch without head. */
        std::vector<std::shared_ptr<KerML::Entities::Element>> Elements;
    };

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
         * Like the constructor above, but the GET requests of the list routes, commits and elements are sent by the given
         * transport instead of libcurl. This allows to test the handling of the answers without a server.
         * @param serverAddress Address of the Server including the port and folder.
         * @param transport Sends one GET request.
         */
        CommunicationService(std::string serverAddress, HttpTransport transport);

        /**
         * D-Tor
         */
        virtual ~CommunicationService();

        /**
         * Creates a second connection to the same server with the same login. The underlying API implementation is not
         * thread safe, so requests that run in parallel to the other backend operations need their own connection.
         * @return A new, already authenticated connection.
         */
        [[nodiscard]] std::shared_ptr<CommunicationService> createIndependentConnection() const;

        /**
         * Sets the token that is sent with every request. It is normally set by setUserForLoginInBackend.
         * @param token The bearer token.
         */
        void setBearerToken(std::string token);

        /**
         * Creates a connection to the REST endpoint of the given AGILA Backend server,  to get all Elements of a Commit and Project, distinguished by its uuid.
         * @param commitId UUID of the commit
         * @param projectId UUID of the project
         * @return An std::vector of the Elements
         * @throws SysMLv2::API::EXCEPTIONS::HTTPException If the server answers with a status outside of 2xx.
         * @throws SysMLv2::API::EXCEPTIONS::ConnectionError If the server is not reachable.
         * @see uuid_t
         * @see std::vector
         * @see ENTITIES::Element
         */
        [[nodiscard]] std::vector<std::shared_ptr<KerML::Entities::Element>> getAllElements(boost::uuids::uuid commitId, boost::uuids::uuid projectId) const;

        /**
         * Creates a connection to the REST endpoint of the given AGILA Backend sever, to get all saved projects within the instance of the Backend.
         * Lists of the server have at most 100 entries per page, the following pages (Link header, rel="next") are loaded too.
         * @return An std::vector of the Projects
         * @throws SysMLv2::API::EXCEPTIONS::HTTPException If the server answers with a status outside of 2xx.
         * @throws SysMLv2::API::EXCEPTIONS::ConnectionError If the server is not reachable.
         */
        std::vector<std::shared_ptr<SysMLv2::REST::Project>> getAllProjects();

        /**
         * Creates online a project with the given Properties.
         * @param projectName The name of the Project.
         * @param projectDescription The description of the Project.
         * @param defaultBranchName The name of the default branch of the Project.
         * @param owningGroup Optional. The group that owns the project. The owner is always the logged in user, the
         *                    server decides on it.
         * @return The project created by the server.
         * @throws SysMLv2::API::EXCEPTIONS::HTTPException If the server answers with a status outside of 2xx.
         * @throws SysMLv2::API::EXCEPTIONS::ConnectionError If the server is not reachable.
         */
        std::shared_ptr<SysMLv2::REST::Project> postProject(std::string projectName, std::string projectDescription, std::string defaultBranchName, std::string owningGroup = "");

        /**
         * Download all branches for a project, with the projects id.
         * @param projectId UUID ("@id") of the project.
         * @return All Branches of the Project.
         * @throws SysMLv2::API::EXCEPTIONS::HTTPException If the server answers with a status outside of 2xx.
         * @throws SysMLv2::API::EXCEPTIONS::ConnectionError If the server is not reachable.
         */
        std::vector<std::shared_ptr<SysMLv2::REST::Branch>> getAllBranchesForProjectWithID(boost::uuids::uuid projectId);

        /**
         * Loads the head of a branch with its elements. The branches are read again, so the head is current. A branch
         * without head (a new project has none) yields an empty state, not an error.
         * @param projectId UUID ("@id") of the project.
         * @param branchId UUID ("@id") of the branch.
         * @return The branch, its head commit and the elements of the head.
         * @throws std::runtime_error If the project has no such branch.
         * @throws SysMLv2::API::EXCEPTIONS::HTTPException If the server answers with a status outside of 2xx.
         * @throws SysMLv2::API::EXCEPTIONS::ConnectionError If the server is not reachable.
         */
        BranchState loadBranchState(boost::uuids::uuid projectId, boost::uuids::uuid branchId);

        /**
         * Download the commits with its commit id and project id identifying the specific commit.
         * @param projectId UUID ("@id") of the project.
         * @param commitId UUID ("@id") of the commit.
         * @return The complete Commit
         * @throws SysMLv2::API::EXCEPTIONS::HTTPException If the server answers with a status outside of 2xx.
         * @throws SysMLv2::API::EXCEPTIONS::ConnectionError If the server is not reachable.
         * @see ENTITIES::Commit
         */
        std::shared_ptr<SysMLv2::REST::Commit> getCommitWithId(boost::uuids::uuid projectId, boost::uuids::uuid commitId);

        /**
         * Posts a new Commit to the Backend.
         * @param projectId UUID ("@id") of the project.
         * @param commit The changes and the description of the commit.
         * @return The commit created by the server.
         * @throws SysMLv2::API::EXCEPTIONS::HTTPException If the server answers with a status outside of 2xx.
         * @throws SysMLv2::API::EXCEPTIONS::ConnectionError If the server is not reachable.
         */
        std::shared_ptr<SysMLv2::REST::Commit> postCommitWithId(boost::uuids::uuid projectId, std::shared_ptr<SysMLv2::REST::CommitRequest> commit);


        /**
         * Downloads all commits of a project (GET projects/{projectId}/commits).
         * The commits are returned in the order of the server, use Commit::getPreviousCommits to build the history.
         * @param projectId UUID ("@id") of the project.
         * @param timestamps Optional. Receives the creation time of every commit by its id. The time is read from the
         *                   ISO 8601 text of the answer ("timestamp" or "created"), because Commit::getCreated does not
         *                   interpret ISO 8601 on every platform.
         * @return The commits of the project, empty if the project has none.
         * @throws SysMLv2::API::EXCEPTIONS::HTTPException If the server answers with a status outside of 2xx (e.g. 404 for an unknown project).
         * @throws SysMLv2::API::EXCEPTIONS::ConnectionError If the server is not reachable.
         * @throws std::runtime_error If the server answers with a 2xx and an invalid body.
         */
        std::vector<std::shared_ptr<SysMLv2::REST::Commit>> getAllCommitsForProject(boost::uuids::uuid projectId, std::map<boost::uuids::uuid, std::chrono::system_clock::time_point> *timestamps = nullptr);

        /**
         * Posts a new commit to a specific branch (POST projects/{projectId}/commits?branchId={branchId}).
         * @param projectId UUID ("@id") of the project.
         * @param branchId UUID ("@id") of the branch that receives the commit.
         * @param commit The changes and the description of the commit.
         * @return The commit created by the server.
         * @throws SysMLv2::API::EXCEPTIONS::HTTPException If the server answers with a status outside of 2xx (e.g. 404 for an unknown project or branch).
         * @throws SysMLv2::API::EXCEPTIONS::ConnectionError If the server is not reachable.
         * @throws std::runtime_error If the server answers with a 2xx and an invalid body.
         */
        std::shared_ptr<SysMLv2::REST::Commit> postCommitToBranch(boost::uuids::uuid projectId, boost::uuids::uuid branchId, std::shared_ptr<SysMLv2::REST::CommitRequest> commit);

        /**
         * Creates a new branch whose head is the given commit (POST projects/{projectId}/branches).
         * @param projectId UUID ("@id") of the project.
         * @param name Name of the new branch. The server rejects names that are already used in the project.
         * @param headCommitId UUID ("@id") of the commit, that becomes the head of the new branch.
         * @return The branch created by the server.
         * @throws SysMLv2::API::EXCEPTIONS::HTTPException If the server rejects the branch (e.g. 409 for a duplicate name, 404 for an unknown commit or project).
         * @throws SysMLv2::API::EXCEPTIONS::ConnectionError If the server is not reachable.
         * @throws std::runtime_error If the server answers with a 2xx and an invalid body.
         */
        std::shared_ptr<SysMLv2::REST::Branch> postBranch(boost::uuids::uuid projectId, const std::string &name, boost::uuids::uuid headCommitId);

        /**
         * Downloads all the elements of a specific commit.
         * @param projectId UUID ("@id") of the project.Í
         * @param commitId UUID ("@id") of the commit.
         * @return All elements of the specific commit.
         * @throws SysMLv2::API::EXCEPTIONS::HTTPException If the server answers with a status outside of 2xx.
         * @throws SysMLv2::API::EXCEPTIONS::ConnectionError If the server is not reachable.
         * @see ENTITIES::Element
         */
        std::vector<std::shared_ptr<KerML::Entities::Element>> getAllElementsOfCommit(boost::uuids::uuid projectId, boost::uuids::uuid commitId);

        /**
         * Sets and checks internally the user, that the server is connected to.
         * @param username The username or email of the user to login.
         * @param password The password of the user to login.
         * @return True if the server returned a token.
         * @throws std::invalid_argument If the credentials are wrong (HTTP 401).
         * @throws std::runtime_error If the login is throttled (HTTP 429), the message contains the seconds to wait if the server sent them.
         * @throws SysMLv2::API::EXCEPTIONS::HTTPException For all other status codes outside of 2xx.
         * @throws SysMLv2::API::EXCEPTIONS::ConnectionError If the server is not reachable.
         */
        bool setUserForLoginInBackend(std::string username, std::string password);

        /**
         * Reads the logged in user (GET me).
         * This endpoint is an extension of the StructuraSystemsServer and not part of the SysML v2 standard. It is not
         * called automatically.
         * @return The user, or std::nullopt if the server does not offer the endpoint (HTTP 404).
         * @throws SysMLv2::API::EXCEPTIONS::HTTPException For all other status codes outside of 2xx.
         * @throws SysMLv2::API::EXCEPTIONS::ConnectionError If the server is not reachable.
         * @throws std::runtime_error If the server answers with a 2xx and an invalid body.
         */
        std::optional<UserInfo> getCurrentUser();

        /**
         * Changes the password of the logged in user (PUT me/password).
         * This endpoint is an extension of the StructuraSystemsServer and not part of the SysML v2 standard. It is not
         * called automatically.
         * @param oldPassword The current password.
         * @param newPassword The new password, at least 12 characters.
         * @throws std::invalid_argument If the new password has fewer than 12 characters (nothing is sent).
         * @throws SysMLv2::API::EXCEPTIONS::HTTPException If the server answers with a status outside of 2xx (401 if the old password is wrong).
         * @throws SysMLv2::API::EXCEPTIONS::ConnectionError If the server is not reachable.
         */
        void changeOwnPassword(const std::string &oldPassword, const std::string &newPassword);

        /**
         * Creates a short, user facing text for an exception that was thrown by a backend call.
         * HTTPException and ConnectionError of the sysmllib are translated, for all other exceptions the text of
         * exception.what() is returned.
         * @param exception The exception to describe.
         * @return The text for the user.
         */
        static std::string describeBackendError(const std::exception &exception);


        /**
         * Creates a digital twin for a project (POST projects/{projectId}/twins).
         * @param projectId UUID ("@id") of the project.
         * @param twinRequest Name and referenced commit of the new digital twin.
         * @return The digital twin created by the server.
         * @throws SysMLv2::API::EXCEPTIONS::HTTPException If the server answers with a status outside of 2xx (e.g. 404 for an unknown project or commit).
         * @throws SysMLv2::API::EXCEPTIONS::ConnectionError If the server is not reachable.
         * @throws std::runtime_error If the server answers with a 2xx and an invalid body.
         */
        std::shared_ptr<SysMLv2::REST::DigitalTwin> postDigitalTwinToProject(boost::uuids::uuid projectId, std::shared_ptr<SysMLv2::REST::DigitalTwinRequest> twinRequest);

        /**
         * Downloads all digital twins of a project (GET projects/{projectId}/twins).
         * @param projectId UUID ("@id") of the project.
         * @return The digital twins of the project, empty if the project has none.
         * @throws SysMLv2::API::EXCEPTIONS::HTTPException If the server answers with a status outside of 2xx.
         * @throws SysMLv2::API::EXCEPTIONS::ConnectionError If the server is not reachable.
         * @throws std::runtime_error If the server answers with a 2xx and a body that is not a list.
         */
        std::vector<std::shared_ptr<SysMLv2::REST::DigitalTwin>> getAllDigitalTwinsForProject(boost::uuids::uuid projectId);

        /**
         * Deletes a project on the server (DELETE projects/{projectId}). This cannot be undone.
         * @param projectId UUID ("@id") of the project.
         * @throws SysMLv2::API::EXCEPTIONS::HTTPException If the server answers with a status outside of 2xx.
         * @throws SysMLv2::API::EXCEPTIONS::ConnectionError If the server is not reachable.
         */
        void deleteProject(boost::uuids::uuid projectId);

    private:
        /** Downloads all pages of a list route (see getAllPages). */
        [[nodiscard]] std::vector<nlohmann::json> getAllPagesOf(const std::string &endpoint) const;

        [[nodiscard]] std::vector<std::shared_ptr<KerML::Entities::Element>> elementsOfCommit(boost::uuids::uuid projectId, boost::uuids::uuid commitId) const;

        std::string ServerAddress;

        HttpTransport Transport;

        std::string BarrierString = "";

        std::unique_ptr<SysMLv2::API::SysMLAPIImplementation> APIImplementation;
    };
}
