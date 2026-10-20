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
#include <sysmlv2/rest/entities/ProjectRequest.h>
#include <sysmlv2/rest/entities/CommitRequest.h>
#include <kerml/root/annotations/TextualRepresentation.h>
#include <kerml/root/elements/Element.h>
#include "entities/DigitalTwin.h"
#include "entities/DigitalTwinRequest.h"
#include <nlohmann/json.hpp>
#include <sysmlv2/service/online/SysMLAPIImplementation.h>
#include <sysmlv2/service/online/HttpException.h>
#include <sysmlv2/service/online/ConnectionError.h>
#include <boost/uuid/uuid_io.hpp>
#include <boost/lexical_cast.hpp>
#include <cstdio>
#include <memory>
#include <optional>
#include <stdexcept>

//---------------------------------------------------------
// Internal Classes
//---------------------------------------------------------
#include "BECommunicationService.h"



namespace {
    /**
     * Reads an ISO 8601 timestamp (2026-10-18T14:03:00[.ffffff][Z|+hh:mm]). A timestamp without zone is UTC, like the
     * ones written by the sysmllib.
     */
    std::optional<std::chrono::system_clock::time_point> parseIso8601(const std::string &text) {
        int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0, consumed = 0;
        if (std::sscanf(text.c_str(), "%d-%d-%dT%d:%d:%d%n", &year, &month, &day, &hour, &minute, &second, &consumed) != 6)
            return std::nullopt;

        auto position = static_cast<size_t>(consumed);
        long long fractionMicroseconds = 0;
        if (position < text.size() && text[position] == '.') {
            position++;
            long long scale = 100000;
            while (position < text.size() && text[position] >= '0' && text[position] <= '9') {
                fractionMicroseconds += (text[position] - '0') * scale;
                scale /= 10;
                position++;
            }
        }

        long long offsetSeconds = 0;
        if (position < text.size() && (text[position] == '+' || text[position] == '-')) {
            int offsetHours = 0, offsetMinutes = 0;
            if (std::sscanf(text.c_str() + position + 1, "%2d:%2d", &offsetHours, &offsetMinutes) < 1)
                return std::nullopt;
            offsetSeconds = (offsetHours * 3600LL + offsetMinutes * 60LL) * (text[position] == '-' ? -1 : 1);
        }

        // Days since 1970-01-01 (civil calendar algorithm by Howard Hinnant).
        year -= month <= 2 ? 1 : 0;
        const long long era = (year >= 0 ? year : year - 399) / 400;
        const long long yearOfEra = year - era * 400;
        const long long dayOfYear = (153LL * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
        const long long dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
        const long long days = era * 146097 + dayOfEra - 719468;

        const long long seconds = days * 86400 + hour * 3600LL + minute * 60LL + second - offsetSeconds;
        return std::chrono::system_clock::time_point(std::chrono::duration_cast<std::chrono::system_clock::duration>(
            std::chrono::seconds(seconds) + std::chrono::microseconds(fractionMicroseconds)));
    }

    /**
     * Creates a commit from its JSON object. The Commit class reads the creation time from "timestamp", the SysML v2
     * API standard calls it "created". If only one of them is present, it is copied, so that the answer is accepted.
     * @param timestamp Receives the parsed creation time, if the answer contains one.
     */
    std::shared_ptr<SysMLv2::REST::Commit> commitFromJson(nlohmann::json json, std::optional<std::chrono::system_clock::time_point> &timestamp) {
        if (!json.is_object() || !json.contains("@id") || !json["@id"].is_string())
            throw std::runtime_error("The server answered with an entry that is not a commit.");

        if (!json.contains("timestamp") && json.contains("created"))
            json["timestamp"] = json["created"];
        // The Commit class needs a timestamp next to a description; the placeholder is not reported as creation time.
        const bool hasTimestamp = json.contains("timestamp");
        if (json.contains("description") && !hasTimestamp)
            json["timestamp"] = "1970-01-01T00:00:00Z";

        if (hasTimestamp && json["timestamp"].is_string())
            timestamp = parseIso8601(json["timestamp"].get<std::string>());
        else
            timestamp.reset();

        return std::make_shared<SysMLv2::REST::Commit>(json.dump());
    }

    /** Short text of an error answer of the server, if it is a JSON object with a message. */
    std::string serverMessage(const nlohmann::json &json) {
        if (json.is_object()) {
            for (const auto *key : {"message", "error", "detail"})
                if (json.contains(key) && json[key].is_string())
                    return json[key].get<std::string>();
        }
        return {};
    }
}

namespace StructuraSystems::Client {
    CommunicationService::CommunicationService(std::string serverAddress) {
        ServerAddress = std::move(serverAddress);
        APIImplementation = std::make_unique<SysMLv2::API::SysMLAPIImplementation>(ServerAddress);
    }

    CommunicationService::~CommunicationService() = default;

    std::shared_ptr<CommunicationService> CommunicationService::createIndependentConnection() const {
        auto connection = std::make_shared<CommunicationService>(ServerAddress);
        connection->BarrierString = BarrierString;
        return connection;
    }

    std::vector<std::shared_ptr<KerML::Entities::Element>> CommunicationService::getAllElements(boost::uuids::uuid commitId, boost::uuids::uuid projectId) const {
        auto entities = APIImplementation->getAllElementsFromCommit(boost::lexical_cast<std::string>(projectId),boost::lexical_cast<std::string>(commitId), BarrierString);
        std::vector<std::shared_ptr<KerML::Entities::Element>> elements;

        for(auto entitiy : entities)
            elements.push_back(dynamic_pointer_cast<KerML::Entities::Element>(entitiy));

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

    std::shared_ptr<SysMLv2::REST::Commit> CommunicationService::postCommitWithId(boost::uuids::uuid projectId, std::shared_ptr<SysMLv2::REST::CommitRequest> commit)
    {
        std::string projectdString = boost::uuids::to_string(projectId);
        auto commi = APIImplementation->postCommit(projectdString, commit, BarrierString);
        return dynamic_pointer_cast<SysMLv2::REST::Commit>(commi);
    }


    bool CommunicationService::setUserForLoginInBackend(std::string username, std::string password) {
        try {
            BarrierString = APIImplementation->loginUserWithPassword(username,password);
        } catch (const SysMLv2::API::EXCEPTIONS::HTTPException &exception) {
            if (exception.httpErrorCode() == 401)
                throw std::invalid_argument("Invalid username or password");
            if (exception.httpErrorCode() == 429) {
                std::string message = "Too many login attempts.";
                if (exception.retryAfterSeconds() >= 0)
                    message += " Please try again in " + std::to_string(exception.retryAfterSeconds()) + " s.";
                throw std::runtime_error(message);
            }
            throw;
        }
        return !BarrierString.empty();
    }

    std::optional<UserInfo> CommunicationService::getCurrentUser() {
        std::string answer;
        try {
            answer = APIImplementation->getCustomRequest("me", BarrierString);
        } catch (const SysMLv2::API::EXCEPTIONS::HTTPException &exception) {
            // A server without the extension does not know the endpoint.
            if (exception.httpErrorCode() == 404)
                return std::nullopt;
            throw;
        }

        if (!nlohmann::json::accept(answer))
            throw std::runtime_error("The server did not return valid user information.");
        const auto json = nlohmann::json::parse(answer);
        if (!json.is_object() || !json.contains("username") || !json["username"].is_string())
            throw std::runtime_error("The server did not return valid user information.");

        UserInfo user;
        user.Username = json["username"].get<std::string>();
        if (json.contains("group") && json["group"].is_string())
            user.Group = json["group"].get<std::string>();
        if (json.contains("role") && json["role"].is_number_integer())
            user.Role = json["role"].get<int>();
        if (json.contains("mustChangePassword") && json["mustChangePassword"].is_boolean())
            user.MustChangePassword = json["mustChangePassword"].get<bool>();
        return user;
    }

    void CommunicationService::changeOwnPassword(const std::string &oldPassword, const std::string &newPassword) {
        // Counted in bytes of UTF-8 by the server too; the check only avoids a request that is rejected anyway.
        if (newPassword.size() < 12)
            throw std::invalid_argument("The new password must have at least 12 characters.");

        nlohmann::json request;
        request["oldPassword"] = oldPassword;
        request["newPassword"] = newPassword;
        APIImplementation->putCustomRequest("me/password", request.dump(), BarrierString);
    }

    std::string CommunicationService::describeBackendError(const std::exception &exception) {
        if (const auto *http = dynamic_cast<const SysMLv2::API::EXCEPTIONS::HTTPException *>(&exception)) {
            switch (http->httpErrorCode()) {
                case 401:
                    return "The session has expired or you are not logged in. Please log in again.";
                case 403:
                    if (http->serverErrorCode() == "password_change_required")
                        return "The password must be changed before you can continue.";
                    return "You do not have permission for this action.";
                case 404:
                    return "Not found, or you do not have access to it.";
                case 409: {
                    const auto serverText = http->serverErrorCode();
                    return "Conflict with the current state of the server." + (serverText.empty() ? std::string() : " " + serverText);
                }
                case 429:
                    if (http->retryAfterSeconds() >= 0)
                        return "Too many attempts. Please try again in " + std::to_string(http->retryAfterSeconds()) + " s.";
                    return "Too many attempts. Please try again later.";
                default:
                    break;
            }
        } else if (dynamic_cast<const SysMLv2::API::EXCEPTIONS::ConnectionError *>(&exception) != nullptr) {
            return std::string("The server is not reachable: ") + exception.what();
        }
        return exception.what();
    }

    std::shared_ptr<SysMLv2::REST::DigitalTwin> CommunicationService::postDigitalTwinToProject(
        boost::uuids::uuid projectId, std::shared_ptr<SysMLv2::REST::DigitalTwinRequest> twinRequest) {
        const auto digiTwinJSON = APIImplementation->postCustomRequest("projects/"+boost::lexical_cast<std::string>(projectId)+"/twins",twinRequest->serializeToJson(),BarrierString);
        if (!nlohmann::json::accept(digiTwinJSON))
            throw std::runtime_error("The server did not return a valid digital twin.");
        return std::make_shared<SysMLv2::REST::DigitalTwin>(digiTwinJSON);
    }

    void CommunicationService::deleteProject(boost::uuids::uuid projectId) {
        APIImplementation->deleteCustomRequest("projects/"+boost::lexical_cast<std::string>(projectId),BarrierString);
    }

    std::vector<std::shared_ptr<SysMLv2::REST::Commit>> CommunicationService::getAllCommitsForProject(
        boost::uuids::uuid projectId, std::map<boost::uuids::uuid, std::chrono::system_clock::time_point> *timestamps) {
        const auto commitsJSON = APIImplementation->getCustomRequest("projects/"+boost::lexical_cast<std::string>(projectId)+"/commits",BarrierString);
        // A project without commits answers with an empty array.
        if (!nlohmann::json::accept(commitsJSON))
            throw std::runtime_error("The server did not return the commits of the project.");

        const auto parsedJson = nlohmann::json::parse(commitsJSON);
        if (!parsedJson.is_array()) {
            const auto message = serverMessage(parsedJson);
            throw std::runtime_error("The server did not return a list of commits." + (message.empty() ? std::string() : " " + message));
        }

        std::vector<std::shared_ptr<SysMLv2::REST::Commit>> returnValue;
        returnValue.reserve(parsedJson.size());
        for (const auto& entry : parsedJson) {
            std::optional<std::chrono::system_clock::time_point> timestamp;
            auto commit = commitFromJson(entry, timestamp);
            if (timestamps != nullptr && timestamp.has_value())
                (*timestamps)[commit->getId()] = *timestamp;
            returnValue.push_back(std::move(commit));
        }
        return returnValue;
    }

    std::shared_ptr<SysMLv2::REST::Commit> CommunicationService::postCommitToBranch(
        boost::uuids::uuid projectId, boost::uuids::uuid branchId, std::shared_ptr<SysMLv2::REST::CommitRequest> commit) {
        const auto endpoint = "projects/"+boost::lexical_cast<std::string>(projectId)+"/commits?branchId="+boost::lexical_cast<std::string>(branchId);
        const auto commitJSON = APIImplementation->postCustomRequest(endpoint,commit->serializeToJson(),BarrierString);
        if (!nlohmann::json::accept(commitJSON))
            throw std::runtime_error("The server did not return a valid commit.");

        const auto parsedJson = nlohmann::json::parse(commitJSON);
        std::optional<std::chrono::system_clock::time_point> timestamp;
        try {
            return commitFromJson(parsedJson, timestamp);
        } catch (const std::runtime_error &) {
            const auto message = serverMessage(parsedJson);
            throw std::runtime_error("The server rejected the commit." + (message.empty() ? std::string() : " " + message));
        }
    }

    std::shared_ptr<SysMLv2::REST::Branch> CommunicationService::postBranch(
        boost::uuids::uuid projectId, const std::string &name, boost::uuids::uuid headCommitId) {
        // BranchRequest::serializeToJson of the sysmllib overwrites "@type", so the request is built here.
        nlohmann::json request;
        request["@type"] = "Branch";
        request["name"] = name;
        request["head"] = {{"@id", boost::uuids::to_string(headCommitId)}};

        const auto endpoint = "projects/"+boost::lexical_cast<std::string>(projectId)+"/branches";
        const auto branchJSON = APIImplementation->postCustomRequest(endpoint,request.dump(),BarrierString);
        if (!nlohmann::json::accept(branchJSON))
            throw std::runtime_error("The server did not return a valid branch for " + name + ".");

        auto parsedJson = nlohmann::json::parse(branchJSON);
        if (!parsedJson.is_object() || !parsedJson.contains("@id") || !parsedJson["@id"].is_string()) {
            const auto message = serverMessage(parsedJson);
            throw std::runtime_error("The server did not return a valid branch for " + name + "." + (message.empty() ? std::string() : " " + message));
        }

        // Some servers answer without name or head; the values that were sent are then the ones of the new branch.
        if (!parsedJson.contains("name") || !parsedJson["name"].is_string())
            parsedJson["name"] = name;
        if (!parsedJson.contains("head") || !parsedJson["head"].is_object())
            parsedJson["head"] = request["head"];
        return std::make_shared<SysMLv2::REST::Branch>(parsedJson.dump());
    }

    std::vector<std::shared_ptr<SysMLv2::REST::DigitalTwin>> CommunicationService::getAllDigitalTwinsForProject(boost::uuids::uuid projectId) {
        const auto twinsJSON = APIImplementation->getCustomRequest("projects/"+boost::lexical_cast<std::string>(projectId)+"/twins",BarrierString);
        if (!nlohmann::json::accept(twinsJSON))
            throw std::runtime_error("The server did not return the digital twins of the project.");

        const auto parsedJson = nlohmann::json::parse(twinsJSON);
        if (!parsedJson.is_array())
            throw std::runtime_error("The server did not return a list of digital twins.");

        std::vector<std::shared_ptr<SysMLv2::REST::DigitalTwin>> returnValue;
        returnValue.reserve(parsedJson.size());

        for (const auto& twin : parsedJson)
            returnValue.push_back(std::make_shared<SysMLv2::REST::DigitalTwin>(twin.dump()));

        return returnValue;
    }


    std::vector<std::shared_ptr<SysMLv2::REST::Branch>> CommunicationService::getAllBranchesForProjectWithID(boost::uuids::uuid projectId) {
        auto elements = APIImplementation->getAllBranchesFroProject(boost::lexical_cast<std::string>(projectId), BarrierString);

        std::vector<std::shared_ptr<SysMLv2::REST::Branch>> returnValue;

        for (auto elem : elements)
            returnValue.push_back(dynamic_pointer_cast<SysMLv2::REST::Branch>(elem));

        return returnValue;
    }

    std::vector<std::shared_ptr<KerML::Entities::Element>> CommunicationService::getAllElementsOfCommit(boost::uuids::uuid projectId, boost::uuids::uuid commitId) {
        auto elements = APIImplementation->getAllElementsFromCommit(boost::lexical_cast<std::string>(projectId),boost::lexical_cast<std::string>(commitId), BarrierString);
        std::vector<std::shared_ptr<KerML::Entities::Element>> returnValue;

        for(auto elem : elements)
            returnValue.push_back(dynamic_pointer_cast<KerML::Entities::Element>(elem));

        return returnValue;
    }

    std::shared_ptr<SysMLv2::REST::Project> CommunicationService::postProject(std::string projectName, std::string projectDescription, std::string defaultBranchName, std::string owningGroup) {
        std::shared_ptr<SysMLv2::REST::ProjectRequest> projectRequest = std::make_shared<SysMLv2::REST::ProjectRequest>(projectName, projectDescription, defaultBranchName);
        const auto returnValue = APIImplementation->postProject(projectRequest, BarrierString, owningGroup);
        const auto project = dynamic_pointer_cast<SysMLv2::REST::Project>(returnValue);
        return project;
    }

}
