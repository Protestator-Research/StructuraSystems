//
// Tests of the synchronisation with a server that conforms to the Systems Modeling API v1.1 (REST/HTTP-PSM):
// delta commits, the branch of a commit, branches without head and the pagination of list routes with the Link header.
// The requests are answered by a fake transport, no server is needed.
//

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/string_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <kerml/root/annotations/TextualRepresentation.h>
#include <kerml/root/elements/Element.h>
#include <nlohmann/json.hpp>
#include <sysmlv2/rest/entities/Branch.h>
#include <sysmlv2/rest/entities/Commit.h>
#include <sysmlv2/rest/entities/CommitRequest.h>
#include <sysmlv2/rest/entities/Project.h>
#include <sysmlv2/rest/serialization/SysMLv2Deserializer.h>
#include <sysmlv2/service/online/HttpException.h>

#include <QtTest>

#include "../src/Services/BECommunicationService.h"
#include "../src/Services/CommitDelta.h"
#include "../src/Services/PagedRequest.h"

using namespace StructuraSystems::Client;
using nlohmann::json;

namespace {
    std::string newId() {
        return boost::uuids::to_string(boost::uuids::random_generator()());
    }

    json textual(const std::string &id, const std::string &body, const std::string &language = "sysml") {
        return {{"@id", id}, {"@type", "TextualRepresentation"}, {"language", language}, {"body", body}};
    }

    json packageJson(const std::string &id) {
        return {{"@id", id}, {"@type", "Package"}, {"name", "Pkg"}};
    }

    std::shared_ptr<KerML::Entities::Element> elementOf(const json &source) {
        return std::dynamic_pointer_cast<KerML::Entities::Element>(SysMLv2::SysMLv2Deserializer::deserializeJsonString(source.dump()));
    }

    std::vector<std::shared_ptr<KerML::Entities::Element>> elementsOf(const std::vector<json> &sources) {
        std::vector<std::shared_ptr<KerML::Entities::Element>> result;
        for (const auto &source : sources)
            result.push_back(elementOf(source));
        return result;
    }

    json requestJson(const CommitDelta &delta) {
        return json::parse(buildCommitRequest("test", delta)->serializeToJson());
    }

    /** Answers the GET requests from a table of URLs and remembers what was asked. */
    struct FakeServer {
        std::map<std::string, HttpResponse> Answers;
        std::vector<std::string> Requests;
        std::vector<std::string> Tokens;

        void page(const std::string &url, const json &body, const std::string &nextUrl = {}, long status = 200) {
            HttpResponse response;
            response.Status = status;
            response.Body = body.dump();
            response.Headers = "HTTP/1.1 " + std::to_string(status) + " OK\r\nContent-Type: application/json\r\n";
            if (!nextUrl.empty())
                response.Headers += "Link: <" + nextUrl + ">; rel=\"next\"\r\n";
            response.Headers += "\r\n";
            Answers[url] = response;
        }

        HttpTransport transport() {
            return [this](const std::string &url, const std::string &token) {
                Requests.push_back(url);
                Tokens.push_back(token);
                const auto answer = Answers.find(url);
                if (answer == Answers.end()) {
                    HttpResponse missing;
                    missing.Status = 404;
                    missing.Body = R"({"error":"not found"})";
                    return missing;
                }
                return answer->second;
            };
        }
    };

    const std::string SERVER = "http://server.test:8088";

    json projectJson(const std::string &id, const std::string &name, const std::string &branchId) {
        return {{"@id", id}, {"@type", "Project"}, {"name", name}, {"description", ""}, {"defaultBranch", {{"@id", branchId}}}};
    }

    json branchJson(const std::string &id, const std::string &name, const std::string &headId) {
        json branch = {{"@id", id}, {"@type", "Branch"}, {"name", name}, {"head", headId.empty() ? json(nullptr) : json({{"@id", headId}})}};
        return branch;
    }
}

class OnlineSyncTests : public QObject {
    Q_OBJECT

private slots:
    // ---- Link header

    void nextPageUrlReadsRelNext() {
        const std::string headers = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                                    "Link: <http://h/projects?page[after]=abc>; rel=\"next\"\r\n\r\n";
        QCOMPARE(nextPageUrl(headers).value_or("-"), std::string("http://h/projects?page[after]=abc"));
    }

    void nextPageUrlHandlesSeveralLinksAndCase() {
        const std::string headers = "HTTP/1.1 200 OK\r\nlInK: <http://h/p?page[before]=1>; rel=\"prev\", "
                                    "<http://h/p?page[after]=2>; title=\"a, b\"; REL=next\r\n\r\n";
        QCOMPARE(nextPageUrl(headers).value_or("-"), std::string("http://h/p?page[after]=2"));
    }

    void nextPageUrlHandlesRelListAndSeveralHeaders() {
        const std::string headers = "HTTP/1.1 200 OK\r\nLink: <http://h/a>; rel=\"prev\"\r\n"
                                    "Link: <http://h/b?page[after]=3>; rel=\"last next\"\r\n\r\n";
        QCOMPARE(nextPageUrl(headers).value_or("-"), std::string("http://h/b?page[after]=3"));
    }

    void nextPageUrlIsEmptyWithoutNext() {
        QVERIFY(!nextPageUrl("HTTP/1.1 200 OK\r\n\r\n").has_value());
        QVERIFY(!nextPageUrl("HTTP/1.1 200 OK\r\nLink: <http://h/a>; rel=\"prev\"\r\n\r\n").has_value());
        QVERIFY(!nextPageUrl("").has_value());
        QVERIFY(!nextPageUrl("Link: <http://h/a; rel=next").has_value());
    }

    void nextPageUrlOnlyUsesTheLastResponse() {
        const std::string headers = "HTTP/1.1 100 Continue\r\nLink: <http://h/old>; rel=\"next\"\r\n\r\n"
                                    "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n";
        QVERIFY(!nextPageUrl(headers).has_value());
    }

    void followLinkKeepsTheServerOfTheRequest() {
        // The token must not be sent to the host that is named in a response.
        QCOMPARE(followLink("http://server.test/api/projects", "http://evil.test/x/projects?page[after]=7&page[size]=2").value_or("-"),
                 std::string("http://server.test/api/projects?page[after]=7&page[size]=2"));
        QCOMPARE(followLink("http://server.test/api/projects?page[after]=7", "http://server.test/api/projects?page[after]=9").value_or("-"),
                 std::string("http://server.test/api/projects?page[after]=9"));
        QVERIFY(!followLink("http://server.test/api/projects", "http://server.test/api/projects").has_value());
    }

    // ---- pagination

    void allPagesAreAggregated() {
        FakeServer server;
        server.page(SERVER + "/projects", json::array({1, 2}), "http://other/projects?page[after]=a");
        server.page(SERVER + "/projects?page[after]=a", json::array({3, 4}), "http://other/projects?page[after]=b");
        server.page(SERVER + "/projects?page[after]=b", json::array({5}));

        const auto entries = getAllPages(SERVER + "/projects", "secret", server.transport());
        QCOMPARE(int(entries.size()), 5);
        QCOMPARE(entries.back().get<int>(), 5);
        QCOMPARE(int(server.Requests.size()), 3);
        for (const auto &token : server.Tokens)
            QCOMPARE(token, std::string("secret"));
    }

    void singlePageAndEmptyListStop() {
        FakeServer server;
        server.page(SERVER + "/projects", json::array());
        QVERIFY(getAllPages(SERVER + "/projects", "", server.transport()).empty());
        QCOMPARE(int(server.Requests.size()), 1);
    }

    void repeatedPageUrlEndsTheLoop() {
        FakeServer server;
        server.page(SERVER + "/projects", json::array({1}), "http://x/projects?page[after]=a");
        server.page(SERVER + "/projects?page[after]=a", json::array({2}), "http://x/projects?page[after]=a");
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, getAllPages(SERVER + "/projects", "", server.transport()));
        QCOMPARE(int(server.Requests.size()), 2);
    }

    void errorStatusIsAnHttpException() {
        FakeServer server;
        server.page(SERVER + "/projects", json::array({1}), "http://x/projects?page[after]=a");
        server.Answers[SERVER + "/projects?page[after]=a"].Status = 404;
        server.Answers[SERVER + "/projects?page[after]=a"].Body = R"({"error":"gone"})";
        try {
            getAllPages(SERVER + "/projects", "", server.transport());
            QFAIL("no exception");
        } catch (const SysMLv2::API::EXCEPTIONS::HTTPException &exception) {
            QCOMPARE(exception.httpErrorCode(), 404L);
            QCOMPARE(QString::fromStdString(CommunicationService::describeBackendError(exception)), QStringLiteral("Not found, or you do not have access to it."));
        }
    }

    void listRoutesOfTheServiceFollowTheLinks() {
        const auto projectId = newId(), branchId = newId();
        FakeServer server;
        server.page(SERVER + "/projects", json::array({projectJson(newId(), "A", branchId), projectJson(projectId, "B", branchId)}),
                    "http://x/projects?page[after]=c1");
        server.page(SERVER + "/projects?page[after]=c1", json::array({projectJson(newId(), "C", branchId)}));

        CommunicationService service(SERVER, server.transport());
        service.setBearerToken("tok");
        const auto projects = service.getAllProjects();
        QCOMPARE(int(projects.size()), 3);
        QCOMPARE(QString::fromStdString(projects[2]->getName()), QStringLiteral("C"));
        QCOMPARE(QString::fromStdString(server.Tokens.front()), QStringLiteral("tok"));

        server.Requests.clear();
        server.page(SERVER + "/projects/" + projectId + "/commits", json::array({json({{"@id", newId()}, {"@type", "Commit"}, {"description", "one"}, {"created", "2026-10-18T14:03:00Z"}})}),
                    "http://x/projects/" + projectId + "/commits?page[after]=k");
        server.page(SERVER + "/projects/" + projectId + "/commits?page[after]=k", json::array({json({{"@id", newId()}, {"@type", "Commit"}, {"description", "two"}})}));
        std::map<boost::uuids::uuid, std::chrono::system_clock::time_point> timestamps;
        const auto commits = service.getAllCommitsForProject(boost::uuids::string_generator()(projectId), &timestamps);
        QCOMPARE(int(commits.size()), 2);
        // Only the first commit has a creation time.
        QCOMPARE(int(timestamps.size()), 1);
        QVERIFY(timestamps.count(commits[0]->getId()) == 1);
        QCOMPARE(int(server.Requests.size()), 2);
    }

    void elementsOfACommitAreReadFromAllPages() {
        const auto projectId = newId(), commitId = newId();
        const auto base = SERVER + "/projects/" + projectId + "/commits/" + commitId + "/elements";
        FakeServer server;
        server.page(base, json::array({textual(newId(), "a"), packageJson(newId())}), "http://x/e?page[after]=1");
        server.page(base + "?page[after]=1", json::array({textual(newId(), "b")}));

        CommunicationService service(SERVER, server.transport());
        const auto elements = service.getAllElementsOfCommit(boost::uuids::string_generator()(projectId), boost::uuids::string_generator()(commitId));
        QCOMPARE(int(elements.size()), 3);
    }

    // ---- branches without head

    void branchWithoutHeadYieldsAnEmptyState() {
        const auto projectId = newId(), branchId = newId();
        FakeServer server;
        server.page(SERVER + "/projects/" + projectId + "/branches", json::array({branchJson(branchId, "main", "")}));

        CommunicationService service(SERVER, server.transport());
        const auto state = service.loadBranchState(boost::uuids::string_generator()(projectId), boost::uuids::string_generator()(branchId));
        QVERIFY(state.Branch != nullptr);
        QVERIFY(state.Branch->getHead() == nullptr);
        QVERIFY(state.Commit == nullptr);
        QVERIFY(state.Elements.empty());
        // Neither a commit nor elements are requested for a branch without head.
        QCOMPARE(int(server.Requests.size()), 1);

        // The first commit has an empty base: everything is created.
        const auto delta = computeCommitDelta(state.Elements, elementsOf({textual(newId(), "x")}));
        QCOMPARE(delta.Created, 1);
        QCOMPARE(delta.Updated + delta.Deleted, 0);
    }

    void branchWithHeadLoadsCommitAndElements() {
        const auto projectId = newId(), branchId = newId(), commitId = newId();
        FakeServer server;
        server.page(SERVER + "/projects/" + projectId + "/branches", json::array({branchJson(newId(), "other", ""), branchJson(branchId, "main", commitId)}));
        server.page(SERVER + "/projects/" + projectId + "/commits/" + commitId, json({{"@id", commitId}, {"@type", "Commit"}, {"description", "first"}}));
        server.page(SERVER + "/projects/" + projectId + "/commits/" + commitId + "/elements", json::array({textual(newId(), "a")}));

        CommunicationService service(SERVER, server.transport());
        const auto state = service.loadBranchState(boost::uuids::string_generator()(projectId), boost::uuids::string_generator()(branchId));
        QVERIFY(state.Commit != nullptr);
        QCOMPARE(QString::fromStdString(boost::uuids::to_string(state.Commit->getId())), QString::fromStdString(commitId));
        QCOMPARE(int(state.Elements.size()), 1);
    }

    void unknownBranchIsAnError() {
        const auto projectId = newId();
        FakeServer server;
        server.page(SERVER + "/projects/" + projectId + "/branches", json::array());
        CommunicationService service(SERVER, server.transport());
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, service.loadBranchState(boost::uuids::string_generator()(projectId), boost::uuids::random_generator()()));
    }

    // ---- delta commits

    void deltaContainsCreationUpdateAndDeletion() {
        const auto unchanged = newId(), changed = newId(), removed = newId(), keptPackage = newId(), added = newId();
        const auto base = elementsOf({textual(unchanged, "same"), textual(changed, "old"), textual(removed, "gone"), packageJson(keptPackage)});
        // The package is not shown by the document and missing in it: it must not be deleted.
        const auto document = elementsOf({textual(unchanged, "same"), textual(changed, "new"), textual(added, "fresh")});

        const auto delta = computeCommitDelta(base, document);
        QCOMPARE(delta.Created, 1);
        QCOMPARE(delta.Updated, 1);
        QCOMPARE(delta.Deleted, 1);
        QVERIFY(!delta.empty());

        const auto request = requestJson(delta);
        QCOMPARE(request["@type"].get<std::string>(), std::string("Commit"));
        const auto &changes = request["change"];
        QCOMPARE(int(changes.size()), 3);

        // Creation: payload only, no identity and no @id of the DataVersion.
        QCOMPARE(changes[0]["@type"].get<std::string>(), std::string("DataVersion"));
        QVERIFY(!changes[0].contains("@id"));
        QVERIFY(!changes[0].contains("identity"));
        QCOMPARE(changes[0]["payload"]["@id"].get<std::string>(), added);

        // Update: identity and payload.
        QVERIFY(!changes[1].contains("@id"));
        QCOMPARE(changes[1]["identity"]["@id"].get<std::string>(), changed);
        QCOMPARE(changes[1]["payload"]["body"].get<std::string>(), std::string("new"));

        // Deletion: identity and no payload.
        QVERIFY(!changes[2].contains("@id"));
        QCOMPARE(changes[2]["identity"]["@id"].get<std::string>(), removed);
        QVERIFY(changes[2].contains("payload"));
        QVERIFY(changes[2]["payload"].is_null());
        QCOMPARE(changes[2]["@type"].get<std::string>(), std::string("DataVersion"));
    }

    void unchangedDocumentHasNothingToCommit() {
        const auto first = newId(), second = newId();
        // Both sides are created from the same server data, once as the base and once as the document.
        const auto sources = std::vector<json>{textual(first, "a"), textual(second, "b"), packageJson(newId())};
        const auto delta = computeCommitDelta(elementsOf(sources), elementsOf(sources));
        QVERIFY(delta.empty());
    }

    void editedElementIsAnUpdate() {
        const auto id = newId();
        const auto base = elementsOf({textual(id, "old")});
        const auto document = elementsOf({textual(id, "old")});
        std::dynamic_pointer_cast<KerML::Entities::TextualRepresentation>(document.front())->setBody("changed");
        const auto delta = computeCommitDelta(base, document);
        QCOMPARE(delta.Updated, 1);
        QCOMPARE(delta.Created + delta.Deleted, 0);
    }

    void firstCommitCreatesEverything() {
        auto document = elementsOf({textual(newId(), "a"), textual(newId(), "b")});
        document.push_back(std::make_shared<KerML::Entities::TextualRepresentation>("SysMLv2", "new element"));
        const auto delta = computeCommitDelta({}, document);
        QCOMPARE(delta.Created, 3);
        const auto request = requestJson(delta);
        for (const auto &change : request["change"]) {
            QVERIFY(!change.contains("@id"));
            QVERIFY(!change.contains("identity"));
        }
        QVERIFY(computeCommitDelta({}, {}).empty());
    }

    void commitGoesToTheBranchById() {
        const auto projectId = newId(), branchId = newId();
        QCOMPARE(commitEndpoint(projectId, branchId), "projects/" + projectId + "/commits?branchId=" + branchId);
    }
};

QTEST_APPLESS_MAIN(OnlineSyncTests)
#include "tst_OnlineSync.moc"
