//
// The merged view of the cards of a document (DocumentMerge): packages that are continued in several cards are one logical node, every
// element stays attributed to the card that declares it, and names declared in more than one card are reported as warnings.
// The first half tests the pure merge function with hand-made trees, the second half the DocumentModel with real parsing.
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

#include "../src/Models/Libraries/DocumentMerge.h"
#include "../src/Models/Libraries/DocumentWorkspace.h"
#include "../src/ViewModels/DocumentModel.h"
#include "../src/ViewModels/ProblemListModel.h"

using namespace StructuraSystems::Client;
using nlohmann::json;

namespace {
    // ---- hand-made trees ----

    /** The nodes of a tree are given as (type, name, parent); the root is always the first. */
    struct T {
        QString Type;
        QString Name;
        int Parent;
    };

    QList<ElementTreeNode> tree(const QList<T> &nodes) {
        QList<ElementTreeNode> list;
        for (const auto &node : nodes)
            list.append({node.Type, node.Name, node.Parent, {}, {}});
        return list;
    }

    const QString Pkg = QStringLiteral("Package");
    const QString Lib = QStringLiteral("LibraryPackage");
    const QString Part = QStringLiteral("PartDefinition");
    const QString Ns = QStringLiteral("Namespace");

    /** The merged tree as "indent qualifiedName [cards]" lines, to compare whole trees at once. */
    QStringList dump(const MergedModel &model) {
        QStringList lines;
        QList<int> depth;
        for (const auto &node : model.Nodes) {
            const int d = node.Parent < 0 ? 0 : depth.at(node.Parent) + 1;
            depth.append(d);
            lines << QString(d * 2, QLatin1Char(' ')) + (node.Name.isEmpty() ? QStringLiteral("<unnamed>") : node.Name) + " [" +
                         node.CardIds.join(",") + "]";
        }
        return lines;
    }

    /** Rows of the dataChanged signals of the spy that announce @p role. */
    QSet<int> rowsAnnouncing(const QSignalSpy &spy, int role) {
        QSet<int> rows;
        for (const auto &arguments : spy) {
            for (const auto &announced : arguments.at(2).toList()) {
                if (announced.toInt() == role)
                    rows.insert(arguments.at(0).value<QModelIndex>().row());
            }
        }
        return rows;
    }

    // ---- documents with real parsing ----

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

    const std::string DeclareX = "package A { part def X; }";
    const std::string DeclareZ = "package A { part def Z; }";
    const std::string Broken = "package {{{";

    struct Fixture {
        std::vector<std::string> Ids;
        std::unique_ptr<DocumentModel> Document;

        /** One card per body; the id of card i is Ids[i]. A body of "#..." makes the card Markdown. */
        explicit Fixture(const std::vector<std::string> &bodies) {
            std::vector<std::shared_ptr<KerML::Entities::Element>> elements;
            for (const auto &body : bodies) {
                Ids.push_back(newId());
                elements.push_back(textualElement(Ids.back(), body));
            }
            Document = std::make_unique<DocumentModel>(project(), nullptr, std::move(elements));
            for (size_t i = 0; i < bodies.size(); i++) {
                if (bodies[i].starts_with("#"))
                    Document->setLanguage(int(i), QStringLiteral("Markdown"));
            }
        }

        QString id(size_t i) const { return QString::fromStdString(Ids[i]); }

        int rowOf(size_t i) const {
            for (int row = 0; row < Document->rowCount(); row++) {
                if (Document->data(Document->index(row), DocumentModel::ElementIdRole).toString() == id(i))
                    return row;
            }
            return -1;
        }

        void parse() {
            Document->applyParseResult(DocumentModel::runParser(Document->parseInputs(), "Test", smallWorkspace()));
        }

        QVariantList continued(int row) const {
            return Document->data(Document->index(row), DocumentModel::ContinuedPackagesRole).toList();
        }

        int problemCount(int row) const { return Document->data(Document->index(row), DocumentModel::ProblemCountRole).toInt(); }

        /** The outline as "<qualified name>@<rows of the cards>" in pre-order; the root is "@..." (no name). */
        QStringList outline() const {
            QStringList lines;
            collect(Document->outline(), QModelIndex(), lines);
            return lines;
        }

        QList<Problem> warnings() const {
            QList<Problem> result;
            for (const auto &problem : Document->problems()) {
                if (problem.Severity == 2)
                    result.append(problem);
            }
            return result;
        }

    private:
        static void collect(const ElementTreeModel *model, const QModelIndex &parent, QStringList &lines) {
            for (int row = 0; row < model->rowCount(parent); row++) {
                const auto index = model->index(row, 0, parent);
                QStringList rows;
                for (const auto &card : model->data(index, ElementTreeModel::CardRowsRole).toList())
                    rows << QString::number(card.toInt());
                lines << model->data(index, ElementTreeModel::QualifiedNameRole).toString() + "@" + rows.join(",");
                collect(model, index, lines);
            }
        }
    };
}

class DocumentOutlineTests : public QObject {
    Q_OBJECT

private slots:
    // ---- the pure merge ----

    void packageSplitOverTwoCards() {
        const auto model = DocumentMerge::merge({
            {"c1", tree({{Ns, "Root Namespace", -1}, {Pkg, "A", 0}, {Part, "X", 1}})},
            {"c2", tree({{Ns, "Root Namespace", -1}, {Pkg, "A", 0}, {Part, "Y", 1}})},
        });
        QCOMPARE(dump(model), (QStringList{"<unnamed> [c1,c2]", "  A [c1,c2]", "    X [c1]", "    Y [c2]"}));
        QVERIFY(model.Findings.isEmpty());
        QVERIFY(!model.Continued.contains("c1"));
        QCOMPARE(model.Continued.value("c2"), (QList<ContinuedPackage>{{"A", "c1"}}));
        QCOMPARE(model.Nodes.at(1).QualifiedName, QStringLiteral("A"));
        QCOMPARE(model.Nodes.at(3).QualifiedName, QStringLiteral("A::Y"));
        QVERIFY(model.Nodes.at(1).IsPackage);
        QVERIFY(!model.Nodes.at(2).IsPackage);
    }

    void packageSplitOverThreeCardsKeepsCardOrder() {
        // The cards are given in document order; the contributors follow that order, not the alphabet of the ids.
        const auto model = DocumentMerge::merge({
            {"z", tree({{Ns, "R", -1}, {Pkg, "A", 0}, {Part, "X", 1}})},
            {"m", tree({{Ns, "R", -1}, {Pkg, "A", 0}, {Part, "Y", 1}})},
            {"a", tree({{Ns, "R", -1}, {Lib, "A", 0}, {Part, "Z", 1}})},   // a library package continues a package
        });
        QCOMPARE(model.Nodes.at(1).CardIds, (QStringList{"z", "m", "a"}));
        QCOMPARE(model.Nodes.at(1).Type, Pkg);   // the first declaration gives the type
        QCOMPARE(dump(model), (QStringList{"<unnamed> [z,m,a]", "  A [z,m,a]", "    X [z]", "    Y [m]", "    Z [a]"}));
        QCOMPARE(model.Continued.value("m"), (QList<ContinuedPackage>{{"A", "z"}}));
        QCOMPARE(model.Continued.value("a"), (QList<ContinuedPackage>{{"A", "z"}}));
        QVERIFY(model.Findings.isEmpty());
    }

    void nestedPackages() {
        const auto model = DocumentMerge::merge({
            {"c1", tree({{Ns, "R", -1}, {Pkg, "A", 0}, {Pkg, "B", 1}, {Part, "X", 2}})},
            {"c2", tree({{Ns, "R", -1}, {Pkg, "A", 0}, {Pkg, "B", 1}, {Part, "Y", 2}, {Pkg, "C", 1}})},
            {"c3", tree({{Ns, "R", -1}, {Pkg, "A", 0}, {Pkg, "C", 1}, {Part, "W", 2}})},
        });
        QCOMPARE(dump(model), (QStringList{"<unnamed> [c1,c2,c3]", "  A [c1,c2,c3]", "    B [c1,c2]", "      X [c1]", "      Y [c2]",
                                           "    C [c2,c3]", "      W [c3]"}));
        QCOMPARE(model.Nodes.at(2).QualifiedName, QStringLiteral("A::B"));
        QCOMPARE(model.Nodes.at(3).QualifiedName, QStringLiteral("A::B::X"));
        QCOMPARE(model.Nodes.at(5).QualifiedName, QStringLiteral("A::C"));
        QCOMPARE(model.Continued.value("c2"), (QList<ContinuedPackage>{{"A", "c1"}, {"A::B", "c1"}}));
        QCOMPARE(model.Continued.value("c3"), (QList<ContinuedPackage>{{"A", "c1"}, {"A::C", "c2"}}));
        QVERIFY(!model.Continued.contains("c1"));
        QVERIFY(model.Findings.isEmpty());
        // The parents precede their children.
        for (int i = 0; i < model.Nodes.size(); i++)
            QVERIFY(model.Nodes.at(i).Parent < i);
    }

    void sameNameInDifferentPackagesIsNoDuplicate() {
        const auto model = DocumentMerge::merge({
            {"c1", tree({{Ns, "R", -1}, {Pkg, "A", 0}, {Part, "X", 1}})},
            {"c2", tree({{Ns, "R", -1}, {Pkg, "B", 0}, {Part, "X", 1}})},
        });
        QVERIFY(model.Findings.isEmpty());
        QVERIFY(model.Continued.isEmpty());
        QCOMPARE(dump(model), (QStringList{"<unnamed> [c1,c2]", "  A [c1]", "    X [c1]", "  B [c2]", "    X [c2]"}));
    }

    void unnamedNodesAreNotMerged() {
        const auto model = DocumentMerge::merge({
            {"c1", tree({{Ns, "R", -1}, {Pkg, "A", 0}, {Part, "", 1}, {Part, "P", 2}, {Pkg, "", 1}})},
            {"c2", tree({{Ns, "R", -1}, {Pkg, "A", 0}, {Part, "", 1}, {Part, "P", 2}, {Pkg, "", 1}})},
        });
        // Two unnamed parts stay two nodes, each with its own child P (which is no duplicate), and so do the unnamed packages.
        QCOMPARE(dump(model), (QStringList{"<unnamed> [c1,c2]", "  A [c1,c2]", "    <unnamed> [c1]", "      P [c1]", "    <unnamed> [c1]",
                                           "    <unnamed> [c2]", "      P [c2]", "    <unnamed> [c2]"}));
        QVERIFY(model.Findings.isEmpty());
        QCOMPARE(model.Nodes.at(3).QualifiedName, QString());
        QCOMPARE(model.Nodes.at(4).QualifiedName, QString());   // below an unnamed node nothing is mergeable
        QVERIFY(!model.Nodes.at(3).Mergeable);
        QVERIFY(!model.Nodes.at(4).Mergeable);
    }

    void duplicateNonPackageIsReportedOnTheLaterCardOnly() {
        const QList<MergeCard> cards = {
            {"c1", tree({{Ns, "R", -1}, {Pkg, "A", 0}, {Part, "X", 1}})},
            {"c2", tree({{Ns, "R", -1}, {Pkg, "A", 0}, {Part, "X", 1}})},
        };
        const auto model = DocumentMerge::merge(cards);
        QCOMPARE(model.Findings, (QList<MergeFinding>{{"A::X", "c2", "c1"}}));
        // One node per declaration, each attributed to its card.
        QCOMPARE(dump(model), (QStringList{"<unnamed> [c1,c2]", "  A [c1,c2]", "    X [c1]", "    X [c2]"}));

        // A third declaration: reported too, still naming the FIRST card.
        auto three = cards;
        three.append({"c3", tree({{Ns, "R", -1}, {Pkg, "A", 0}, {Part, "X", 1}})});
        QCOMPARE(DocumentMerge::merge(three).Findings, (QList<MergeFinding>{{"A::X", "c2", "c1"}, {"A::X", "c3", "c1"}}));

        // The duplicate is not merged further: its children are no further findings.
        const auto nested = DocumentMerge::merge({
            {"c1", tree({{Ns, "R", -1}, {Part, "X", 0}, {Part, "m", 1}})},
            {"c2", tree({{Ns, "R", -1}, {Part, "X", 0}, {Part, "m", 1}})},
        });
        QCOMPARE(nested.Findings, (QList<MergeFinding>{{"X", "c2", "c1"}}));
    }

    void packageAgainstNonPackageClash() {
        auto model = DocumentMerge::merge({
            {"c1", tree({{Ns, "R", -1}, {Part, "A", 0}})},
            {"c2", tree({{Ns, "R", -1}, {Pkg, "A", 0}, {Part, "X", 1}})},
        });
        QCOMPARE(model.Findings, (QList<MergeFinding>{{"A", "c2", "c1"}}));
        QVERIFY(model.Continued.isEmpty());
        QCOMPARE(dump(model), (QStringList{"<unnamed> [c1,c2]", "  A [c1]", "  A [c2]", "    X [c2]"}));

        model = DocumentMerge::merge({
            {"c1", tree({{Ns, "R", -1}, {Pkg, "A", 0}})},
            {"c2", tree({{Ns, "R", -1}, {Part, "A", 0}})},
        });
        QCOMPARE(model.Findings, (QList<MergeFinding>{{"A", "c2", "c1"}}));
        QVERIFY(model.Continued.isEmpty());
    }

    void duplicatesInsideOneCardAreNotReported() {
        const auto model = DocumentMerge::merge({
            {"c1", tree({{Ns, "R", -1}, {Part, "X", 0}, {Part, "X", 0}, {Pkg, "A", 0}, {Part, "Y", 3}, {Part, "Y", 3}})},
            {"c2", tree({{Ns, "R", -1}, {Pkg, "A", 0}, {Pkg, "A", 0}, {Part, "Z", 1}})},
        });
        QVERIFY(model.Findings.isEmpty());
        QCOMPARE(dump(model), (QStringList{"<unnamed> [c1,c2]", "  X [c1]", "  X [c1]", "  A [c1,c2]", "    Y [c1]", "    Y [c1]", "    Z [c2]"}));
        QCOMPARE(model.Continued.value("c2"), (QList<ContinuedPackage>{{"A", "c1"}}));   // listed once although A appears twice in c2
        QVERIFY(!model.Continued.contains("c1"));   // a package that is only repeated inside its own card is not "continued"
    }

    void clashWithEarlierCardIsReportedOncePerCardAndName() {
        const auto model = DocumentMerge::merge({
            {"c1", tree({{Ns, "R", -1}, {Part, "X", 0}})},
            {"c2", tree({{Ns, "R", -1}, {Part, "X", 0}, {Part, "X", 0}})},
        });
        QCOMPARE(model.Findings, (QList<MergeFinding>{{"X", "c2", "c1"}}));
    }

    void namedRootIsAlwaysTheDocumentRoot() {
        const auto model = DocumentMerge::merge({
            {"c1", tree({{Ns, "Root Namespace", -1}, {Pkg, "A", 0}})},
            {"c2", tree({{Ns, "Something else", -1}, {Pkg, "A", 0}})},
            {"c3", tree({{Pkg, "Odd", -1}, {Part, "Q", 0}})},   // even a package as root is only the root
        });
        QCOMPARE(model.Nodes.at(0).Parent, -1);
        QCOMPARE(model.Nodes.at(0).Name, QString());
        QCOMPARE(model.Nodes.at(0).QualifiedName, QString());
        QCOMPARE(model.Nodes.at(0).Type, Ns);   // of the first card
        QCOMPARE(model.Nodes.at(0).CardIds, (QStringList{"c1", "c2", "c3"}));
        QCOMPARE(model.Nodes.at(1).QualifiedName, QStringLiteral("A"));   // no "Root Namespace::" in front
        QCOMPARE(model.Nodes.at(1).CardIds, (QStringList{"c1", "c2"}));
        QCOMPARE(model.Nodes.at(2).QualifiedName, QStringLiteral("Q"));
        int roots = 0;
        for (const auto &node : model.Nodes)
            roots += node.Parent < 0;
        QCOMPARE(roots, 1);
    }

    void emptyCardsAndCardsWithoutTree() {
        QVERIFY(DocumentMerge::merge({}).Nodes.isEmpty());
        QVERIFY(DocumentMerge::merge({{"c1", {}}, {"c2", {}}}).Nodes.isEmpty());
        QVERIFY(DocumentMerge::merge({{"c1", {}}}).Findings.isEmpty());

        const auto model = DocumentMerge::merge({
            {"c1", {}},
            {"c2", tree({{Ns, "R", -1}, {Pkg, "A", 0}})},
            {"c3", {}},
            {"c4", tree({{Ns, "R", -1}, {Pkg, "A", 0}, {Part, "X", 1}})},
        });
        QCOMPARE(dump(model), (QStringList{"<unnamed> [c2,c4]", "  A [c2,c4]", "    X [c4]"}));
        QCOMPARE(model.Continued.value("c4"), (QList<ContinuedPackage>{{"A", "c2"}}));   // the first card WITH a tree
        QVERIFY(!model.Continued.contains("c1"));
        QVERIFY(!model.Continued.contains("c3"));

        // A card with just the root contributes to the root and nothing else.
        const auto onlyRoot = DocumentMerge::merge({{"c1", tree({{Ns, "R", -1}})}});
        QCOMPARE(dump(onlyRoot), (QStringList{"<unnamed> [c1]"}));
    }

    void mergeIsDeterministic() {
        QList<MergeCard> cards;
        for (int c = 0; c < 6; c++) {
            QList<T> nodes = {{Ns, "R", -1}};
            for (int p = 0; p < 4; p++) {
                nodes.append({Pkg, QStringLiteral("P%1").arg((p + c) % 4), 0});
                const int parent = int(nodes.size()) - 1;
                for (int m = 0; m < 5; m++)
                    nodes.append({Part, QStringLiteral("M%1").arg((m * (c + 1)) % 5), parent});
            }
            cards.append({QStringLiteral("card%1").arg(c), tree(nodes)});
        }
        const auto first = DocumentMerge::merge(cards);
        QVERIFY(!first.Findings.isEmpty());
        QVERIFY(!first.Continued.isEmpty());
        for (int i = 0; i < 5; i++)
            QVERIFY(DocumentMerge::merge(cards) == first);

        // Findings come in card order (then tree order), whatever the hash order of the maps inside.
        QString lastCard;
        for (const auto &finding : first.Findings) {
            QVERIFY(finding.CardId >= lastCard);
            lastCard = finding.CardId;
        }
    }

    void namesContainingDoubleColonDoNotCollide() {
        // part def 'A::B' in one card, package A { part def B } in another: different elements, the display string is the same.
        auto model = DocumentMerge::merge({
            {"c1", tree({{Ns, "R", -1}, {Part, "A::B", 0}})},
            {"c2", tree({{Ns, "R", -1}, {Pkg, "A", 0}, {Part, "B", 1}})},
        });
        QVERIFY(model.Findings.isEmpty());
        QVERIFY(model.Continued.isEmpty());
        QCOMPARE(dump(model), (QStringList{"<unnamed> [c1,c2]", "  A::B [c1]", "  A [c2]", "    B [c2]"}));
        QCOMPARE(model.Nodes.at(1).QualifiedName, QStringLiteral("A::B"));
        QCOMPARE(model.Nodes.at(3).QualifiedName, QStringLiteral("A::B"));   // display only

        // Packages are keyed by path too: a package 'A::B' is not the package B inside A.
        model = DocumentMerge::merge({
            {"c1", tree({{Ns, "R", -1}, {Pkg, "A::B", 0}, {Part, "X", 1}})},
            {"c2", tree({{Ns, "R", -1}, {Pkg, "A", 0}, {Pkg, "B", 1}, {Part, "X", 2}})},
        });
        QVERIFY(model.Findings.isEmpty());
        QVERIFY(model.Continued.isEmpty());
        QCOMPARE(model.Nodes.at(1).CardIds, (QStringList{"c1"}));

        // The same name twice is still a duplicate.
        model = DocumentMerge::merge({
            {"c1", tree({{Ns, "R", -1}, {Part, "A::B", 0}})},
            {"c2", tree({{Ns, "R", -1}, {Part, "A::B", 0}})},
        });
        QCOMPARE(model.Findings, (QList<MergeFinding>{{"A::B", "c2", "c1"}}));
    }

    void childrenOfAClashKeepTheirQualifiedNameButStayUnmerged() {
        const auto model = DocumentMerge::merge({
            {"c1", tree({{Ns, "R", -1}, {Part, "X", 0}, {Part, "m", 1}})},
            {"c2", tree({{Ns, "R", -1}, {Part, "X", 0}, {Pkg, "P", 1}, {Part, "m", 2}, {Part, "", 1}, {Part, "u", 4}})},
        });
        QCOMPARE(model.Findings, (QList<MergeFinding>{{"X", "c2", "c1"}}));   // nothing for X::m
        QCOMPARE(dump(model), (QStringList{"<unnamed> [c1,c2]", "  X [c1]", "    m [c1]", "  X [c2]", "    P [c2]", "      m [c2]",
                                           "    <unnamed> [c2]", "      u [c2]"}));
        QCOMPARE(model.Nodes.at(2).QualifiedName, QStringLiteral("X::m"));
        QCOMPARE(model.Nodes.at(3).QualifiedName, QStringLiteral("X"));
        QCOMPARE(model.Nodes.at(4).QualifiedName, QStringLiteral("X::P"));
        QCOMPARE(model.Nodes.at(5).QualifiedName, QStringLiteral("X::P::m"));
        QCOMPARE(model.Nodes.at(6).QualifiedName, QString());   // unnamed
        QCOMPARE(model.Nodes.at(7).QualifiedName, QString());   // below an unnamed node
        for (int i = 3; i < 6; i++)
            QVERIFY(!model.Nodes.at(i).Mergeable);   // the clash and what is below it
        QVERIFY(model.Nodes.at(1).Mergeable);
        QVERIFY(model.Continued.isEmpty());   // the package P below the clash is not "continued"
    }

    void malformedParentsHaveDefinedOutput() {
        // Parent >= own index (also itself) and negative values other than -1 become direct children of the document root (they are
        // not dropped); several real roots (-1) are all the document root. No crash, parents precede children.
        const auto model = DocumentMerge::merge({
            {"c1", tree({{Ns, "R", -1}, {Part, "a", 5}, {Part, "b", -3}, {Ns, "R2", -1}, {Part, "c", 3}, {Part, "d", 5}, {Part, "e", 4}})},
        });
        QCOMPARE(dump(model), (QStringList{"<unnamed> [c1]", "  a [c1]", "  b [c1]", "  c [c1]", "    e [c1]", "  d [c1]"}));
        QCOMPARE(model.Nodes.at(1).QualifiedName, QStringLiteral("a"));
        QCOMPARE(model.Nodes.at(3).QualifiedName, QStringLiteral("c"));
        for (int i = 1; i < model.Nodes.size(); i++)
            QVERIFY(model.Nodes.at(i).Parent >= 0 && model.Nodes.at(i).Parent < i);
        QCOMPARE(model.Nodes.at(3).Parent, 0);
        QCOMPARE(model.Nodes.at(4).Parent, 3);   // e: a valid parent (c) stays
        QVERIFY(model.Findings.isEmpty());

        // The same across cards: the stray nodes of the second card clash by name like any others.
        const auto two = DocumentMerge::merge({
            {"c1", tree({{Ns, "R", -1}, {Part, "a", 0}})},
            {"c2", tree({{Part, "a", 9}, {Ns, "R", -1}, {Part, "b", 0}})},
        });
        QCOMPARE(two.Findings, (QList<MergeFinding>{{"a", "c2", "c1"}}));
        QCOMPARE(two.Nodes.at(0).CardIds, (QStringList{"c1", "c2"}));
        for (int i = 1; i < two.Nodes.size(); i++)
            QVERIFY(two.Nodes.at(i).Parent < i);
    }

    void clashFollowedByAThirdCardContinuingThePackage() {
        // Pins the current behaviour: only the FIRST declaration of a name takes part in the merge. Card 1 declares A as a part, so the
        // packages A of cards 2 and 3 both clash with it and are NOT merged with each other (two nodes, a finding on each card, both
        // naming card 1, nothing continued). A package "continues" only a package that was the first declaration.
        const auto model = DocumentMerge::merge({
            {"c1", tree({{Ns, "R", -1}, {Part, "A", 0}})},
            {"c2", tree({{Ns, "R", -1}, {Pkg, "A", 0}, {Part, "X", 1}})},
            {"c3", tree({{Ns, "R", -1}, {Pkg, "A", 0}, {Part, "Y", 1}})},
        });
        QCOMPARE(model.Findings, (QList<MergeFinding>{{"A", "c2", "c1"}, {"A", "c3", "c1"}}));
        QVERIFY(model.Continued.isEmpty());
        QCOMPARE(dump(model), (QStringList{"<unnamed> [c1,c2,c3]", "  A [c1]", "  A [c2]", "    X [c2]", "  A [c3]", "    Y [c3]"}));
        QCOMPARE(model.Nodes.at(3).QualifiedName, QStringLiteral("A::X"));
    }

    // ---- DocumentModel ----

    void outlineAndContinuedPackages() {
        Fixture f({DeclareX, "# Notes", DeclareZ, DeclareX});
        QVERIFY(f.Document->outline() != nullptr);
        QCOMPARE(f.Document->outline()->count(), 0);   // before the first parse

        f.parse();

        QCOMPARE(f.outline(), (QStringList{"@0,2,3", "A@0,2,3", "A::X@0", "A::Z@2", "A::X@3"}));
        const auto *outline = f.Document->outline();
        const auto root = outline->index(0, 0);
        const auto package = outline->index(0, 0, root);
        QCOMPARE(outline->data(package, ElementTreeModel::NameRole).toString(), QStringLiteral("A"));
        QCOMPARE(outline->data(package, ElementTreeModel::ElementTypeRole).toString(), Pkg);
        QCOMPARE(outline->data(package, ElementTreeModel::CardRowRole).toInt(), 0);   // jump target: the first contributor
        QCOMPARE(outline->data(package, ElementTreeModel::CardRowsRole).toList(), (QVariantList{0, 2, 3}));
        QCOMPARE(outline->data(root, ElementTreeModel::NameRole).toString(), QString());

        QVERIFY(f.continued(0).isEmpty());
        QVERIFY(f.continued(1).isEmpty());
        for (const int row : {2, 3}) {
            const auto continued = f.continued(row);
            QCOMPARE(continued.size(), 1);
            QCOMPARE(continued.at(0).toMap().value("name").toString(), QStringLiteral("A"));
            QCOMPARE(continued.at(0).toMap().value("row").toInt(), 0);
        }
        QVERIFY(f.Document->roleNames().values().contains("continuedPackages"));

        // The duplicate: a warning on the later card, naming the first one (1-based).
        const auto warnings = f.warnings();
        QCOMPARE(warnings.size(), 1);
        QCOMPARE(warnings.at(0).Message, QStringLiteral("'A::X' is also declared in card 1"));
        QCOMPARE(warnings.at(0).ElementId, f.id(3));
        QCOMPARE(warnings.at(0).Row, 3);
        QCOMPARE(warnings.at(0).Line, -1);
        QCOMPARE(warnings.at(0).Column, -1);
        QCOMPARE(warnings.at(0).Severity, 2);
        QCOMPARE(f.Document->problems().size(), 1);
        QCOMPARE(f.problemCount(3), 1);
        QCOMPARE(f.problemCount(0), 0);
    }

    void movingTheFirstDeclarationMovesTheWarning() {
        Fixture f({DeclareX, "# Notes", DeclareZ, DeclareX});
        f.parse();
        QSignalSpy rows(f.Document.get(), &DocumentModel::problemRowsChanged);
        QSignalSpy parsed(f.Document.get(), &DocumentModel::problemsChanged);

        f.Document->moveRow(3, 0);   // the other declaration of A::X comes first now

        QCOMPARE(parsed.count(), 0);
        QCOMPARE(rows.count(), 1);
        QCOMPARE(f.rowOf(3), 0);
        QCOMPARE(f.rowOf(0), 1);
        const auto warnings = f.warnings();
        QCOMPARE(warnings.size(), 1);
        QCOMPARE(warnings.at(0).ElementId, f.id(0));   // the former first one is the later card now
        QCOMPARE(warnings.at(0).Row, 1);
        QCOMPARE(warnings.at(0).Message, QStringLiteral("'A::X' is also declared in card 1"));   // card 1 is the moved one
        QCOMPARE(f.problemCount(1), 1);
        QCOMPARE(f.problemCount(0), 0);
        QCOMPARE(f.outline(), (QStringList{"@0,1,3", "A@0,1,3", "A::X@0", "A::X@1", "A::Z@3"}));
        QCOMPARE(f.continued(0).size(), 0);
        QCOMPARE(f.continued(1).at(0).toMap().value("row").toInt(), 0);
        QCOMPARE(f.continued(3).at(0).toMap().value("row").toInt(), 0);

        // The card number follows: a card moved in front of the first declaration shifts it to position 2.
        f.Document->moveRow(2, 0);   // [c3, c0, markdown, c2] -> [markdown, c3, c0, c2]
        QCOMPARE(rows.count(), 2);
        QCOMPARE(f.warnings().at(0).Message, QStringLiteral("'A::X' is also declared in card 2"));
        QCOMPARE(f.warnings().at(0).Row, 2);
        QCOMPARE(parsed.count(), 0);

        // A move that changes nothing of the warning (or the outline) is silent.
        Fixture g({DeclareX, DeclareX, "# a", "# b"});
        g.parse();
        QSignalSpy gRows(g.Document.get(), &DocumentModel::problemRowsChanged);
        QSignalSpy gData(g.Document.get(), &DocumentModel::dataChanged);
        g.Document->moveRow(2, 3);
        QCOMPARE(gRows.count(), 0);
        QCOMPARE(gData.count(), 0);
        QCOMPARE(g.outline(), (QStringList{"@0,1", "A@0,1", "A::X@0", "A::X@1"}));
    }

    void removingOneDeclarationRemovesTheWarning() {
        Fixture f({DeclareX, "# Notes", DeclareZ, DeclareX});
        f.parse();
        QSignalSpy rows(f.Document.get(), &DocumentModel::problemRowsChanged);
        QSignalSpy parsed(f.Document.get(), &DocumentModel::problemsChanged);

        f.Document->removeElement(3);

        QVERIFY(f.Document->problems().isEmpty());
        QCOMPARE(rows.count(), 1);
        QCOMPARE(parsed.count(), 0);
        QCOMPARE(f.outline(), (QStringList{"@0,2", "A@0,2", "A::X@0", "A::Z@2"}));
        for (int row = 0; row < f.Document->rowCount(); row++)
            QCOMPARE(f.problemCount(row), 0);

        // The same with the first declaration: the later one is the only one left, with no warning and no continued package.
        Fixture g({DeclareX, "# Notes", DeclareZ, DeclareX});
        g.parse();
        g.Document->removeElement(0);
        QVERIFY(g.Document->problems().isEmpty());
        QCOMPARE(g.outline(), (QStringList{"@1,2", "A@1,2", "A::Z@1", "A::X@2"}));
        QVERIFY(g.continued(1).isEmpty());   // (the Z card is the first one of A now)
        QCOMPARE(g.continued(2).at(0).toMap().value("row").toInt(), 1);
    }

    void insertingACardShiftsTheRows() {
        Fixture f({DeclareX, DeclareX});
        f.parse();
        QCOMPARE(f.warnings().at(0).Message, QStringLiteral("'A::X' is also declared in card 1"));
        QSignalSpy rows(f.Document.get(), &DocumentModel::problemRowsChanged);

        f.Document->insertElement(0, QStringLiteral("Markdown"));

        QCOMPARE(rows.count(), 1);
        QCOMPARE(f.warnings().at(0).Message, QStringLiteral("'A::X' is also declared in card 2"));
        QCOMPARE(f.warnings().at(0).Row, 2);
        QCOMPARE(f.outline(), (QStringList{"@1,2", "A@1,2", "A::X@1", "A::X@2"}));
        QCOMPARE(f.continued(2).at(0).toMap().value("row").toInt(), 1);
    }

    void turningAContributorIntoMarkdownUpdatesOutlineAndWarnings() {
        Fixture f({DeclareX, "# Notes", DeclareZ, DeclareX});
        f.parse();
        QSignalSpy rows(f.Document.get(), &DocumentModel::problemRowsChanged);
        QSignalSpy parsed(f.Document.get(), &DocumentModel::problemsChanged);

        f.Document->setLanguage(3, QStringLiteral("Markdown"));   // the duplicate
        QVERIFY(f.Document->problems().isEmpty());
        QCOMPARE(f.outline(), (QStringList{"@0,2", "A@0,2", "A::X@0", "A::Z@2"}));
        QVERIFY(f.continued(3).isEmpty());
        QCOMPARE(f.problemCount(3), 0);
        QCOMPARE(rows.count(), 1);

        f.Document->setLanguage(0, QStringLiteral("Markdown"));   // the first declaration of A
        QCOMPARE(f.outline(), (QStringList{"@2", "A@2", "A::Z@2"}));
        QVERIFY(f.continued(2).isEmpty());
        QCOMPARE(parsed.count(), 0);

        // A card that turns into Markdown without having contributed anything changes nothing.
        const int before = int(rows.count());
        f.Document->setLanguage(1, QStringLiteral("YAML"));
        QCOMPARE(int(rows.count()), before);
    }

    void staleResultLeavesOutlineAndWarningsAlone() {
        Fixture f({DeclareX, "# Notes", DeclareZ, DeclareX});
        f.parse();
        const auto outlineBefore = f.outline();
        const auto problemsBefore = f.Document->problems();
        const auto result = DocumentModel::runParser(f.Document->parseInputs(), "Test", smallWorkspace());
        f.Document->setBody(3, QStringLiteral("package A { part def Other; }"));   // the result is stale now
        const auto outlineAfterEdit = f.outline();   // an edit alone does not re-parse: the old trees stay
        QSignalSpy parsed(f.Document.get(), &DocumentModel::problemsChanged);
        QSignalSpy rows(f.Document.get(), &DocumentModel::problemRowsChanged);
        QSignalSpy data(f.Document.get(), &DocumentModel::dataChanged);

        QVERIFY(!f.Document->applyParseResult(result));

        QCOMPARE(f.outline(), outlineAfterEdit);
        QCOMPARE(outlineAfterEdit, outlineBefore);
        QCOMPARE(f.Document->problems().size(), problemsBefore.size());
        QCOMPARE(f.warnings().size(), 1);
        QCOMPARE(parsed.count(), 0);
        QCOMPARE(rows.count(), 0);
        QCOMPARE(data.count(), 0);

        // The same for a document that was never parsed: nothing appears.
        Fixture g({DeclareX, DeclareX});
        const auto gResult = DocumentModel::runParser(g.Document->parseInputs(), "Test", smallWorkspace());
        g.Document->setBody(0, QStringLiteral("package B;"));
        QVERIFY(!g.Document->applyParseResult(gResult));
        QCOMPARE(g.Document->outline()->count(), 0);
        QVERIFY(g.Document->problems().isEmpty());
    }

    void clearParseResultsEmptiesTheOutline() {
        Fixture f({DeclareX, DeclareX});
        f.parse();
        QCOMPARE(f.warnings().size(), 1);
        QSignalSpy rows(f.Document.get(), &DocumentModel::problemRowsChanged);
        QSignalSpy parsed(f.Document.get(), &DocumentModel::problemsChanged);

        f.Document->clearParseResults();

        QCOMPARE(f.Document->outline()->count(), 0);
        QVERIFY(f.Document->problems().isEmpty());
        QVERIFY(f.continued(1).isEmpty());
        QCOMPARE(f.problemCount(1), 0);
        QCOMPARE(rows.count(), 1);   // the merge warning went
        QCOMPARE(parsed.count(), 0);

        // Clearing again changes nothing, so it is silent.
        f.Document->clearParseResults();
        QCOMPARE(rows.count(), 1);
    }

    void newParseRegeneratesEverythingWithOneSignal() {
        Fixture f({DeclareX, DeclareX});
        f.parse();
        f.Document->clearParseResults();
        QSignalSpy parsed(f.Document.get(), &DocumentModel::problemsChanged);
        QSignalSpy rows(f.Document.get(), &DocumentModel::problemRowsChanged);

        f.parse();

        QCOMPARE(parsed.count(), 1);
        QCOMPARE(rows.count(), 0);
        QCOMPARE(f.outline(), (QStringList{"@0,1", "A@0,1", "A::X@0", "A::X@1"}));
        QCOMPARE(f.warnings().size(), 1);
        QCOMPARE(f.problemCount(1), 1);

        // Parsing the same content again gives the same warnings, not a doubled list.
        f.parse();
        QCOMPARE(parsed.count(), 2);
        QCOMPARE(f.Document->problems().size(), 1);
    }

    void parserProblemsComeFirstAndStayApartFromMergeWarnings() {
        Fixture f({DeclareX, Broken, DeclareX});
        f.parse();
        const auto problems = f.Document->problems();
        QVERIFY(problems.size() >= 2);
        QCOMPARE(problems.last().Severity, 2);
        QCOMPARE(problems.last().ElementId, f.id(2));
        for (qsizetype i = 0; i + 1 < problems.size(); i++)
            QCOMPARE(problems.at(i).ElementId, f.id(1));   // the parser's problems of the broken card
        // The broken card has no tree: the other two still merge.
        QCOMPARE(f.outline(), (QStringList{"@0,2", "A@0,2", "A::X@0", "A::X@2"}));

        // Moving cards regenerates the warning, the parser's problems are only re-mapped.
        const auto parserCount = problems.size() - 1;
        f.Document->moveRow(2, 0);
        QCOMPARE(f.Document->problems().size(), parserCount + 1);
        QCOMPARE(f.Document->problems().last().ElementId, f.id(0));
        QCOMPARE(f.Document->problems().last().Row, 1);
        QCOMPARE(f.problemCount(f.rowOf(1)), int(parserCount));
    }

    void replaceContentForgetsTheOutline() {
        // replaceContent is for online documents; the fixture's document is one (it is built from the elements).
        Fixture f({DeclareX, DeclareX});
        f.parse();
        QVERIFY(f.Document->outline()->count() > 0);
        std::vector<std::shared_ptr<KerML::Entities::Element>> elements = {textualElement(newId(), DeclareX)};
        QVERIFY(f.Document->replaceContent(nullptr, elements));
        QCOMPARE(f.Document->outline()->count(), 0);
        QVERIFY(f.Document->problems().isEmpty());
        QVERIFY(f.continued(0).isEmpty());
    }

    void namesWithDoubleColonAreNotDuplicatesWithRealParser() {
        Fixture f({"part def 'A::B';", "package A { part def B; }"});
        f.parse();
        QVERIFY(f.Document->problems().isEmpty());
        QCOMPARE(f.Document->outline()->count(), 4);   // root, the part, A, B
        QVERIFY(f.continued(1).isEmpty());

        // A real duplicate of the quoted name is still found.
        Fixture g({"part def 'A::B';", "part def 'A::B';"});
        g.parse();
        QCOMPARE(g.warnings().size(), 1);
        QCOMPARE(g.warnings().at(0).ElementId, g.id(1));
    }

    void outlineUpdatesInPlaceWhenOnlyRowsShift() {
        Fixture f({DeclareX, "# a", DeclareZ});
        f.parse();
        QCOMPARE(f.outline(), (QStringList{"@0,2", "A@0,2", "A::X@0", "A::Z@2"}));
        QSignalSpy reset(f.Document->outline(), &QAbstractItemModel::modelReset);
        QSignalSpy data(f.Document->outline(), &QAbstractItemModel::dataChanged);

        f.Document->moveRow(1, 0);   // the Markdown card moves in front: the rows of all code cards shift, the tree stays

        QCOMPARE(reset.count(), 0);
        QCOMPARE(f.outline(), (QStringList{"@1,2", "A@1,2", "A::X@1", "A::Z@2"}));
        QVERIFY(data.count() > 0);
        for (const auto &arguments : data) {
            const auto roles = arguments.at(2).toList();
            QVERIFY(roles.contains(int(ElementTreeModel::CardRowRole)));
            QVERIFY(roles.contains(int(ElementTreeModel::CardRowsRole)));
            QVERIFY(!roles.contains(int(ElementTreeModel::NameRole)));
        }
        const auto *outline = f.Document->outline();
        const auto package = outline->index(0, 0, outline->index(0, 0));
        QCOMPARE(outline->data(package, ElementTreeModel::CardRowRole).toInt(), 1);

        // The same trees again: equal outline, no signal at all.
        data.clear();
        f.parse();
        QCOMPARE(reset.count(), 0);
        QCOMPARE(data.count(), 0);

        // A structural change resets.
        f.Document->removeElement(f.rowOf(2));
        QCOMPARE(reset.count(), 1);
        QCOMPARE(f.outline(), (QStringList{"@1", "A@1", "A::X@1"}));
    }

    void dataChangedAnnouncesTheRightRoles() {
        Fixture f({DeclareX, "# Notes", DeclareZ, DeclareX});
        QSignalSpy data(f.Document.get(), &DocumentModel::dataChanged);

        f.parse();

        QCOMPARE(rowsAnnouncing(data, DocumentModel::ContinuedPackagesRole), (QSet<int>{2, 3}));
        QCOMPARE(rowsAnnouncing(data, DocumentModel::ProblemCountRole), (QSet<int>{3}));
        QCOMPARE(rowsAnnouncing(data, DocumentModel::ElementCountRole), (QSet<int>{0, 2, 3}));
        data.clear();

        f.Document->moveRow(3, 0);   // [c3, c0, markdown, c2]: the warning moves from c3 to c0

        // c3 is first now (nothing to continue), c0 continues A; c2 still refers to row 0, so it is not announced.
        QCOMPARE(rowsAnnouncing(data, DocumentModel::ContinuedPackagesRole), (QSet<int>{0, 1}));
        QCOMPARE(rowsAnnouncing(data, DocumentModel::ProblemCountRole), (QSet<int>{0, 1}));
        QCOMPARE(rowsAnnouncing(data, DocumentModel::ElementCountRole), (QSet<int>{}));
        data.clear();

        // Nothing changes for a move that leaves the warning and the continued packages as they are.
        f.Document->moveRow(2, 3);   // markdown and c2 swap
        QCOMPARE(rowsAnnouncing(data, DocumentModel::ProblemCountRole), (QSet<int>{}));
        QCOMPARE(rowsAnnouncing(data, DocumentModel::ContinuedPackagesRole), (QSet<int>{}));
    }

    void realParserTypeStringsOfPackages() {
        // The Type strings the parser really delivers: "Namespace" (root), "Package", "LibraryPackage", "PartDefinition".
        Fixture f({"library package L { part def X; }", "package A { part def Y; }"});
        f.parse();
        const auto *outline = f.Document->outline();
        QVERIFY(outline->count() >= 5);
        QStringList types;
        const auto root = outline->index(0, 0);
        types << outline->data(root, ElementTreeModel::ElementTypeRole).toString();
        for (int row = 0; row < outline->rowCount(root); row++) {
            const auto child = outline->index(row, 0, root);
            types << outline->data(child, ElementTreeModel::ElementTypeRole).toString();
            for (int k = 0; k < outline->rowCount(child); k++)
                types << outline->data(outline->index(k, 0, child), ElementTreeModel::ElementTypeRole).toString();
        }
        QVERIFY(DocumentMerge::isPackageType(types.at(1)));
        QVERIFY(DocumentMerge::isPackageType(types.at(3)));
        QVERIFY(!DocumentMerge::isPackageType(types.at(0)));
        QVERIFY(!DocumentMerge::isPackageType(types.at(2)));
        QVERIFY(!DocumentMerge::isPackageType(types.at(4)));
    }
};

QTEST_MAIN(DocumentOutlineTests)
#include "tst_DocumentOutline.moc"
