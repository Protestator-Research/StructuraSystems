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

#include "../src/Models/Libraries/DocumentWorkspace.h"
#include "../src/ViewModels/CompletionEngine.h"
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

    /** A workspace whose standard library is one small package, so that parsing stays fast. */
    std::shared_ptr<DocumentWorkspace> smallWorkspace() {
        auto library = std::make_shared<std::vector<StandardSource>>();
        library->push_back({"Lib.sysml", "library package Lib { part def Base; attribute def Num; }", false});
        return std::make_shared<DocumentWorkspace>(nullptr, QString(), library);
    }

    const NameEntry *findName(const NameIndex &index, const QString &qualifiedName) {
        for (const auto &entry : index.entries()) {
            if (entry.QualifiedName == qualifiedName)
                return &entry;
        }
        return nullptr;
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

    void nameIndexHasLibraryAndDocumentNames() {
        const auto workspace = smallWorkspace();
        const auto inputs = QList<ParseInput>{{0, "SysMLv2", "package P { part def A; part def B :> A; part x : A; }"}};
        const auto result = DocumentModel::runParser(inputs, "Test", workspace);
        QVERIFY(result.Names != nullptr);
        const auto &index = *result.Names;

        const auto *a = findName(index, "P::A");
        QVERIFY(a != nullptr);
        QVERIFY(a->IsType);
        QVERIFY(!a->IsFeature);
        QVERIFY(a->InDocument);
        QCOMPARE(a->Owner, QStringLiteral("P"));
        QCOMPARE(a->Name, QStringLiteral("A"));
        QVERIFY(a->Type.contains(QStringLiteral("Definition")));

        const auto *x = findName(index, "P::x");
        QVERIFY(x != nullptr);
        QVERIFY(x->IsFeature);
        QVERIFY(!x->IsType);

        const auto *p = findName(index, "P");
        QVERIFY(p != nullptr);
        QVERIFY(p->IsNamespace);
        QVERIFY(p->Owner.isEmpty());
        QCOMPARE(index.members(QStringLiteral("P")).size(), 3);

        // The library, from the standard library of the workspace.
        const auto *base = findName(index, "Lib::Base");
        QVERIFY(base != nullptr);
        QVERIFY(base->IsType);
        QVERIFY(!base->InDocument);
        QVERIFY(findName(index, "Lib::Num") != nullptr);

        // It feeds the completion: types after the colon, members after the qualifier.
        QStringList types;
        for (const auto &item : CompletionEngine::complete(QStringLiteral("part y : "), 9, QStringLiteral("SysMLv2"), index))
            types.append(item.InsertText);
        QVERIFY(types.contains(QStringLiteral("Lib::Base")));
        QVERIFY(types.contains(QStringLiteral("P::A")));
        QVERIFY(!types.contains(QStringLiteral("P::x")));
        QStringList members;
        for (const auto &item : CompletionEngine::complete(QStringLiteral("Lib::"), 5, QStringLiteral("SysMLv2"), index))
            members.append(item.Label);
        QCOMPARE(members, QStringList({"Base", "Num"}));
    }

    void nameIndexFollowsTheDocument() {
        const auto workspace = smallWorkspace();
        auto inputs = QList<ParseInput>{{0, "SysMLv2", "package P { part def A; }"}};
        const auto first = DocumentModel::runParser(inputs, "Test", workspace);
        QVERIFY(findName(*first.Names, "P::A") != nullptr);

        inputs[0].Body = "package Q { part def C; }";
        const auto second = DocumentModel::runParser(inputs, "Test", workspace);
        // The old snapshot stays what it was, the new one has the new names and still the library.
        QVERIFY(findName(*first.Names, "P::A") != nullptr);
        QVERIFY(findName(*second.Names, "P::A") == nullptr);
        QVERIFY(findName(*second.Names, "Q::C") != nullptr);
        QVERIFY(findName(*second.Names, "Lib::Base") != nullptr);

        // A block with syntax errors has no model: only the library is left.
        inputs[0].Body = "package {{{";
        const auto broken = DocumentModel::runParser(inputs, "Test", workspace);
        QVERIFY(findName(*broken.Names, "Q::C") == nullptr);
        QVERIFY(findName(*broken.Names, "Lib::Base") != nullptr);
    }

    void nameIndexFollowsPublicImports() {
        // ISQ of the standard library owns hardly anything: `ISQ::mass` comes from `public import ISQBase::*;`.
        auto library = std::make_shared<std::vector<StandardSource>>();
        library->push_back({"Base.sysml", "library package Base { attribute def MassValue; attribute mass : MassValue; }", false});
        library->push_back({"Hidden.sysml", "library package Hidden { attribute def Secret; }", false});
        library->push_back({"Front.sysml", "library package Front { public import Base::*; private import Hidden::*; attribute def Own; }", false});
        const auto workspace = std::make_shared<DocumentWorkspace>(nullptr, QString(), library);
        const auto inputs = QList<ParseInput>{{0, "SysMLv2", "package P { part def A; }"}};
        const auto result = DocumentModel::runParser(inputs, "Test", workspace);
        QVERIFY(result.Names != nullptr);

        const auto *front = findName(*result.Names, "Front");
        QVERIFY(front != nullptr);
        QCOMPARE(front->Exports, QStringList{QStringLiteral("Base")});

        QStringList members;
        for (const auto &item : CompletionEngine::complete(QStringLiteral("part x : Front::"), 16, QStringLiteral("SysMLv2"), *result.Names))
            members.append(item.Label);
        QVERIFY(members.contains(QStringLiteral("Own")));
        QVERIFY(members.contains(QStringLiteral("MassValue")));
        QVERIFY(!members.contains(QStringLiteral("Secret")));
    }

    void documentKeepsTheNamesOfTheLastParse() {
        DocumentModel document(project(), nullptr, std::vector<std::shared_ptr<KerML::Entities::Element>>{});
        QVERIFY(document.nameIndex() == nullptr);
        const auto inputs = QList<ParseInput>{{0, "SysMLv2", "package P { part def A; }"}};
        document.applyParseResult(DocumentModel::runParser(inputs, "Test", smallWorkspace()));
        QVERIFY(document.nameIndex() != nullptr);
        QVERIFY(findName(*document.nameIndex(), "P::A") != nullptr);
    }
};

QTEST_MAIN(OnlineDocumentTests)
#include "tst_OnlineDocument.moc"
