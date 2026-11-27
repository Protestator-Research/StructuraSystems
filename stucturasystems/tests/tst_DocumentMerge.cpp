//
// How the text blocks ("cards") of one document relate to each other in DocumentWorkspace::update(): continued packages, SysML on KerML,
// forward references and the model of each row. These tests CHARACTERIZE the current behaviour (nothing is changed in the production
// code); they record what really happens, not what a model in which blocks build on each other would need.
//

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <kerml/core/features/Feature.h>
#include <kerml/core/features/FeatureTyping.h>
#include <kerml/core/types/Specialization.h>
#include <kerml/core/types/Type.h>
#include <kerml/root/elements/Element.h>
#include <kerml/root/namespaces/Namespace.h>

#include <QtTest>

#include "../src/Models/Libraries/DocumentWorkspace.h"
#include "../src/Models/Libraries/LibraryWorkspace.h"

using namespace StructuraSystems::Client;

namespace {
    /** A workspace whose standard library is one small package, so that parsing stays fast. */
    std::shared_ptr<DocumentWorkspace> smallWorkspace() {
        auto library = std::make_shared<std::vector<StandardSource>>();
        library->push_back({"Lib.sysml", "library package Lib { part def Base; attribute def Num; }", false});
        return std::make_shared<DocumentWorkspace>(nullptr, QString(), library);
    }

    DocumentSource sysml(int row, const char *text) {
        DocumentSource source;
        source.Row = row;
        source.Text = text;
        return source;
    }

    DocumentSource kerml(int row, const char *text) {
        DocumentSource source = sysml(row, text);
        source.KerML = true;
        return source;
    }

    void collect(const KerML::Entities::Namespace &owner, const std::string &prefix, std::set<QString> &names, int depth) {
        for (const auto &member : owner.ownedMember()) {
            if (member == nullptr)
                continue;
            const auto name = member->declaredName();
            if (!name.has_value() || name->empty())
                continue;
            const auto qualified = prefix.empty() ? *name : prefix + "::" + *name;
            names.insert(QString::fromStdString(qualified));
            if (depth < 8) {
                if (const auto *inner = dynamic_cast<const KerML::Entities::Namespace *>(member.get()))
                    collect(*inner, qualified, names, depth + 1);
            }
        }
    }

    QString qualifiedNameOf(const KerML::Entities::Element *element) {
        QStringList segments;
        for (int depth = 0; element != nullptr && depth < 32; ++depth) {
            const auto owner = element->owningNamespace();
            const auto name = element->declaredName();
            if (owner == nullptr || !name.has_value() || name->empty())
                break;
            segments.prepend(QString::fromStdString(*name));
            element = owner.get();
        }
        return segments.join(QStringLiteral("::"));
    }

    /** What a run of update() shows: the diagnostics and, per row, the qualified names declared below the root of the row. */
    struct Outcome {
        WorkspaceResult Result;
        std::map<int, std::set<QString>> Names;
        std::map<int, std::set<QString>> NamedElements;   ///< declared names of all elements in WorkspaceRow::Elements (flat: packages and their members)
        std::map<int, bool> HasRoot;
        /// Per row: "<element> -> <qualified name of the bound type>" for every typing / specialization (target "<unbound>" if the general type is null).
        std::map<int, std::set<QString>> Bindings;
        /// "<row>:<element> -> <target>" to the row whose model declares the bound type, -1 if it is in no row (identity comparison only).
        std::map<QString, int> TargetRow;
        std::map<QString, const KerML::Entities::Element *> TargetPointer;
        std::map<const KerML::Entities::Element *, int> ElementRow;   ///< element -> row of the block that declares it (never dereferenced)

        int rowOfTarget(int row, const QString &binding) const {
            const auto it = TargetRow.find(QString::number(row) + ":" + binding);
            return it == TargetRow.end() ? -2 : it->second;
        }

        QStringList messages(int row) const {
            QStringList list;
            for (const auto &d: Result.Diagnostics)
                if (d.Row == row)
                    list << d.Message;
            return list;
        }

        bool has(int row, const QString &name) const {
            const auto it = Names.find(row);
            return it != Names.end() && it->second.count(name) > 0;
        }

        bool bound(int row, const QString &binding) const {
            const auto it = Bindings.find(row);
            return it != Bindings.end() && it->second.count(binding) > 0;
        }

        int count() const { return int(Result.Diagnostics.size()); }
    };

    Outcome run(const std::vector<DocumentSource> &sources, const char *title) {
        auto workspace = smallWorkspace();
        Outcome outcome;
        outcome.Result = workspace->update(sources, QStringLiteral("doc"), [&outcome](const WorkspaceRow &row) {
            outcome.HasRoot[row.Row] = row.Root != nullptr;
            if (row.Root != nullptr)
                collect(*row.Root, std::string(), outcome.Names[row.Row], 0);
            auto &top = outcome.NamedElements[row.Row];
            auto &bindings = outcome.Bindings[row.Row];
            for (const auto &element: row.Elements) {
                if (element == nullptr)
                    continue;
                outcome.ElementRow[element.get()] = row.Row;
                const auto from = element->declaredName().value_or(std::string());
                const auto bind = [&](const std::shared_ptr<KerML::Entities::Type> &general) {
                    const auto target = general != nullptr ? qualifiedNameOf(general.get()) : QStringLiteral("<unbound>");
                    const auto binding = QString::fromStdString(from) + QStringLiteral(" -> ") + target;
                    bindings.insert(binding);
                    outcome.TargetPointer[QString::number(row.Row) + ":" + binding] = general.get();
                };
                if (const auto *feature = dynamic_cast<const KerML::Entities::Feature *>(element.get())) {
                    for (const auto &typing: feature->ownedTyping())
                        if (typing != nullptr)
                            bind(typing->general());
                }
                if (const auto *type = dynamic_cast<const KerML::Entities::Type *>(element.get())) {
                    for (const auto &specialization: type->ownedSpecialization())
                        if (specialization != nullptr)
                            bind(specialization->general());
                }
                const auto name = element->declaredName();
                if (name.has_value() && !name->empty())
                    top.insert(QString::fromStdString(*name));
            }
        });
        for (const auto &[key, pointer]: outcome.TargetPointer) {
            const auto it = outcome.ElementRow.find(pointer);
            outcome.TargetRow[key] = it == outcome.ElementRow.end() ? -1 : it->second;
        }
        qInfo() << "==" << title;
        for (const auto &[row, list]: outcome.Bindings)
            for (const auto &binding: list)
                qInfo() << "  row" << row << "binds" << binding << "(declared in row" << outcome.rowOfTarget(row, binding) << ")";
        for (const auto &d: outcome.Result.Diagnostics)
            qInfo() << "  diagnostic row" << d.Row << "line" << d.Line << "col" << d.Column << "sev" << d.Severity << d.Message;
        for (const auto &[row, names]: outcome.Names) {
            QStringList list(names.begin(), names.end());
            qInfo() << "  row" << row << "names:" << list;
        }
        for (const auto &[row, names]: outcome.NamedElements) {
            QStringList list(names.begin(), names.end());
            qInfo() << "  row" << row << "Elements:" << list;
        }
        return outcome;
    }
}

class DocumentMergeTests : public QObject {
    Q_OBJECT

private slots:
    void baseline() {
        // Different packages, qualified reference: the plain case works.
        const auto outcome = run({sysml(0, "package A { part def X; }"), sysml(1, "package B { part y : A::X; }")}, "baseline: different packages");
        QCOMPARE(outcome.count(), 0);
        QVERIFY(outcome.bound(1, "y -> A::X"));
        QCOMPARE(outcome.rowOfTarget(1, "y -> A::X"), 0);
    }

    // ---- block 0 = `package A { part def X; }`, block 1 continues package A ----

    // CHARACTERIZATION: current behaviour, expected to change in phase 3
    void continuedPackageUnqualified() {
        // Block 1 re-opens package A and refers to X, which only block 0 declares inside A.
        const auto outcome = run({sysml(0, "package A { part def X; }"), sysml(1, "package A { part y : X; }")}, "continued package, unqualified");
        // Observed: an unqualified lookup (through the enclosing namespace A of block 1) does not see X, which another block declares in a
        // package of the same name: one warning (severity 2) on row 1, none on row 0.
        QCOMPARE(outcome.count(), 1);
        const auto diagnostic = outcome.Result.Diagnostics.first();
        QCOMPARE(diagnostic.Row, 1);
        QCOMPARE(diagnostic.Severity, 2);
        QVERIFY2(diagnostic.Message.startsWith(QStringLiteral("unresolved reference to type 'X'")), qPrintable(diagnostic.Message));
        qInfo() << "  full message:" << diagnostic.Message;
        // No complaint about the duplicate package name.
        for (const auto &message: outcome.messages(1))
            QVERIFY2(!message.contains(QStringLiteral("duplicate"), Qt::CaseInsensitive), qPrintable(message));
        // The model of each row holds its own package A with only its own member.
        QVERIFY(outcome.has(0, "A::X") && !outcome.has(0, "A::y"));
        QVERIFY(outcome.has(1, "A::y") && !outcome.has(1, "A::X"));
        // The feature is bound to an unnamed placeholder outside every row (asserted below), not to X.
        QVERIFY(!outcome.bound(1, "y -> A::X"));
        QVERIFY(outcome.bound(1, "y -> "));
        QCOMPARE(outcome.rowOfTarget(1, "y -> "), -1);
    }

    // CHARACTERIZATION: current behaviour, expected to change in phase 3
    void continuedPackageSelfImportInsideFails() {
        // Observed: the self-import `import A::*` inside the continued package A does not help (this is the only failing variant besides
        // the plain unqualified one; the other forms below resolve).
        const auto outcome = run({sysml(0, "package A { part def X; }"), sysml(1, "package A { private import A::*; part y : X; }")},
                                 "continued package, import A::* inside A");
        QCOMPARE(outcome.count(), 1);
        QCOMPARE(outcome.Result.Diagnostics.first().Row, 1);
        QVERIFY(outcome.Result.Diagnostics.first().Message.startsWith(QStringLiteral("unresolved reference to type 'X'")));
        QVERIFY(!outcome.bound(1, "y -> A::X"));
        QVERIFY(outcome.bound(1, "y -> "));
        QCOMPARE(outcome.rowOfTarget(1, "y -> "), -1);
    }

    void continuedPackageImportOfTheMember() {
        const auto outcome = run({sysml(0, "package A { part def X; }"), sysml(1, "package A { private import A::X; part y : X; }")},
                                 "continued package, import A::X inside A");
        QCOMPARE(outcome.count(), 0);
        QVERIFY(outcome.bound(1, "y -> A::X"));
        QCOMPARE(outcome.rowOfTarget(1, "y -> A::X"), 0);
    }

    void continuedPackageTopLevelImport() {
        const auto outcome = run({sysml(0, "package A { part def X; }"), sysml(1, "private import A::*; package A { part y : X; }")},
                                 "continued package, import A::* at the top level of the block");
        QCOMPARE(outcome.count(), 0);
        QVERIFY(outcome.bound(1, "y -> A::X"));
        QCOMPARE(outcome.rowOfTarget(1, "y -> A::X"), 0);
    }

    void continuedPackageQualifiedInside() {
        const auto outcome = run({sysml(0, "package A { part def X; }"), sysml(1, "package A { part y : A::X; }")},
                                 "continued package, A::X inside A");
        QCOMPARE(outcome.count(), 0);
        QVERIFY(outcome.bound(1, "y -> A::X"));
        QCOMPARE(outcome.rowOfTarget(1, "y -> A::X"), 0);
    }

    void otherPackageWithImport() {
        const auto outcome = run({sysml(0, "package A { part def X; }"), sysml(1, "package A2 { private import A::*; part y : X; }")},
                                 "other package A2 with import A::*");
        QCOMPARE(outcome.count(), 0);
        QVERIFY(outcome.bound(1, "y -> A::X"));
        QCOMPARE(outcome.rowOfTarget(1, "y -> A::X"), 0);
    }

    void continuedPackageQualified() {
        // Package A is split over two blocks; a third block with package B uses A::X and A::Z.
        const auto outcome = run({sysml(0, "package A { part def X; }"), sysml(1, "package A { part def Z; }"),
                                  sysml(2, "package B { part y : A::X; part z : A::Z; }")}, "continued package, qualified from third block");
        // Observed: both qualified names resolve, no diagnostics, each to the block that declares it.
        QCOMPARE(outcome.count(), 0);
        QVERIFY(outcome.has(2, "B::y") && outcome.has(2, "B::z"));
        QVERIFY(outcome.bound(2, "y -> A::X") && outcome.bound(2, "z -> A::Z"));
        QCOMPARE(outcome.rowOfTarget(2, "y -> A::X"), 0);
        QCOMPARE(outcome.rowOfTarget(2, "z -> A::Z"), 1);
    }

    // CHARACTERIZATION: current behaviour, expected to change in phase 3
    void duplicateDeclarationInTwoBlocks() {
        // A::X is declared in block 0 and in block 2; block 1 uses A::X.
        const auto outcome = run({sysml(0, "package A { part def X; }"), sysml(1, "package B { part y : A::X; }"),
                                  sysml(2, "package A { part def X; }")}, "A::X declared in block 0 and block 2");
        // Observed: no diagnostic (no duplicate / ambiguity report).
        QCOMPARE(outcome.count(), 0);
        QVERIFY(outcome.bound(1, "y -> A::X"));
        const int winner = outcome.rowOfTarget(1, "y -> A::X");
        qInfo() << "  winner row:" << winner;
        QCOMPARE(winner, 0);   // the first block in source order wins (observed sysmllib behaviour, not specified in Workspace.h; in the app row order equals source order); row 2 is silently shadowed
    }

    // ---- KerML / SysML ----

    void sysmlOnKerml() {
        const auto outcome = run({kerml(0, "package K { classifier C; }"), sysml(1, "package S { part def D :> K::C; part p : K::C; }")}, "SysML on KerML");
        // Observed: resolves, no diagnostics (all blocks are on one level, the order does not matter), bound to the KerML block.
        QCOMPARE(outcome.count(), 0);
        QVERIFY(outcome.has(0, "K::C"));
        QVERIFY(outcome.has(1, "S::D") && outcome.has(1, "S::p"));
        QVERIFY(outcome.bound(1, "D -> K::C") && outcome.bound(1, "p -> K::C"));
        QCOMPARE(outcome.rowOfTarget(1, "D -> K::C"), 0);
        QCOMPARE(outcome.rowOfTarget(1, "p -> K::C"), 0);
    }

    void kermlOnSysml() {
        const auto outcome = run({sysml(0, "package S { part def D; }"), kerml(1, "package K { classifier C specializes S::D; }")}, "KerML on SysML");
        QCOMPARE(outcome.count(), 0);
        QVERIFY(outcome.has(1, "K::C"));
        QVERIFY(outcome.bound(1, "C -> S::D"));
        QCOMPARE(outcome.rowOfTarget(1, "C -> S::D"), 0);
    }

    void forwardReference() {
        // Block 0 uses what only the later block 1 declares.
        const auto outcome = run({sysml(0, "package A { part y : B::X; }"), sysml(1, "package B { part def X; }")}, "forward reference");
        // Observed: resolves, no diagnostics (no ordering between the blocks), bound to the later block.
        QCOMPARE(outcome.count(), 0);
        QVERIFY(outcome.bound(0, "y -> B::X"));
        QCOMPARE(outcome.rowOfTarget(0, "y -> B::X"), 1);
    }

    void unknownNameIsReportedOnItsRow() {
        // Control: a name that no block declares is reported on the row that uses it.
        const auto outcome = run({sysml(0, "package A { part def X; }"), sysml(1, "package B { part y : A::Nope; }")}, "unknown name");
        QVERIFY(outcome.count() >= 1);
        for (const auto &d: outcome.Result.Diagnostics)
            QCOMPARE(d.Row, 1);
        QVERIFY(!outcome.bound(1, "y -> A::X"));
        QVERIFY(outcome.bound(1, "y -> "));
        QCOMPARE(outcome.rowOfTarget(1, "y -> "), -1);
    }

    // ---- per-row models ----

    void perRowProvenance() {
        const auto outcome = run({sysml(0, "package A { part def X; }"), sysml(1, "package B { part def Y; part def W; }")}, "per-row provenance");
        QCOMPARE(outcome.count(), 0);
        QVERIFY(outcome.HasRoot.at(0) && outcome.HasRoot.at(1));
        QCOMPARE(outcome.Names.at(0), (std::set<QString>{"A", "A::X"}));
        QCOMPARE(outcome.Names.at(1), (std::set<QString>{"B", "B::W", "B::Y"}));
        // WorkspaceRow::Elements is flat (the package and its members), also only of the own block.
        QCOMPARE(outcome.NamedElements.at(0), (std::set<QString>{"A", "X"}));
        QCOMPARE(outcome.NamedElements.at(1), (std::set<QString>{"B", "W", "Y"}));
    }

    void perRowProvenanceOfContinuedPackage() {
        // Two blocks that both declare package A: each row holds only its own part of A.
        const auto outcome = run({sysml(0, "package A { part def X; }"), sysml(1, "package A { part def Z; }")}, "per-row provenance, continued package");
        QCOMPARE(outcome.Names.at(0), (std::set<QString>{"A", "A::X"}));
        QCOMPARE(outcome.Names.at(1), (std::set<QString>{"A", "A::Z"}));
        QCOMPARE(outcome.count(), 0);
    }

    void kermlRowHasNamedRootNamespaceElement() {
        // A KerML block lists the root namespace "Root Namespace" among its Elements (a SysML block does not).
        const auto outcome = run({kerml(0, "package K { classifier C; }"), sysml(1, "package S { part def D; }")}, "KerML Elements");
        QVERIFY(outcome.NamedElements.at(0).count("Root Namespace") == 1);
        QVERIFY(outcome.NamedElements.at(1).count("Root Namespace") == 0);
    }
};

QTEST_GUILESS_MAIN(DocumentMergeTests)

#include "tst_DocumentMerge.moc"
