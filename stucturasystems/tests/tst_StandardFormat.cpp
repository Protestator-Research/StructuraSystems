//
// The standard library of sysmllib in the two formats (text, XMI): a document that is resolved against either has to give the same
// diagnostics and the same names for the code completion (NameIndex). The test reports every difference.
//
// It builds a workspace for each format (parsing the text library takes about a minute in a debug build, reading the XMI half of that).
//

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <tuple>
#include <vector>

#include <QtTest>

#include "../src/Models/Libraries/DocumentWorkspace.h"
#include "../src/Models/Libraries/LibraryWorkspace.h"

using namespace StructuraSystems::Client;

namespace {
    DocumentSource block(const QString &text, int row = 0) {
        DocumentSource source;
        source.Row = row;
        source.Text = text.toStdString();
        return source;
    }

    using Diagnostic = std::tuple<int, int, int, int, QString>;

    std::vector<Diagnostic> diagnosticsOf(const WorkspaceResult &result) {
        std::vector<Diagnostic> list;
        for (const auto &d: result.Diagnostics)
            list.emplace_back(d.Row, d.Line, d.Column, d.Severity, d.Message);
        std::sort(list.begin(), list.end());
        return list;
    }

    QString describe(const Diagnostic &d) {
        return QString("row %1, line %2, column %3, severity %4: %5").arg(std::get<0>(d)).arg(std::get<1>(d)).arg(std::get<2>(d)).arg(std::get<3>(d)).arg(std::get<4>(d));
    }

    QString describe(const NameEntry &entry) {
        QStringList exports = entry.Exports;
        std::sort(exports.begin(), exports.end());
        return QString("%1 | %2 | type=%3 feature=%4 namespace=%5 | exports=[%6]")
                .arg(entry.QualifiedName, entry.Type).arg(entry.IsType).arg(entry.IsFeature).arg(entry.IsNamespace).arg(exports.join(','));
    }

    std::multiset<QString> entriesOf(const NameIndex &index) {
        std::multiset<QString> set;
        for (const auto &entry: index.entries())
            set.insert(describe(entry));
        return set;
    }

    std::set<QString> qualifiedNames(const NameIndex &index, const QList<int> &indices) {
        std::set<QString> set;
        for (const int i: indices)
            set.insert(index.entries().at(i).QualifiedName);
        return set;
    }

    /// Writes the differences (the first ones in full, and how many there are per package) and returns their number.
    template<class T>
    int report(const QString &what, const std::multiset<T> &text, const std::multiset<T> &xmi) {
        std::vector<T> onlyText, onlyXmi;
        std::set_difference(text.begin(), text.end(), xmi.begin(), xmi.end(), std::back_inserter(onlyText));
        std::set_difference(xmi.begin(), xmi.end(), text.begin(), text.end(), std::back_inserter(onlyXmi));
        const auto write = [&what](const char *side, const std::vector<T> &items) {
            if (items.empty())
                return;
            std::map<QString, int> perPackage;
            for (const auto &item: items)
                ++perPackage[QString(item).section(QStringLiteral("::"), 0, 0).section(QLatin1Char(' '), 0, 0).section(QLatin1Char('|'), 0, 0)];
            QStringList summary;
            for (const auto &[package, count]: perPackage)
                summary << QString("%1 (%2)").arg(package).arg(count);
            qWarning().noquote() << what << ":" << items.size() << "only with the" << side << "library, by package:" << summary.join(", ");
            for (size_t i = 0; i < items.size() && i < 8; ++i)
                qWarning().noquote() << "   " << what << ": only with the" << side << "library:" << items[i];
        };
        write("text", onlyText);
        write("XMI", onlyXmi);
        return static_cast<int>(onlyText.size() + onlyXmi.size());
    }

    std::multiset<QString> asSet(const std::vector<Diagnostic> &list) {
        std::multiset<QString> set;
        for (const auto &d: list)
            set.insert(describe(d));
        return set;
    }

    std::multiset<QString> asSet(const std::set<QString> &list) {
        return std::multiset<QString>(list.begin(), list.end());
    }

    struct Document {
        QString Name;
        QString Text;
    };

    /** Documents: models of the kind that the editor holds (from the test data of the conformance tests of sysmllib, shortened), and two with problems. */
    std::vector<Document> documents() {
        return {
            {"imports", "package P { import ISQ::*; import SI::*; import ScalarValues::*; attribute m : MassValue; attribute l : LengthValue = 3 [m]; attribute r : Real; }"},
            {"part_usage", "package Vehicles { import ScalarValues::*; part def Vehicle { attribute mass : Real; part wheels : Wheel[4]; } part def Wheel; part car : Vehicle { attribute :>> mass = 1200; } }"},
            {"port_usage", "package Ports { import ISQ::*; port def PowerPort { attribute voltage : ElectricPotentialDifferenceValue; in attribute current : ElectricCurrentValue; } part def Engine { port supply : PowerPort; port out : ~PowerPort; } }"},
            {"action_def", "package A { import ScalarValues::*; action def Drive { in attribute target : Real; out attribute distance : Real; action step1; then action step2; } action drive : Drive { in target = 5; } }"},
            {"state_def", "package S { state def Machine { entry action start; state idle; state running; transition t1 first idle then running; } state machine : Machine; }"},
            {"requirement_usage", "package R { import ScalarValues::*; requirement def MassLimit { attribute limit : Real; require constraint { limit > 0 } } requirement massLimit : MassLimit; part def Car { attribute mass : Real; } satisfy massLimit by Car; }"},
            {"constraint_calc", "package C { import ScalarValues::*; constraint def Positive { in x : Real; x > 0 } calc def Double { in x : Real; return : Real = x * 2; } attribute a : Real = Double(3.0); }"},
            {"connection_flow", "package F { import ScalarValues::*; item def Fluid; part def Pipe { port inlet; port outlet; } part pipe1 : Pipe; part pipe2 : Pipe; connect pipe1.outlet to pipe2.inlet; flow of Fluid from pipe1.outlet to pipe2.inlet; }"},
            {"metadata", "package M { import Metaobjects::*; metadata def Info { attribute text : ScalarValues::String; } part p { @Info { text = \"x\"; } } }"},
            {"enum_interface", "package E { enum def Color { enum red; enum green; } interface def Link { end a; end b; } part def X { attribute c : Color = Color::red; } }"},
            {"shadows_library", "package Base { part def Mine; } package U { import Base::*; part m : Mine; part q : Parts::Part; }"},
            {"unresolved", "package Broken { import Missing::*; part x : Nope; attribute y : ScalarValues::Missing; part z : ISQ::Missing; }"},
            {"syntax_error", "package Bad { part def ;;; attribute def A :> ; part x : }"},
        };
    }
}

class StandardFormatTests : public QObject {
    Q_OBJECT

private slots:
    void aWorkspaceKeepsTheFormatItWasCreatedWith() {
        // (the standard library is read when the workspace is built, which can be long after it was created: no mix of formats)
        const auto before = LibraryWorkspace::standardFormat();
        LibraryWorkspace::setStandardFormat(StandardFormat::Xmi);
        DocumentWorkspace xmiWorkspace;
        LibraryWorkspace::setStandardFormat(StandardFormat::Text);
        DocumentWorkspace textWorkspace;
        LibraryWorkspace::setStandardFormat(StandardFormat::Xmi);
        QCOMPARE(xmiWorkspace.standardFormat(), StandardFormat::Xmi);
        QCOMPARE(textWorkspace.standardFormat(), StandardFormat::Text);
        LibraryWorkspace::setStandardFormat(before);
        QCOMPARE(LibraryWorkspace::standardFormat(), StandardFormat::Xmi);   // (the default of the editor)
    }

    void sameDiagnosticsAndNamesInBothFormats() {
        QElapsedTimer timer;
        auto build = [&](StandardFormat format, const char *name) {
            LibraryWorkspace::setStandardFormat(format);
            auto workspace = std::make_unique<DocumentWorkspace>();
            timer.restart();
            workspace->warmUp();
            qInfo() << name << "library: warm up" << timer.elapsed() << "ms," << LibraryWorkspace::standardLibrary().size() << "files";
            return workspace;
        };
        auto text = build(StandardFormat::Text, "text");
        auto xmi = build(StandardFormat::Xmi, "XMI");
        LibraryWorkspace::setStandardFormat(StandardFormat::Text);
        QVERIFY(text != nullptr && xmi != nullptr);

        int differences = 0;
        int diagnosticDifferences = 0;
        size_t documentsChecked = 0;
        for (const auto &document: documents()) {
            const std::vector<DocumentSource> sources{block(document.Text)};
            const auto textResult = text->update(sources, "doc");
            const auto xmiResult = xmi->update(sources, "doc");
            const QString what = document.Name;
            diagnosticDifferences += report(what + " diagnostics", asSet(diagnosticsOf(textResult)), asSet(diagnosticsOf(xmiResult)));
            QVERIFY(!textResult.Rows.empty());
            QCOMPARE(xmiResult.Rows.size(), textResult.Rows.size());
            QCOMPARE(xmiResult.Rows[0].HasSyntaxErrors, textResult.Rows[0].HasSyntaxErrors);
            QCOMPARE(xmiResult.NeedsRemoteDownload, textResult.NeedsRemoteDownload);

            const auto textNames = text->buildNameIndex();
            const auto xmiNames = xmi->buildNameIndex();
            QVERIFY(textNames != nullptr && xmiNames != nullptr);
            differences += report(what + " name index", entriesOf(*textNames), entriesOf(*xmiNames));
            if (documentsChecked == 0 && !qEnvironmentVariableIsEmpty("STANDARD_FORMAT_DUMP_DIR")) {
                // (for looking at the differences: all entries of both indices, one per line, sorted)
                const QString directory = QString::fromLocal8Bit(qgetenv("STANDARD_FORMAT_DUMP_DIR"));
                for (const auto &[name, set]: {std::pair<QString, std::multiset<QString>>{"text", entriesOf(*textNames)}, {"xmi", entriesOf(*xmiNames)}}) {
                    QFile file(directory + "/names_" + name + ".txt");
                    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                        for (const auto &line: set)
                            file.write(line.toUtf8() + "\n");
                    }
                }
            }
            for (const auto &owner: {QString("ISQ"), QString("SI"), QString("ScalarValues"), QString("Parts"), QString("Base")}) {
                differences += report(what + " visibleMembers(" + owner + ")", asSet(qualifiedNames(*textNames, textNames->visibleMembers(owner))),
                                      asSet(qualifiedNames(*xmiNames, xmiNames->visibleMembers(owner))));
            }
            differences += report(what + " types()", asSet(qualifiedNames(*textNames, textNames->types())), asSet(qualifiedNames(*xmiNames, xmiNames->types())));
            differences += report(what + " namespaces()", asSet(qualifiedNames(*textNames, textNames->namespaces())),
                                  asSet(qualifiedNames(*xmiNames, xmiNames->namespaces())));
            ++documentsChecked;
        }
        qInfo() << documentsChecked << "documents compared," << diagnosticDifferences << "differences of the diagnostics," << differences << "differences of the names";
        QCOMPARE(diagnosticDifferences, 0);
        QCOMPARE(differences, 0);
    }
};

QTEST_GUILESS_MAIN(StandardFormatTests)

#include "tst_StandardFormat.moc"
