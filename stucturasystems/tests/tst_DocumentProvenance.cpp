//
// Tests that the results of a parser run stay attached to the card (TextualRepresentation) they came from, identified by its element id,
// also when cards are moved, inserted or removed while the parser runs or after the result was applied.
//

#include <functional>
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
#include "../src/ViewModels/DocumentModel.h"
#include "../src/ViewModels/ProblemListModel.h"

using namespace StructuraSystems::Client;
using nlohmann::json;

namespace {
    std::string newId() {
        return boost::uuids::to_string(boost::uuids::random_generator()());
    }

    std::shared_ptr<KerML::Entities::Element> textualElement(const std::string &id, const std::string &body) {
        const json source = {{"@id", id}, {"@type", "TextualRepresentation"}, {"language", "sysml"}, {"body", body}};
        return std::dynamic_pointer_cast<KerML::Entities::Element>(SysMLv2::SysMLv2Deserializer::deserializeJsonString(source.dump()));
    }

    std::shared_ptr<SysMLv2::REST::Project> project() {
        const json source = {{"@id", newId()}, {"@type", "Project"}, {"name", "New"}, {"description", ""}, {"defaultBranch", {{"@id", newId()}}}};
        return std::dynamic_pointer_cast<SysMLv2::REST::Project>(SysMLv2::SysMLv2Deserializer::deserializeJsonString(source.dump()));
    }

    /** A workspace whose standard library is one small package, so that parsing stays fast. */
    std::shared_ptr<DocumentWorkspace> smallWorkspace() {
        auto library = std::make_shared<std::vector<StandardSource>>();
        library->push_back({"Lib.sysml", "library package Lib { part def Base; }", false});
        return std::make_shared<DocumentWorkspace>(nullptr, QString(), library);
    }

    const std::string GoodA = "package A { part def X; part def Y; part def Z; }";   // 3 parts: a bigger tree than GoodC
    const std::string Broken = "package {{{";
    const std::string GoodC = "package C { part def W; }";

    struct Fixture {
        std::vector<std::string> Ids;
        std::unique_ptr<DocumentModel> Document;

        /** One card per body; the id of card i is Ids[i]. */
        explicit Fixture(const std::vector<std::string> &bodies) {
            std::vector<std::shared_ptr<KerML::Entities::Element>> elements;
            for (const auto &body : bodies) {
                Ids.push_back(newId());
                elements.push_back(textualElement(Ids.back(), body));
            }
            Document = std::make_unique<DocumentModel>(project(), nullptr, std::move(elements));
        }

        QString id(size_t i) const { return QString::fromStdString(Ids[i]); }

        int rowOf(size_t i) const {
            for (int row = 0; row < Document->rowCount(); row++) {
                if (Document->data(Document->index(row), DocumentModel::ElementIdRole).toString() == id(i))
                    return row;
            }
            return -1;
        }

        int problemCount(int row) const { return Document->data(Document->index(row), DocumentModel::ProblemCountRole).toInt(); }
        int elementCount(int row) const { return Document->data(Document->index(row), DocumentModel::ElementCountRole).toInt(); }
    };

    bool announcesProblemCount(const QList<QVariant> &dataChangedArguments) {
        for (const auto &role : dataChangedArguments.at(2).toList()) {
            if (role.toInt() == DocumentModel::ProblemCountRole)
                return true;
        }
        return false;
    }

    bool sameProblem(const Problem &a, const Problem &b) {
        return a.Message == b.Message && a.Line == b.Line && a.Column == b.Column && a.DocumentTitle == b.DocumentTitle &&
               a.Row == b.Row && a.Severity == b.Severity && a.ElementId == b.ElementId;
    }

    /** The result as if it came from inputs built by hand: it is applied whatever the document looks like now. */
    ParseResult withoutProvenance(ParseResult result) {
        result.HasProvenance = false;
        return result;
    }

    int problemsOfRow(const QList<Problem> &problems, int row) {
        int count = 0;
        for (const auto &problem : problems)
            count += problem.Row == row;
        return count;
    }
}

class DocumentProvenanceTests : public QObject {
    Q_OBJECT

private slots:
    void cardMovedWhileParsing() {
        Fixture f({GoodA, Broken, GoodC});
        const auto workspace = smallWorkspace();
        const auto result = DocumentModel::runParser(f.Document->parseInputs(), "Test", workspace);

        f.Document->moveRow(1, 0);   // the broken card is now first
        f.Document->applyParseResult(result);

        QCOMPARE(f.rowOf(1), 0);
        QVERIFY(f.problemCount(0) > 0);
        QCOMPARE(f.elementCount(0), 0);
        QCOMPARE(f.problemCount(1), 0);
        QCOMPARE(f.problemCount(2), 0);
        QVERIFY(f.elementCount(1) > f.elementCount(2));   // card A (3 parts) at 1, card C (1 part) at 2
        QVERIFY(f.elementCount(2) > 0);
        for (const auto &problem : f.Document->problems()) {
            QCOMPARE(problem.Row, 0);
            QCOMPARE(problem.ElementId, f.id(1));
        }
        QVERIFY(!f.Document->problems().isEmpty());
    }

    void cardRemovedWhileParsing() {
        Fixture f({Broken, GoodA, GoodC});
        const auto result = withoutProvenance(DocumentModel::runParser(f.Document->parseInputs(), "Test", smallWorkspace()));
        QVERIFY(!result.Problems.isEmpty());

        f.Document->removeElement(0);   // the card with the problems
        f.Document->applyParseResult(result);

        QCOMPARE(f.Document->rowCount(), 2);
        QVERIFY(f.Document->problems().isEmpty());
        QCOMPARE(f.problemCount(0), 0);
        QCOMPARE(f.problemCount(1), 0);
        QVERIFY(f.elementCount(0) > f.elementCount(1));   // A then C, still with their own trees
        QVERIFY(f.elementCount(1) > 0);
    }

    void cardRemovedKeepsOthers() {
        Fixture f({GoodA, Broken, GoodC});
        const auto result = withoutProvenance(DocumentModel::runParser(f.Document->parseInputs(), "Test", smallWorkspace()));

        f.Document->removeElement(0);   // a card without problems
        f.Document->applyParseResult(result);

        QCOMPARE(f.Document->rowCount(), 2);
        QVERIFY(f.problemCount(0) > 0);   // the broken card moved up to row 0
        QCOMPARE(f.problemCount(1), 0);
        QCOMPARE(f.elementCount(0), 0);
        QVERIFY(f.elementCount(1) > 0);
        for (const auto &problem : f.Document->problems())
            QCOMPARE(problem.Row, 0);
    }

    void cardInsertedWhileParsing() {
        Fixture f({Broken, GoodA});
        const auto result = DocumentModel::runParser(f.Document->parseInputs(), "Test", smallWorkspace());

        f.Document->insertElement(0, QStringLiteral("Markdown"));   // the broken card moves to row 1 (a Markdown card does not make the result stale)
        QVERIFY(f.Document->applyParseResult(result));

        QCOMPARE(f.Document->rowCount(), 3);
        QCOMPARE(f.problemCount(0), 0);
        QCOMPARE(f.elementCount(0), 0);   // the new card: no problems, empty tree
        QVERIFY(f.problemCount(1) > 0);
        QCOMPARE(f.elementCount(1), 0);
        QCOMPARE(f.problemCount(2), 0);
        QVERIFY(f.elementCount(2) > 0);
        for (const auto &problem : f.Document->problems())
            QCOMPARE(problem.Row, 1);
    }

    void editsAfterApplyKeepProblemsOnTheirCard() {
        Fixture f({GoodA, Broken, GoodC});
        f.Document->applyParseResult(DocumentModel::runParser(f.Document->parseInputs(), "Test", smallWorkspace()));
        const int total = int(f.Document->problems().size());
        QVERIFY(total > 0);
        QCOMPARE(problemsOfRow(f.Document->problems(), 1), total);
        QSignalSpy changed(f.Document.get(), &DocumentModel::problemRowsChanged);
        QSignalSpy parsed(f.Document.get(), &DocumentModel::problemsChanged);   // must stay silent: it opens the problem list

        f.Document->moveRow(1, 2);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(problemsOfRow(f.Document->problems(), 2), total);
        QCOMPARE(f.problemCount(2), total);
        QCOMPARE(f.problemCount(1), 0);

        f.Document->insertElement(0, QStringLiteral("SysMLv2"));   // broken card: row 3
        QCOMPARE(changed.count(), 2);
        QCOMPARE(problemsOfRow(f.Document->problems(), 3), total);
        QCOMPARE(f.problemCount(3), total);
        QCOMPARE(f.problemCount(2), 0);

        f.Document->removeElement(1);   // a card in front of it: row 2
        QCOMPARE(changed.count(), 3);
        QCOMPARE(problemsOfRow(f.Document->problems(), 2), total);
        QCOMPARE(f.problemCount(2), total);
        QCOMPARE(f.rowOf(1), 2);

        f.Document->insertElement(3, QStringLiteral("SysMLv2"));   // a card behind it: nothing to follow
        QCOMPARE(changed.count(), 3);
        QCOMPARE(problemsOfRow(f.Document->problems(), 2), total);

        f.Document->removeElement(2);   // the broken card itself
        QCOMPARE(changed.count(), 4);
        QVERIFY(f.Document->problems().isEmpty());
        for (int row = 0; row < f.Document->rowCount(); row++)
            QCOMPARE(f.problemCount(row), 0);
        QCOMPARE(parsed.count(), 0);
    }

    void swappedProblemCardsSwapCountsAndRows() {
        Fixture f({Broken, "package }}} }}}"});
        f.Document->applyParseResult(DocumentModel::runParser(f.Document->parseInputs(), "Test", smallWorkspace()));
        const int first = f.problemCount(0);
        const int second = f.problemCount(1);
        QVERIFY(first > 0 && second > 0);
        QSignalSpy rows(f.Document.get(), &DocumentModel::problemRowsChanged);
        QSignalSpy parsed(f.Document.get(), &DocumentModel::problemsChanged);

        f.Document->moveRow(0, 1);

        QCOMPARE(f.problemCount(0), second);
        QCOMPARE(f.problemCount(1), first);
        QCOMPARE(problemsOfRow(f.Document->problems(), 0), second);
        QCOMPARE(problemsOfRow(f.Document->problems(), 1), first);
        for (const auto &problem : f.Document->problems())
            QCOMPARE(problem.ElementId, f.id(size_t(problem.Row == 0 ? 1 : 0)));
        QCOMPARE(rows.count(), 1);
        QCOMPARE(parsed.count(), 0);
    }

    void moveWithoutEffectOnProblemCardsIsSilent() {
        Fixture f({GoodA, GoodC, Broken});
        f.Document->applyParseResult(DocumentModel::runParser(f.Document->parseInputs(), "Test", smallWorkspace()));
        QVERIFY(f.problemCount(2) > 0);
        QSignalSpy rows(f.Document.get(), &DocumentModel::problemRowsChanged);
        QSignalSpy parsed(f.Document.get(), &DocumentModel::problemsChanged);
        QSignalSpy data(f.Document.get(), &DocumentModel::dataChanged);

        f.Document->moveRow(0, 1);   // the two good cards swap, the broken one stays at row 2

        QCOMPARE(rows.count(), 0);
        QCOMPARE(parsed.count(), 0);
        for (const auto &arguments : data)
            QVERIFY(!announcesProblemCount(arguments));
    }

    void problemCountRoleIsAnnouncedForTheAffectedRows() {
        Fixture f({GoodA, Broken, GoodC});
        const auto result = DocumentModel::runParser(f.Document->parseInputs(), "Test", smallWorkspace());
        f.Document->moveRow(1, 0);   // while parsing
        QSignalSpy data(f.Document.get(), &DocumentModel::dataChanged);

        f.Document->applyParseResult(result);

        QSet<int> announced;
        for (const auto &arguments : data) {
            if (announcesProblemCount(arguments))
                announced.insert(arguments.at(0).value<QModelIndex>().row());
        }
        QCOMPARE(announced, (QSet<int>{0}));   // only the broken card, now at row 0

        // Later moves carry the count with the row (rowsMoved), so no dataChanged is needed for it.
        QSignalSpy moved(f.Document.get(), &DocumentModel::rowsMoved);
        QSignalSpy later(f.Document.get(), &DocumentModel::dataChanged);
        f.Document->moveRow(0, 2);
        QCOMPARE(moved.count(), 1);
        QCOMPARE(f.problemCount(2), result.Problems.size());
        QCOMPARE(f.problemCount(0), 0);
        QCOMPARE(later.count(), 0);
    }

    void problemListFollowsInPlace() {
        const auto make = [](const char *message, int row, const QString &id) {
            Problem problem;
            problem.Message = message;
            problem.Row = row;
            problem.ElementId = id;
            return problem;
        };
        ProblemListModel list;
        list.setProblems({make("a", 0, "A"), make("b", 1, "B"), make("c", 2, "C")});
        QSignalSpy reset(&list, &QAbstractItemModel::modelReset);
        QSignalSpy removed(&list, &QAbstractItemModel::rowsRemoved);
        QSignalSpy changed(&list, &QAbstractItemModel::dataChanged);

        list.updateProblems({make("a", 0, "A"), make("c", 1, "C")});   // card B removed, C moved up

        QCOMPARE(reset.count(), 0);
        QCOMPARE(removed.count(), 1);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(list.count(), 2);
        QCOMPARE(list.data(list.index(0), ProblemListModel::RowRole).toInt(), 0);
        QCOMPARE(list.data(list.index(1), ProblemListModel::MessageRole).toString(), QStringLiteral("c"));
        QCOMPARE(list.data(list.index(1), ProblemListModel::RowRole).toInt(), 1);
    }

    void documentLevelProblemsSurvive() {
        Fixture f({GoodA, Broken});
        Problem general;
        general.Message = "general";
        general.DocumentTitle = "Test";
        auto result = DocumentModel::runParser(f.Document->parseInputs(), "Test", smallWorkspace());
        result.Problems.append(general);
        const int rowProblems = int(result.Problems.size()) - 1;
        QVERIFY(rowProblems > 0);

        f.Document->moveRow(0, 1);
        f.Document->applyParseResult(result);
        f.Document->moveRow(1, 0);
        f.Document->insertElement(0, QStringLiteral("SysMLv2"));
        f.Document->removeElement(0);

        QCOMPARE(f.Document->problems().size(), rowProblems + 1);
        const auto problems = f.Document->problems();
        const auto doc = std::find_if(problems.begin(), problems.end(), [](const Problem &p) { return p.Message == "general"; });
        QVERIFY(doc != problems.end());
        QCOMPARE(doc->Row, -1);
        QVERIFY(doc->ElementId.isEmpty());
        QCOMPARE(problemsOfRow(problems, 1), rowProblems);   // the broken card is at row 1 again
    }

    // ---- results computed from content that changed meanwhile are stale as a whole ----

    void staleSetBody() {
        checkStale([](Fixture &f) { f.Document->setBody(2, QStringLiteral("package C { part def Other; }")); }, false);
        checkStale([](Fixture &f) { f.Document->setBody(2, QStringLiteral("package C { part def Other; }")); }, true);
    }

    void staleSetLanguage() {
        checkStale([](Fixture &f) { f.Document->setLanguage(0, QStringLiteral("KerML")); }, true);
        checkStale([](Fixture &f) { f.Document->setLanguage(2, QStringLiteral("Markdown")); }, true);   // code card becomes text
        checkStale([](Fixture &f) { f.Document->setLanguage(0, QStringLiteral("Markdown")); }, false);
    }

    void staleInsertAndRemoveCodeCards() {
        checkStale([](Fixture &f) { f.Document->insertElement(1, QStringLiteral("SysMLv2")); }, true);
        checkStale([](Fixture &f) { f.Document->insertElement(3, QStringLiteral("KerML")); }, false);
        checkStale([](Fixture &f) { f.Document->removeElement(0); }, true);
        checkStale([](Fixture &f) { f.Document->removeElement(1); }, false);
    }

    void staleRemoveKerMLCard() {
        Fixture f({GoodA, GoodC});
        f.Document->setLanguage(1, QStringLiteral("KerML"));
        const auto result = DocumentModel::runParser(f.Document->parseInputs(), "Test", smallWorkspace());
        f.Document->removeElement(1);
        QVERIFY(!f.Document->applyParseResult(result));
        QCOMPARE(f.elementCount(0), 0);
    }

    void markdownAndOrderDoNotMakeStale() {
        Fixture f({GoodA, Broken, GoodC});
        f.Document->insertElement(1, QStringLiteral("Markdown"));
        f.Document->setBody(1, QStringLiteral("# Heading"));
        const auto result = DocumentModel::runParser(f.Document->parseInputs(), "Test", smallWorkspace());
        QVERIFY(result.HasProvenance);

        f.Document->setBody(1, QStringLiteral("# Other heading"));   // edit of a Markdown card
        f.Document->insertElement(0, QStringLiteral("Markdown"));
        f.Document->insertElement(0, QStringLiteral("YAML"));
        f.Document->removeElement(0);
        f.Document->moveRow(1, 4);
        f.Document->moveRow(3, 0);
        QVERIFY(f.Document->applyParseResult(result));

        QVERIFY(!f.Document->problems().isEmpty());
        QVERIFY(f.problemCount(f.rowOf(1)) > 0);
        QVERIFY(f.elementCount(f.rowOf(0)) > 0);
        QVERIFY(f.elementCount(f.rowOf(2)) > 0);
    }

    void staleAfterReplaceContentWithEqualContent() {
        const std::vector<std::string> bodies = {GoodA, Broken, GoodC};
        Fixture f(bodies);
        f.Document->applyParseResult(DocumentModel::runParser(f.Document->parseInputs(), "Test", smallWorkspace()));
        const auto problemsBefore = f.Document->problems();
        const auto namesBefore = f.Document->nameIndex();
        QVERIFY(!problemsBefore.isEmpty());

        const auto result = DocumentModel::runParser(f.Document->parseInputs(), "Test", smallWorkspace());
        const auto epoch = f.Document->contentEpoch();
        std::vector<std::shared_ptr<KerML::Entities::Element>> same;
        for (size_t i = 0; i < bodies.size(); i++)
            same.push_back(textualElement(f.Ids[i], bodies[i]));
        QVERIFY(f.Document->replaceContent(nullptr, same));
        QVERIFY(f.Document->contentEpoch() > epoch);

        QVERIFY(!f.Document->applyParseResult(result));
        QVERIFY(f.Document->problems().isEmpty());   // replaceContent cleared them, the stale result brought nothing back
        QVERIFY(f.Document->nameIndex() == namesBefore);
        for (int row = 0; row < f.Document->rowCount(); row++) {
            QCOMPARE(f.problemCount(row), 0);
            QCOMPARE(f.elementCount(row), 0);
        }
    }

    void freshParseAfterStaleApplies() {
        Fixture f({GoodA, Broken, GoodC});
        const auto stale = DocumentModel::runParser(f.Document->parseInputs(), "Test", smallWorkspace());
        f.Document->setBody(0, QStringLiteral("package A { part def Q; }"));
        QVERIFY(!f.Document->applyParseResult(stale));
        QVERIFY(f.Document->problems().isEmpty());

        QSignalSpy parsed(f.Document.get(), &DocumentModel::problemsChanged);
        QVERIFY(f.Document->applyParseResult(DocumentModel::runParser(f.Document->parseInputs(), "Test", smallWorkspace())));
        QCOMPARE(parsed.count(), 1);
        QVERIFY(f.problemCount(1) > 0);
        QVERIFY(f.elementCount(0) > 0);
        QVERIFY(f.elementCount(2) > 0);
        QVERIFY(f.Document->nameIndex() != nullptr);
    }

    void synchronousParseIsNeverStale() {
        Fixture f({GoodA, Broken});
        f.Document->adoptWorkspace(smallWorkspace());   // otherwise parse() loads the whole standard library
        QVERIFY(!f.Document->parse().isEmpty());
        f.Document->setBody(0, QStringLiteral("package A2;"));
        const auto second = f.Document->parse();
        QVERIFY(!second.isEmpty());   // the broken card is still broken
        for (const auto &problem : second)
            QCOMPARE(problem.ElementId, f.id(1));
        QCOMPARE(f.Document->problems().size(), second.size());
        QVERIFY(f.elementCount(0) > 0);
        QVERIFY(f.problemCount(1) > 0);
    }

    void revertedEditIsNotStale() {
        Fixture f({GoodA, Broken, GoodC});
        const auto result = DocumentModel::runParser(f.Document->parseInputs(), "Test", smallWorkspace());
        f.Document->setBody(2, QStringLiteral("package C { part def Other; }"));
        f.Document->setBody(2, QString::fromStdString(GoodC));   // back to what the parser saw
        QVERIFY(f.Document->applyParseResult(result));
        QVERIFY(f.problemCount(1) > 0);
        QVERIFY(f.elementCount(0) > 0);
        QVERIFY(f.elementCount(2) > 0);
    }

    void cardTurnedIntoTextLosesItsResults() {
        for (const char *target : {"Markdown", "YAML"}) {
            Fixture f({GoodA, Broken, "package }}} }}}"});
            f.Document->applyParseResult(DocumentModel::runParser(f.Document->parseInputs(), "Test", smallWorkspace()));
            QVERIFY(f.problemCount(1) > 0);
            QVERIFY(f.problemCount(2) > 0);
            QVERIFY(f.elementCount(0) > 0);
            const int otherProblems = f.problemCount(2);
            const int total = int(f.Document->problems().size());
            QSignalSpy rows(f.Document.get(), &DocumentModel::problemRowsChanged);
            QSignalSpy parsed(f.Document.get(), &DocumentModel::problemsChanged);

            f.Document->setLanguage(1, QString::fromLatin1(target));   // the card with problems
            QCOMPARE(problemsOfRow(f.Document->problems(), 1), 0);
            QCOMPARE(f.Document->problems().size(), qsizetype(otherProblems));
            QVERIFY(total > otherProblems);
            QCOMPARE(f.problemCount(1), 0);
            QCOMPARE(f.problemCount(2), otherProblems);   // untouched
            QCOMPARE(problemsOfRow(f.Document->problems(), 2), otherProblems);
            QCOMPARE(rows.count(), 1);
            QCOMPARE(parsed.count(), 0);

            // A card with a tree: tree gone, other trees stay.
            const int treeOfC = f.elementCount(0);
            f.Document->setLanguage(0, QString::fromLatin1(target));
            QCOMPARE(f.elementCount(0), 0);
            QVERIFY(treeOfC > 0);
            QCOMPARE(parsed.count(), 0);
            QCOMPARE(rows.count(), 1);   // card 0 had no problems: no problem moved
        }
    }

    void clearParseResultsForgetsEverything() {
        Fixture f({GoodA, Broken});
        f.Document->applyParseResult(DocumentModel::runParser(f.Document->parseInputs(), "Test", smallWorkspace()));
        QVERIFY(!f.Document->problems().isEmpty());
        QVERIFY(f.Document->nameIndex() != nullptr);
        QSignalSpy rows(f.Document.get(), &DocumentModel::problemRowsChanged);
        QSignalSpy parsed(f.Document.get(), &DocumentModel::problemsChanged);

        f.Document->clearParseResults();

        QVERIFY(f.Document->problems().isEmpty());
        QVERIFY(f.Document->nameIndex() == nullptr);
        QVERIFY(f.Document->instanceElements().empty());
        QCOMPARE(f.problemCount(1), 0);
        QCOMPARE(f.elementCount(0), 0);
        QCOMPARE(rows.count(), 1);
        QCOMPARE(parsed.count(), 0);
    }

private:
    /**
     * Applies a result after @p mutate changed the document: nothing may change and the call reports staleness.
     * @param previous whether a valid result was applied before (it must survive).
     */
    void checkStale(const std::function<void(Fixture &)> &mutate, bool previous) {
        Fixture f({GoodA, Broken, GoodC});
        if (previous)
            f.Document->applyParseResult(DocumentModel::runParser(f.Document->parseInputs(), "Test", smallWorkspace()));
        const auto problems = f.Document->problems();
        const auto names = f.Document->nameIndex();
        const auto instances = f.Document->instanceElements().size();
        QHash<QString, QPair<int, int>> perCard;   // (problem count, element count) per element id
        for (int row = 0; row < f.Document->rowCount(); row++)
            perCard.insert(f.Document->data(f.Document->index(row), DocumentModel::ElementIdRole).toString(),
                           {f.problemCount(row), f.elementCount(row)});

        const auto result = DocumentModel::runParser(f.Document->parseInputs(), "Test", smallWorkspace());
        mutate(f);
        QSignalSpy parsed(f.Document.get(), &DocumentModel::problemsChanged);
        QSignalSpy rowsChanged(f.Document.get(), &DocumentModel::problemRowsChanged);
        QSignalSpy dataSpy(f.Document.get(), &DocumentModel::dataChanged);
        const auto before = f.Document->problems();   // the mutation itself may drop the problems of a removed card
        const auto namesBefore = f.Document->nameIndex();
        QVERIFY(!f.Document->applyParseResult(result));

        QCOMPARE(parsed.count(), 0);
        QCOMPARE(rowsChanged.count(), 0);
        QCOMPARE(dataSpy.count(), 0);
        QCOMPARE(f.Document->problems().size(), before.size());
        for (qsizetype i = 0; i < before.size(); i++)
            QVERIFY(sameProblem(f.Document->problems().at(i), before.at(i)));
        QVERIFY(f.Document->nameIndex() == namesBefore);
        QVERIFY(namesBefore == names);
        QCOMPARE(f.Document->instanceElements().size(), instances);
        if (!previous) {
            QVERIFY(f.Document->problems().isEmpty());
            for (int row = 0; row < f.Document->rowCount(); row++) {
                QCOMPARE(f.problemCount(row), 0);
                QCOMPARE(f.elementCount(row), 0);
            }
        } else {
            QVERIFY(!f.Document->problems().isEmpty() || before.isEmpty());
            for (int row = 0; row < f.Document->rowCount(); row++) {
                const auto id = f.Document->data(f.Document->index(row), DocumentModel::ElementIdRole).toString();
                if (!perCard.contains(id))
                    continue;   // a card that was inserted
                const auto language = f.Document->data(f.Document->index(row), DocumentModel::LanguageRole).toString();
                if (language != "SysMLv2" && language != "KerML")
                    continue;   // turned into text by the mutation: it lost its results then, not by the stale apply
                QCOMPARE(f.problemCount(row), perCard.value(id).first);
                QCOMPARE(f.elementCount(row), perCard.value(id).second);
            }
        }
    }
};

QTEST_MAIN(DocumentProvenanceTests)
#include "tst_DocumentProvenance.moc"
