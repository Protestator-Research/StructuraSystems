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
#include <kerml/root/elements/Relationship.h>
#include <nlohmann/json.hpp>
#include <sysmlv2/rest/entities/Commit.h>
#include <sysmlv2/rest/entities/Project.h>
#include <sysmlv2/rest/serialization/SysMLv2Deserializer.h>

#include <QtTest>

#include "../src/Models/Libraries/DocumentWorkspace.h"
#include "../src/ViewModels/CompletionEngine.h"
#include "../src/ViewModels/DocumentModel.h"
#include "../src/Services/DocumentStructure.h"
#include <kerml/root/annotations/TextualRepresentation.h>
#include <algorithm>

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

    /**
     * @brief Tests the complete lifecycle of a legacy document without a root
     * package using simulated server data (Blocks A,B,C) without a root package.
     *
     * Steps:
     * 1. Generate a local root package and its owning memberships
     * 2. Move block C to the beginning of the document
     * 3. Insert a new block D
     * 4. Remove block A
     * 5. Change the language of block C
     * 6. Reload the original commit and restore the initial document state
     * 7. Remove all blocks, ensuring that the empty root package remains
     *
     * Verifies: correct block order, preservation of root package and
     * membership identities, compatibility with the existing commit mechanism without uploading the local document structure to the server.
     */
    void legacyDocumentStructureLifecycle() {
        using namespace KerML::Entities;
        const auto a = newId();
        const auto b = newId();
        const auto c = newId();
        const auto makeElements = [&]() {
            return std::vector<std::shared_ptr<Element>>{
                elementOf(textual(a, "A")), elementOf(textual(b, "B")),
                elementOf(textual(c, "C"))};
        };
        const auto base = makeElements();
        auto commit = std::make_shared<SysMLv2::REST::Commit>(
            json({{"@id", newId()}, {"@type", "Commit"}}).dump());
        DocumentModel document(project(), commit, makeElements());
        const auto ordered = [&]() {
            auto shuffled = document.documentStructure();
            std::reverse(shuffled.begin(), shuffled.end());
            std::vector<std::string> warnings;
            auto result = orderedDocumentBlocks(shuffled, &warnings);
            if (!warnings.empty())
                QTest::qFail("Unexpected document structure warning", __FILE__, __LINE__);
            return result;
        };
        const auto bodies = [&]() {
            QStringList result;
            for (const auto &block : ordered())
                result << QString::fromStdString(block->body());
            return result;
        };

        QCOMPARE(document.rowCount(), 3);
        QCOMPARE(document.documentStructure().size(), std::size_t(7));
        QVERIFY(document.documentStructure().front()->getType() == "Package");
        QCOMPARE(document.documentStructure().front()->ownedRelationship().size(), std::size_t(3));
        QCOMPARE(bodies(), (QStringList{"A", "B", "C"}));
        QVERIFY(!document.modified());
        QVERIFY(document.commitDelta(base).empty());

        const auto rootId = document.documentStructure().front()->getId();
        boost::uuids::uuid cMembershipId;
        {
            const auto initial = ordered();
            QCOMPARE(initial.size(), std::size_t(3));
            QVERIFY(initial[2]->owningRelationship() != nullptr);
            cMembershipId = initial[2]->owningRelationship()->getId();
        }

        document.moveRow(2, 0);
        QCOMPARE(bodies(), (QStringList{"C", "A", "B"}));
        QVERIFY(document.documentStructure().front()->getId() == rootId);
        {
            const auto moved = ordered();
            QCOMPARE(moved.size(), std::size_t(3));
            QVERIFY(moved[0]->owningRelationship() != nullptr);
            QVERIFY(moved[0]->owningRelationship()->getId() == cMembershipId);
        }
        // Moving a row is local only until structural commits are implemented.
        QVERIFY(document.commitDelta(base).empty());

        document.insertElement(1, QStringLiteral("Markdown"));
        document.setBody(1, QStringLiteral("D"));
        QCOMPARE(bodies(), (QStringList{"C", "D", "A", "B"}));
        QCOMPARE(document.documentStructure().size(), std::size_t(9));
        QVERIFY(document.documentStructure().front()->getId() == rootId);
        QCOMPARE(document.commitDelta(base).Created, 1);

        document.removeElement(2);
        QCOMPARE(bodies(), (QStringList{"C", "D", "B"}));
        QCOMPARE(document.documentStructure().size(), std::size_t(7));
        QVERIFY(document.documentStructure().front()->getId() == rootId);
        const auto delta = document.commitDelta(base);
        QCOMPARE(delta.Created, 1);
        QCOMPARE(delta.Deleted, 1);
        QCOMPARE(delta.Updated, 0);

        document.setLanguage(0, QStringLiteral("Markdown"));
        {
            const auto edited = ordered();
            QCOMPARE(edited.size(), std::size_t(3));
            QVERIFY(edited[0]->language() == "Markdown");
        }
        for (const auto &element : base)
            QVERIFY(!element->owner());

        // Replacing the commit reconstructs a fresh graph, without uploading.
        QVERIFY(document.replaceContent(commit, makeElements()));
        QCOMPARE(bodies(), (QStringList{"A", "B", "C"}));
        QVERIFY(!document.modified());
        QVERIFY(document.commitDelta(base).empty());
        while (document.rowCount() > 0)
            document.removeElement(0);
        QCOMPARE(document.documentStructure().size(), std::size_t(1));
        QVERIFY(document.documentStructure().front()->ownedRelationship().empty());
    }

    /**
     * @brief Tests handling of an existing document structure with a root package and owning memberships using simulated server data.
     *
     * Steps:
     * 1. Create a structured document containing blocks A and B
     * 2. Reverse the order of the elements to simulate an unordered server response
     * 3. Load the document and restore the original block order from the root package
     * 4. Move block B to the beginning of the document
     * 5. Verify that the root package UUID is preserved when loading the document
     *    and that the membership UUID of block B is preserved when moving it
     * 6. Ensure that the original document structure remains unchanged
     *
     * Verifies: an existing document structure is correctly interpreted and locally updated without modifying the original structure or
     * generating structural changes for a server commit.
     */
    void structuredDocumentKeepsRootAndMemberships() {
        using namespace KerML::Entities;
        std::vector<std::shared_ptr<TextualRepresentation>> blocks{
            std::make_shared<TextualRepresentation>("Markdown", "A"),
            std::make_shared<TextualRepresentation>("Markdown", "B")};
        auto elements = createDocumentStructure("Structured", blocks);
        const auto rootId = elements.front()->getId();
        const auto membershipId = elements.front()->ownedRelationship().at(1)->getId();
        std::reverse(elements.begin(), elements.end());
        const auto commit = std::make_shared<SysMLv2::REST::Commit>(
            json({{"@id", newId()}, {"@type", "Commit"}}).dump());
        DocumentModel document(project(), commit, elements);
        QVERIFY(document.documentStructure().front()->getId() == rootId);
        QCOMPARE(document.data(document.index(0), DocumentModel::BodyRole).toString(), QStringLiteral("A"));
        document.moveRow(1, 0);
        const auto ordered = orderedDocumentBlocks(document.documentStructure());
        QCOMPARE(ordered.size(), std::size_t(2));
        QVERIFY(ordered[0]->getId() == blocks[1]->getId());
        QVERIFY(ordered[0]->owningRelationship() != nullptr);
        QVERIFY(ordered[0]->owningRelationship()->getId() == membershipId);
        // The downloaded graph remains unchanged; only its private copy is reordered.
        const auto original = orderedDocumentBlocks(elements);
        QCOMPARE(original.size(), std::size_t(2));
        QVERIFY(original[0]->getId() == blocks[0]->getId());
        QVERIFY(document.commitDelta(elements).empty());
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

    
    void documentStructureRoundTrip() {
        using namespace KerML::Entities;
        using namespace StructuraSystems::Client;

        std::vector<std::shared_ptr<TextualRepresentation>> blocks = {
            std::make_shared<TextualRepresentation>("Markdown", "# Introduction"),
            std::make_shared<TextualRepresentation>("SysMLv2", "part example;"),
            std::make_shared<TextualRepresentation>("Markdown", "# Conclusion")
        };

        // 1. Create the initial document structure.
        auto elements = createDocumentStructure("TestDocument", blocks);

        QCOMPARE(elements.size(), 1 + 2 * blocks.size());

        // 2. Verify the serialized document structure.
        json serialized = json::array();

        for (const auto& element : elements) {
            QVERIFY(element != nullptr);
            serialized.push_back(
                json::parse(element->serializeToJson())
            );
        }

        const auto& package = serialized.at(0);

        QVERIFY(package.at("@type") == "Package");
        QVERIFY(package.at("declaredName") == "TestDocument");

        // The root package must not have an owner.
        QVERIFY(!package.contains("owner") ||
                package.at("owner").is_null());

        const auto& relationships = package.at("ownedRelationship");

        QVERIFY(relationships.is_array());
        QCOMPARE(relationships.size(), blocks.size());

        // Verify every membership and its corresponding block.
        for (std::size_t i = 0; i < blocks.size(); ++i) {
            const auto& membership = serialized.at(1 + 2 * i);
            const auto& child = serialized.at(2 + 2 * i);

            QVERIFY(membership.at("@type") == "OwningMembership");
            QVERIFY(child.at("@type") == "TextualRepresentation");

            // The root package references the membership in document order.
            QVERIFY(membership.at("@id") ==
                    relationships.at(i).at("@id"));

            // The membership belongs to the root package.
            QVERIFY(membership.at("owningRelatedElement").at("@id") ==
                    package.at("@id"));

            // The membership owns exactly one document block.
            QCOMPARE(membership.at("ownedRelatedElement").size(),
                    std::size_t(1));

            QVERIFY(membership.at("ownedRelatedElement").at(0).at("@id") ==
                    child.at("@id"));

            // The document block references its package and membership.
            QVERIFY(child.at("owner").at("@id") ==
                    package.at("@id"));

            QVERIFY(child.at("owningRelationship").at("@id") ==
                    membership.at("@id"));

            // The original block identity and content are preserved.
            QVERIFY(child.at("@id") ==
                    boost::uuids::to_string(blocks[i]->getId()));

            QVERIFY(child.at("body") == blocks[i]->body());
            QVERIFY(child.at("language") == blocks[i]->language());
        }

        // 3. Verify the original in-memory structure.
        std::reverse(elements.begin(), elements.end());

        std::vector<std::string> warnings;
        const auto ordered = orderedDocumentBlocks(elements, &warnings);

        QCOMPARE(ordered.size(), blocks.size());

        for (std::size_t i = 0; i < blocks.size(); ++i) {
            QVERIFY(ordered[i]->getId() == blocks[i]->getId());
            QVERIFY(ordered[i]->body() == blocks[i]->body());
            QVERIFY(ordered[i]->language() == blocks[i]->language());

            // The original block objects must remain unchanged.
            QVERIFY(!blocks[i]->owner());
            QVERIFY(!blocks[i]->owningRelationship());
        }

        QVERIFY(warnings.empty());

        // 4. Simulate the server returning the elements in a different order.
        // Reverse the outer array, not the package's relationship references.
        std::reverse(serialized.begin(), serialized.end());

        // 5. Deserialize the simulated server response using SysMLLib.
        std::vector<std::shared_ptr<Element>> downloadedElements;

        const auto entities =
            SysMLv2::SysMLv2Deserializer::deserializeJsonArray(
                serialized.dump()
            );

        for (const auto& entity : entities) {
            auto element = std::dynamic_pointer_cast<Element>(entity);

            QVERIFY(element != nullptr);
            downloadedElements.push_back(element);
        }

        QCOMPARE(downloadedElements.size(), elements.size());

        // 6. Reconstruct the document order after deserialization.
        warnings.clear();

        const auto restored =
            orderedDocumentBlocks(downloadedElements, &warnings);

        QCOMPARE(restored.size(), blocks.size());

        for (std::size_t i = 0; i < blocks.size(); ++i) {
            QVERIFY(restored[i]->getId() == blocks[i]->getId());
            QVERIFY(restored[i]->body() == blocks[i]->body());
            QVERIFY(restored[i]->language() == blocks[i]->language());

            // Ownership must survive serialization and deserialization.
            QVERIFY(restored[i]->owner() != nullptr);
            QVERIFY(restored[i]->owningRelationship() != nullptr);

            // These must be freshly deserialized objects.
            QVERIFY(restored[i] != ordered[i]);
        }

        QVERIFY(warnings.empty());
    }

};

QTEST_MAIN(OnlineDocumentTests)
#include "tst_OnlineDocument.moc"
