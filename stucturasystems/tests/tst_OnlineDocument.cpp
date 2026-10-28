//
// Tests of the online DocumentModel against the commits of the SysML v2 API: a project without commits is opened as an empty document,
// and opening a document and committing it does not change the server.
//

#include <memory>
#include <string>
#include <vector>

#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <kerml/root/elements/Element.h>
#include <nlohmann/json.hpp>
#include <sysmlv2/rest/entities/Commit.h>
#include <sysmlv2/rest/entities/Project.h>
#include <sysmlv2/rest/serialization/SysMLv2Deserializer.h>

#include <QtTest>

#include "../src/ViewModels/DocumentModel.h"

using namespace StructuraSystems::Client;
using nlohmann::json;

namespace {
    std::string newId() {
        return boost::uuids::to_string(boost::uuids::random_generator()());
    }

    json textual(const std::string &id, const std::string &body) {
        return {{"@id", id}, {"@type", "TextualRepresentation"}, {"language", "sysml"}, {"body", body}};
    }

    std::shared_ptr<KerML::Entities::Element> elementOf(const json &source) {
        return std::dynamic_pointer_cast<KerML::Entities::Element>(SysMLv2::SysMLv2Deserializer::deserializeJsonString(source.dump()));
    }

    std::vector<std::shared_ptr<KerML::Entities::Element>> serverElements(const std::vector<std::string> &textualIds, const std::string &packageId) {
        std::vector<std::shared_ptr<KerML::Entities::Element>> elements;
        for (const auto &id : textualIds)
            elements.push_back(elementOf(textual(id, "body of " + id)));
        elements.push_back(elementOf({{"@id", packageId}, {"@type", "Package"}, {"name", "Pkg"}}));
        return elements;
    }

    std::shared_ptr<SysMLv2::REST::Project> project() {
        const json source = {{"@id", newId()}, {"@type", "Project"}, {"name", "New"}, {"description", ""}, {"defaultBranch", {{"@id", newId()}}}};
        return std::dynamic_pointer_cast<SysMLv2::REST::Project>(SysMLv2::SysMLv2Deserializer::deserializeJsonString(source.dump()));
    }
}

class OnlineDocumentTests : public QObject {
    Q_OBJECT

private slots:
    void projectWithoutCommitsOpensAsEmptyDocument() {
        DocumentModel document(project(), nullptr, std::vector<std::shared_ptr<KerML::Entities::Element>>{});
        QVERIFY(document.isOnline());
        QCOMPARE(document.rowCount(), 0);
        QVERIFY(document.commitId().isEmpty());
        QVERIFY(!document.modified());

        document.setBranch(QStringLiteral("b"), QStringLiteral("main"));
        QCOMPARE(document.branchName(), QStringLiteral("main"));

        // Nothing to commit while it is empty ...
        QVERIFY(document.commitDelta({}).empty());
        // ... and the first element is a creation against the empty base.
        document.insertElement(0, QStringLiteral("SysMLv2"));
        QCOMPARE(document.rowCount(), 1);
        const auto delta = document.commitDelta({});
        QCOMPARE(delta.Created, 1);
        QCOMPARE(delta.Updated + delta.Deleted, 0);
    }

    void openedDocumentHasNothingToCommit() {
        const auto ids = std::vector<std::string>{newId(), newId()};
        const auto packageId = newId();
        // The base of the commit is a second download of the same head.
        const auto base = serverElements(ids, packageId);
        DocumentModel document(project(), std::make_shared<SysMLv2::REST::Commit>(json({{"@id", newId()}, {"@type", "Commit"}}).dump()), serverElements(ids, packageId));
        QCOMPARE(document.rowCount(), 2);
        QVERIFY(document.commitDelta(base).empty());
    }

    void removedRowIsTheOnlyDeletion() {
        const auto ids = std::vector<std::string>{newId(), newId()};
        const auto packageId = newId();
        const auto base = serverElements(ids, packageId);
        DocumentModel document(project(), std::make_shared<SysMLv2::REST::Commit>(json({{"@id", newId()}, {"@type", "Commit"}}).dump()), serverElements(ids, packageId));
        document.removeElement(0);
        const auto delta = document.commitDelta(base);
        // The package is kept by the document; only the removed textual representation is deleted.
        QCOMPARE(delta.Deleted, 1);
        QCOMPARE(delta.Created + delta.Updated, 0);
    }

    void editedRowIsAnUpdate() {
        const auto ids = std::vector<std::string>{newId()};
        const auto packageId = newId();
        const auto base = serverElements(ids, packageId);
        DocumentModel document(project(), std::make_shared<SysMLv2::REST::Commit>(json({{"@id", newId()}, {"@type", "Commit"}}).dump()), serverElements(ids, packageId));
        document.setBody(0, QStringLiteral("part x;"));
        const auto delta = document.commitDelta(base);
        QCOMPARE(delta.Updated, 1);
        QCOMPARE(delta.Created + delta.Deleted, 0);
    }
};

QTEST_MAIN(OnlineDocumentTests)
#include "tst_OnlineDocument.moc"
