//
// Created by Moritz Herzog on 24.10.26.
//
// Tests of the library resolution: standard library only, import chains over several files, cycles, manual levels,
// ambiguous packages and the incremental behaviour of a document workspace.
//

#include <chrono>
#include <map>
#include <memory>
#include <stdexcept>
#include <vector>

#include <sysmlv2/ParserError.h>
#include <sysmlv2/Workspace.h>
#include <sysmlv2/rest/entities/Commit.h>
#include <sysmlv2/rest/entities/CommitRequest.h>
#include <sysmlv2/rest/entities/Project.h>
#include <sysmlv2/rest/entities/ProjectUsage.h>
#include <boost/uuid/string_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <nlohmann/json.hpp>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

#include "../src/Models/Libraries/CodeScanner.h"
#include "../src/Models/Libraries/DocumentWorkspace.h"
#include "../src/Models/Libraries/LibraryWorkspace.h"
#include "../src/Models/Libraries/RemoteLibraries.h"
#include "../src/Models/Libraries/WorkspaceConfig.h"
#include "../src/Services/BECommunicationService.h"

using namespace StructuraSystems::Client;

namespace {
    DocumentSource sysml(const QString &text, int row = 0) {
        DocumentSource source;
        source.Row = row;
        source.Text = text.toStdString();
        return source;
    }


    const QString Project0 = QStringLiteral("00000000-0000-0000-0000-000000000010");
    const QString Commit0 = QStringLiteral("00000000-0000-0000-0000-0000000000c0");
    const QString Project1 = QStringLiteral("00000000-0000-0000-0000-000000000011");
    const QString Commit1 = QStringLiteral("00000000-0000-0000-0000-0000000000c1");
    const QString Project2 = QStringLiteral("00000000-0000-0000-0000-000000000012");
    const QString Commit2 = QStringLiteral("00000000-0000-0000-0000-0000000000c2");
    const QString Project3 = QStringLiteral("00000000-0000-0000-0000-000000000013");
    const QString Commit3 = QStringLiteral("00000000-0000-0000-0000-0000000000c3");
    const QString Project4 = QStringLiteral("00000000-0000-0000-0000-000000000014");
    const QString Commit4 = QStringLiteral("00000000-0000-0000-0000-0000000000c4");

    RemoteUsageRef ref(const QString &project, const QString &commit, const QString &name) {
        RemoteUsageRef usage;
        usage.ProjectId = project;
        usage.CommitId = commit;
        usage.ProjectName = name;
        return usage;
    }

    QString nodeId(const QString &project, const QString &commit) {
        return project + QLatin1Char('@') + commit;
    }

    /** A server that holds projects and answers the fetches of the remote libraries; counts them. No network. */
    struct FakeServer {
        struct Entry {
            RemoteLibraryContent Content;
        };
        std::map<QString, Entry> Entries;
        QMap<QString, QString> Failing;   ///< node id -> error text
        bool Offline = false;
        int Fetches = 0;
        int UsageCounter = 0;

        void add(const QString &project, const QString &commit, const QString &name, const QList<RemoteUsageRef> &usages,
                 const QStringList &blocks) {
            RemoteLibraryContent content;
            content.ProjectId = project;
            content.CommitId = commit;
            content.ProjectName = name;
            for (auto usage : usages) {
                usage.UsageId = QStringLiteral("00000000-0000-0000-0000-%1").arg(++UsageCounter, 12, 10, QLatin1Char('0'));
                content.Usages.append(usage);
            }
            for (const auto &block : blocks)
                content.Sources.append({QStringLiteral("block"), QStringLiteral("SysMLv2"), block});
            Entries[nodeId(project, commit)] = {content};
        }

        RemoteLibraries::Fetcher fetcher() {
            return [this](const QString &project, const QString &commit, bool, const std::atomic<bool> &) {
                ++Fetches;
                if (Offline)
                    throw std::runtime_error("offline");
                const auto key = nodeId(project, commit);
                if (Failing.contains(key))
                    throw std::runtime_error(Failing.value(key).toStdString());
                const auto it = Entries.find(key);
                if (it == Entries.end())
                    throw std::runtime_error("HTTP 404");
                return it->second.Content;
            };
        }
    };

    bool mentions(const WorkspaceResult &result, const QString &needle) {
        for (const auto &diagnostic : result.Diagnostics)
            if (diagnostic.Message.contains(needle))
                return true;
        return false;
    }

    int warningsAndErrors(const WorkspaceResult &result) {
        for (const auto &diagnostic : result.Diagnostics)
            qInfo() << "diagnostic:" << diagnostic.Row << diagnostic.Line << diagnostic.Column << diagnostic.Severity << diagnostic.Message;
        return int(result.Diagnostics.size());
    }

    /**
     * Parsing the whole standard library takes seconds (about a minute in a debug build), so the tests that do not need it
     * use an empty one. onlyStandardLibraryReadsNoFile uses the real one.
     */
    std::shared_ptr<const std::vector<StandardSource>> noStandardLibrary() {
        static const auto empty = std::make_shared<const std::vector<StandardSource>>();
        return empty;
    }
}

class LibraryWorkspaceTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir Folder;
    std::shared_ptr<LibraryWorkspace> Libraries;

    void write(const QString &relative, const QString &content) {
        const auto path = Folder.filePath(relative);
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(content.toUtf8());
    }

    void clearFolder() {
        QDir(Folder.path()).removeRecursively();
        QDir().mkpath(Folder.path());
    }

private slots:
    void init() {
        QVERIFY(Folder.isValid());
        clearFolder();
        Libraries = std::make_shared<LibraryWorkspace>();
        Libraries->setRoot(Folder.path());
    }

    // ---- scanner ----

    void scannerFindsTopLevelPackagesAndRoots() {
        const auto scan = CodeScanner::scan(QStringLiteral(
                "// package Hidden { }\n"
                "/* package AlsoHidden { } */\n"
                "standard library package <s> 'Std Lib' { package Nested { } part x : Other::Thing; }\n"
                "library package Lib { import A::B::*; }\n"
                "package Plain;\n"));
        QCOMPARE(scan.Packages.size(), 3);
        QCOMPARE(scan.Packages[0].Name, QStringLiteral("Std Lib"));
        QVERIFY(scan.Packages[0].IsLibrary);
        QCOMPARE(scan.Packages[1].Name, QStringLiteral("Lib"));
        QVERIFY(scan.Packages[1].IsLibrary);
        QCOMPARE(scan.Packages[2].Name, QStringLiteral("Plain"));
        QVERIFY(!scan.Packages[2].IsLibrary);
        QCOMPARE(scan.References, (QStringList{QStringLiteral("Other"), QStringLiteral("A")}));
    }

    void markdownCodeBlocksAreScanned() {
        write("notes.md", "# Title\n\nSome text with package Fake { }\n\n```sysml\npackage FromMarkdown { part def P; }\n```\n\n```yaml\npackage NotCode\n```\n");
        const auto scan = CodeScanner::scanFile(Folder.filePath("notes.md"));
        QCOMPARE(scan.Packages.size(), 1);
        QCOMPARE(scan.Packages[0].Name, QStringLiteral("FromMarkdown"));
    }

    void configIsStoredInTheWorkspaceFile() {
        WorkspaceConfig config;
        config.Levels.insert("a/b.sysml", 3);
        config.PreferredSources.insert("P", "x.sysml");
        QVERIFY(config.save(Folder.path()));
        QVERIFY(QFile::exists(Folder.filePath(".structurasystems/workspace.json")));

        const auto loaded = WorkspaceConfig::load(Folder.path());
        QCOMPARE(loaded.Levels.value("a/b.sysml"), 3);
        QCOMPARE(loaded.PreferredSources.value("P"), QStringLiteral("x.sysml"));

        QVERIFY(WorkspaceConfig().save(Folder.path()));
        QVERIFY(!QFile::exists(Folder.filePath(".structurasystems/workspace.json")));
    }

    // ---- the standard library ----

    void standardLibraryParsesWithoutSyntaxErrors() {
        const auto &library = LibraryWorkspace::standardLibrary();
        QVERIFY(library.size() > 80);
        QElapsedTimer timer;
        timer.start();
        SysMLv2::Files::Workspace space;
        for (const auto &source : library)
            space.addText(source.Text, source.Name, source.KerML ? SysMLv2::Files::SourceLanguage::KerML : SysMLv2::Files::SourceLanguage::SysML);
        const auto parsed = timer.elapsed();
        space.resolve();
        qInfo() << "standard library:" << library.size() << "files, parsed in" << parsed << "ms, resolved in" << timer.elapsed() - parsed << "ms";
        for (const auto &error : space.errors())
            qWarning() << QString::fromStdString(error->getSource()) << error->getLine() << QString::fromStdString(error->description());
        QCOMPARE(space.errors().size(), size_t(0));
    }

    // ---- resolution ----

    void onlyStandardLibraryReadsNoFile() {
        write("lib.sysml", "library package Lib { part def Unused; }");
        write("other.md", "```sysml\npackage Other { }\n```\n");

        DocumentWorkspace workspace(Libraries);
        const auto result = workspace.update({sysml("package Doc { import ScalarValues::*; attribute x : Real; }")}, "doc");

        QCOMPARE(warningsAndErrors(result), 0);
        QCOMPARE(result.Rows.size(), size_t(1));
        QVERIFY(!result.Rows[0].Elements.empty());
        QVERIFY(result.Rows[0].Root != nullptr);
        QVERIFY(workspace.loadedLibraryFiles().isEmpty());
        QCOMPARE(Libraries->fileReadCount(), 0);
        QCOMPARE(Libraries->indexBuildCount(), 0);
        QVERIFY(!Libraries->indexBuilt());
    }

    void syntaxErrorsKeepTheirPosition() {
        DocumentWorkspace workspace(Libraries, QString(), noStandardLibrary());
        const auto result = workspace.update({sysml("package Doc {\n part def ;;; \n}")}, "doc");
        QVERIFY(!result.Diagnostics.isEmpty());
        QVERIFY(result.Rows[0].HasSyntaxErrors);
        QCOMPARE(result.Diagnostics[0].Severity, 3);
        QVERIFY(result.Diagnostics[0].Line >= 1);
        QCOMPARE(Libraries->fileReadCount(), 0);   // no unresolved names, no need for the folder
    }

    void importChainOverThreeFiles() {
        write("a.sysml", "package A { import B::*; part def X :> B::Y; }");
        write("b.sysml", "package B { import C::*; part def Y :> C::Z; }");
        write("c.sysml", "package C { part def Z; }");
        write("unrelated.sysml", "package Unrelated { part def U; }");

        DocumentWorkspace workspace(Libraries, QString(), noStandardLibrary());
        const auto result = workspace.update({sysml("package Doc { import A::*; part p : X; }")}, "doc");

        for (const auto &diagnostic : result.Diagnostics)
            qInfo() << diagnostic.Message;
        QCOMPARE(warningsAndErrors(result), 0);
        QCOMPARE(workspace.loadedLibraryFiles().size(), 3);
        QVERIFY(!workspace.loadedLibraryFiles().contains("unrelated.sysml"));
        QCOMPARE(Libraries->levelInfo("c.sysml").Level, 1);
        QCOMPARE(Libraries->levelInfo("b.sysml").Level, 2);
        QCOMPARE(Libraries->levelInfo("a.sysml").Level, 3);
        QVERIFY(LibraryWorkspace::DocumentLevel > Libraries->levelInfo("a.sysml").Level);
    }

    void unresolvedImportIsExplained() {
        write("a.sysml", "package A { part def X; }");

        DocumentWorkspace workspace(Libraries, QString(), noStandardLibrary());
        const auto result = workspace.update({sysml("package Doc {\n import Missing::*;\n import A::Nope;\n}")}, "doc");

        QVERIFY(mentions(result, "package 'Missing' was not found"));
        QVERIFY(mentions(result, "does not contain this member"));
        for (const auto &diagnostic : result.Diagnostics) {
            qInfo() << diagnostic.Line << diagnostic.Column << diagnostic.Message;
            QCOMPARE(diagnostic.Severity, 2);
        }
    }

    void importCycleSharesALevel() {
        write("x.sysml", "package X { import Y::*; part def PX :> Y::PY; }");
        write("y.sysml", "package Y { import X::*; part def PY; }");

        DocumentWorkspace workspace(Libraries, QString(), noStandardLibrary());
        const auto result = workspace.update({sysml("package Doc { import X::*; part p : PX; }")}, "doc");

        QCOMPARE(workspace.loadedLibraryFiles().size(), 2);
        QVERIFY(Libraries->levelInfo("x.sysml").InCycle);
        QCOMPARE(Libraries->levelInfo("x.sysml").Level, Libraries->levelInfo("y.sysml").Level);
        QVERIFY(mentions(result, "import cycle"));
        QVERIFY(!mentions(result, "cannot be resolved"));
    }

    void manualLevelOverridesAndIsChecked() {
        write("a.sysml", "package A { import B::*; part def X :> B::Y; }");
        write("b.sysml", "package B { part def Y; }");

        DocumentWorkspace workspace(Libraries, QString(), noStandardLibrary());
        auto result = workspace.update({sysml("package Doc { import A::*; part p : X; }")}, "doc");
        QCOMPARE(warningsAndErrors(result), 0);
        QCOMPARE(Libraries->levelInfo("a.sysml").Level, 2);

        // Raising the level of b above a: a can no longer see b.
        Libraries->setLevelOverride("b.sysml", 5);
        QVERIFY(Libraries->lastSaveSucceeded());
        QVERIFY(QFile::exists(Folder.filePath(".structurasystems/workspace.json")));
        QVERIFY(Libraries->levelInfo("b.sysml").Overridden);
        QCOMPARE(Libraries->levelInfo("b.sysml").Level, 5);
        QCOMPARE(Libraries->levelInfo("a.sysml").Level, 6);   // follows automatically

        // Forcing a below b is the wrong way round.
        Libraries->setLevelOverride("a.sysml", 1);
        result = workspace.update({sysml("package Doc { import A::*; part p : X; }")}, "doc");
        QVERIFY(mentions(result, "A library only sees libraries of the same or a lower level"));
        QVERIFY(mentions(result, "cannot be resolved"));

        // The override survives a restart of the folder.
        Libraries->setRoot(Folder.path());
        QCOMPARE(Libraries->levelInfo("a.sysml").Level, 1);

        Libraries->clearLevelOverride("a.sysml");
        Libraries->clearLevelOverride("b.sysml");
        QVERIFY(!QFile::exists(Folder.filePath(".structurasystems/workspace.json")));
        QCOMPARE(Libraries->levelInfo("a.sysml").Level, 2);
    }

    void declaredLibraryPackagesAreListed() {
        write("lib.sysml", "library package Lib { part def L; }");
        write("model.sysml", "package Model { part def M; }");
        const auto libraries = Libraries->libraries();
        QCOMPARE(libraries.size(), 1);
        QCOMPARE(libraries[0].File, QStringLiteral("lib.sysml"));
        QVERIFY(libraries[0].Info.DeclaresLibrary);
        QCOMPARE(Libraries->knownLevel("lib.sysml"), 1);
        QCOMPARE(Libraries->knownLevel("model.sysml"), -1);
    }

    void ambiguousPackageNeedsAPreference() {
        write("one.sysml", "package P { part def FromOne; }");
        write("two.sysml", "package P { part def FromTwo; }");

        DocumentWorkspace workspace(Libraries, QString(), noStandardLibrary());
        const std::vector<DocumentSource> source{sysml("package Doc { import P::*; part p : FromTwo; }")};
        auto result = workspace.update(source, "doc");
        QVERIFY(mentions(result, "defined in several files"));
        QCOMPARE(workspace.loadedLibraryFiles(), QStringList{"one.sysml"});

        Libraries->setPreferredSource("P", "two.sysml");
        result = workspace.update(source, "doc");
        QVERIFY(!mentions(result, "defined in several files"));
        QCOMPARE(workspace.loadedLibraryFiles(), QStringList{"two.sysml"});
        QCOMPARE(warningsAndErrors(result), 0);
    }

    void markdownFileCanBeALibrary() {
        write("models.md", "# Models\n\nText\n\n```sysml\npackage FromMd { part def Q; }\n```\n");
        DocumentWorkspace workspace(Libraries, QString(), noStandardLibrary());
        const auto result = workspace.update({sysml("package Doc { import FromMd::*; part p : Q; }")}, "doc");
        QCOMPARE(warningsAndErrors(result), 0);
        QCOMPARE(workspace.loadedLibraryFiles(), QStringList{"models.md"});
    }

    void secondRunKeepsTheLibraries() {
        write("a.sysml", "package A { part def X; }");

        DocumentWorkspace workspace(Libraries, QString(), noStandardLibrary());
        auto result = workspace.update({sysml("package Doc { import A::*; part p : X; }")}, "doc");
        QCOMPARE(warningsAndErrors(result), 0);
        const auto reads = Libraries->fileReadCount();
        const auto builds = Libraries->indexBuildCount();
        QVERIFY(reads > 0);

        // The document changes, the library does not: nothing is read again.
        QElapsedTimer timer;
        timer.start();
        result = workspace.update({sysml("package Doc { import A::*; part p : X; part q : X; }")}, "doc");
        qInfo() << "second run:" << timer.elapsed() << "ms";
        QCOMPARE(warningsAndErrors(result), 0);
        QCOMPARE(Libraries->fileReadCount(), reads);
        QCOMPARE(Libraries->indexBuildCount(), builds);
        QCOMPARE(workspace.loadedLibraryFiles().size(), 1);

        // A new import loads one more library.
        write("b.sysml", "package B { part def Y; }");
        Libraries->invalidate();   // what the file watcher of the application does
        result = workspace.update({sysml("package Doc { import A::*; import B::*; part p : X; part q : Y; }")}, "doc");
        QCOMPARE(warningsAndErrors(result), 0);
        QCOMPARE(workspace.loadedLibraryFiles().size(), 2);
    }

    void changedLibraryFileIsReloaded() {
        write("a.sysml", "package A { part def X; }");

        DocumentWorkspace workspace(Libraries, QString(), noStandardLibrary());
        const std::vector<DocumentSource> source{sysml("package Doc { import A::*; part p : X; part q : Z; }")};
        auto result = workspace.update(source, "doc");
        QVERIFY(mentions(result, "Z"));

        write("a.sysml", "package A { part def X; part def Z; }");
        const auto path = Folder.filePath("a.sysml");
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.setFileTime(QDateTime::currentDateTime().addSecs(10), QFileDevice::FileModificationTime));
        file.close();

        result = workspace.update(source, "doc");
        QVERIFY(!mentions(result, "'Z'"));
        QCOMPARE(warningsAndErrors(result), 0);
    }

    void importWithoutQualifiedNameIsADependency() {
        write("c.sysml", "package C { part def Z; }");
        write("b.sysml", "package B { import C; alias ZZ for C; }");
        write("a.sysml", "package A { import all B; }");

        const auto scan = CodeScanner::scan(QStringLiteral("package B { private import C; import all D as E; alias X for F; }"));
        QCOMPARE(scan.References, (QStringList{QStringLiteral("C"), QStringLiteral("D"), QStringLiteral("F")}));

        QCOMPARE(Libraries->levelInfo("c.sysml").Level, 1);
        QCOMPARE(Libraries->levelInfo("b.sysml").Level, 2);
        QCOMPARE(Libraries->levelInfo("a.sysml").Level, 3);
    }

    void configKeepsUnknownContentAndNormalizesPaths() {
        write(".structurasystems/workspace.json",
              "{\"version\":1,\"levels\":{\"sub\\\\x.sysml\":4},\"preferredSources\":{},\"future\":{\"a\":1}}");
        auto config = WorkspaceConfig::load(Folder.path());
        QCOMPARE(config.levelFor("sub/x.sysml").value_or(-1), 4);
        QVERIFY(config.Unknown.contains("future"));
        config.setLevel("sub/../sub/y.sysml", 2);
        QVERIFY(config.save(Folder.path()));
        const auto again = WorkspaceConfig::load(Folder.path());
        QVERIFY(again.Unknown.contains("future"));
        QCOMPARE(again.levelFor("sub/y.sysml").value_or(-1), 2);
    }

    void recycledWorkspaceIsUsableForAnotherDocument() {
        write("a.sysml", "package A { part def X; }");
        write("b.sysml", "package B { part def Y; }");

        DocumentWorkspace workspace(Libraries, QString(), noStandardLibrary());
        auto result = workspace.update({sysml("package Doc { import A::*; part p : X; }")}, "doc");
        QCOMPARE(warningsAndErrors(result), 0);
        QCOMPARE(workspace.loadedLibraryFiles(), QStringList{"a.sysml"});

        workspace.recycle();
        QVERIFY(workspace.loadedLibraryFiles().isEmpty());
        workspace.setLibraries(Libraries, QString());
        result = workspace.update({sysml("package Other { import B::*; part q : Y; }")}, "other");
        QCOMPARE(warningsAndErrors(result), 0);
        QCOMPARE(workspace.loadedLibraryFiles(), QStringList{"b.sysml"});
    }

    void libraryThatIsNoLongerNeededIsDropped() {
        write("a.sysml", "package A { part def X; }");
        DocumentWorkspace workspace(Libraries, QString(), noStandardLibrary());
        auto result = workspace.update({sysml("package Doc { import A::*; part p : X; }")}, "doc");
        QCOMPARE(workspace.loadedLibraryFiles(), QStringList{"a.sysml"});

        // The file disappears: the library is dropped and the import is reported again.
        QVERIFY(QFile::remove(Folder.filePath("a.sysml")));
        Libraries->invalidate();
        result = workspace.update({sysml("package Doc { import A::*; part p : X; }")}, "doc");
        QVERIFY(workspace.loadedLibraryFiles().isEmpty());
        QVERIFY(mentions(result, "package 'A' was not found"));
    }

    // ---- project usages of an online project (remote libraries) ----

    void remoteLibraryResolvesNamesAndIsCachedForever() {
        QTemporaryDir cache;
        QVERIFY(cache.isValid());
        FakeServer server;
        server.add(Project0, Commit0, "Model", {ref(Project1, Commit1, "Motor")}, {});
        server.add(Project1, Commit1, "Motor", {}, {"package Motor { part def Engine; }"});

        auto remote = std::make_shared<RemoteLibraries>(cache.path(), server.fetcher());
        DocumentWorkspace workspace(nullptr, QString(), noStandardLibrary());
        workspace.setRemote(remote, Project0, Commit0);
        const std::vector<DocumentSource> source{sysml("package Doc { import Motor::*; part e : Engine; }")};

        // Nothing is downloaded by a parser run: it only reads the cache, says what is missing, and asks for a download.
        auto result = workspace.update(source, "doc");
        QVERIFY(result.NeedsRemoteDownload);
        QVERIFY(mentions(result, "not downloaded yet"));
        QCOMPARE(server.Fetches, 0);

        QCOMPARE(remote->ensureClosure(Project0, Commit0), 2);   // the usages of the document's commit, and the used commit
        result = workspace.update(source, "doc");
        QVERIFY(!result.NeedsRemoteDownload);
        QCOMPARE(warningsAndErrors(result), 0);
        QCOMPARE(workspace.loadedLibraryFiles().size(), 1);

        // A second instance (a restart of the application) finds everything in the cache: the server is not asked at all.
        FakeServer offline;
        offline.Offline = true;
        auto again = std::make_shared<RemoteLibraries>(cache.path(), offline.fetcher());
        QCOMPARE(again->ensureClosure(Project0, Commit0), 0);
        QCOMPARE(offline.Fetches, 0);
        DocumentWorkspace second(nullptr, QString(), noStandardLibrary());
        second.setRemote(again, Project0, Commit0);
        result = second.update(source, "doc");
        QCOMPARE(warningsAndErrors(result), 0);
        QCOMPARE(offline.Fetches, 0);
    }

    void remoteLevelsFollowTheChainOfUsagesAndSurviveCycles() {
        QTemporaryDir cache;
        FakeServer server;
        server.add(Project0, Commit0, "Model", {ref(Project1, Commit1, "A")}, {});
        // A uses B and C; B uses C; C uses B again (a cycle that must not loop), and D is a leaf.
        server.add(Project1, Commit1, "A", {ref(Project2, Commit2, "B"), ref(Project4, Commit4, "D")}, {"package A { }"});
        server.add(Project2, Commit2, "B", {ref(Project3, Commit3, "C")}, {"package B { }"});
        server.add(Project3, Commit3, "C", {ref(Project2, Commit2, "B")}, {"package C { }"});
        server.add(Project4, Commit4, "D", {}, {"package D { }"});

        RemoteLibraries remote(cache.path(), server.fetcher());
        remote.ensureClosure(Project0, Commit0);
        const auto closure = remote.closure(Project0, Commit0);
        QVERIFY(closure.RootKnown);
        QVERIFY(!closure.Missing);
        QCOMPARE(closure.Libraries.size(), 4);

        QMap<QString, RemoteLibrary> byName;
        for (const auto &library : closure.Libraries)
            byName.insert(library.Content.ProjectName, library);
        QCOMPARE(byName["D"].Level, 1);
        QCOMPARE(byName["B"].Level, byName["C"].Level);   // the cycle shares a level
        QVERIFY(byName["B"].InCycle && byName["C"].InCycle);
        QCOMPARE(byName["B"].Level, 1);
        QCOMPARE(byName["A"].Level, 2);                   // above everything it uses
        QCOMPARE(closure.MaxLevel, 2);
        QCOMPARE(closure.Libraries.first().Level, 1);     // ordered by level
    }

    void failedDownloadIsReportedNotSilent() {
        QTemporaryDir cache;
        FakeServer server;
        server.add(Project0, Commit0, "Model", {ref(Project1, Commit1, "Motor")}, {});
        server.Failing.insert(nodeId(Project1, Commit1), "HTTP 503");

        auto remote = std::make_shared<RemoteLibraries>(cache.path(), server.fetcher());
        remote->ensureClosure(Project0, Commit0);
        QVERIFY(remote->failure(Project1, Commit1).contains("503"));

        DocumentWorkspace workspace(nullptr, QString(), noStandardLibrary());
        workspace.setRemote(remote, Project0, Commit0);
        const auto result = workspace.update({sysml("package Doc {\n import Motor::*;\n}")}, "doc");
        QVERIFY(mentions(result, "could not be downloaded: HTTP 503"));
        bool hinted = false;
        for (const auto &diagnostic : result.Diagnostics)
            hinted = hinted || (diagnostic.Row == 0 && diagnostic.Message.contains("project usages") && diagnostic.Message.contains("503"));
        QVERIFY(hinted);   // the unresolved import itself carries the explanation

        // The server answers the root fetch with an error: the document's own usages are unknown.
        FakeServer down;
        down.Offline = true;
        QTemporaryDir otherCache;
        auto offline = std::make_shared<RemoteLibraries>(otherCache.path(), down.fetcher());
        offline->ensureClosure(Project0, Commit0);
        DocumentWorkspace second(nullptr, QString(), noStandardLibrary());
        second.setRemote(offline, Project0, Commit0);
        const auto offlineResult = second.update({sysml("package Doc { import Motor::*; }")}, "doc");
        QVERIFY(mentions(offlineResult, "could not be downloaded: offline"));
    }

    void localLibrariesSitAboveRemoteOnes() {
        QTemporaryDir cache;
        FakeServer server;
        server.add(Project0, Commit0, "Model", {ref(Project1, Commit1, "Motor")}, {});
        server.add(Project1, Commit1, "Motor", {}, {"package Motor { part def Engine; }"});
        auto remote = std::make_shared<RemoteLibraries>(cache.path(), server.fetcher());
        remote->ensureClosure(Project0, Commit0);

        // The local library needs the remote one; the document only knows the local one.
        write("local.sysml", "package Local { import Motor::*; part def Car :> Engine; }");
        DocumentWorkspace workspace(Libraries, QString(), noStandardLibrary());
        workspace.setRemote(remote, Project0, Commit0);
        const auto result = workspace.update({sysml("package Doc { import Local::*; part c : Car; }")}, "doc");
        QCOMPARE(warningsAndErrors(result), 0);
        QCOMPARE(workspace.loadedLibraryFiles().size(), 2);
        QVERIFY(workspace.loadedLibraryFiles().contains("local.sysml"));
    }

    void nothingRemoteIsTouchedWhileNamesResolve() {
        QTemporaryDir cache;
        FakeServer server;
        auto remote = std::make_shared<RemoteLibraries>(cache.path(), server.fetcher());
        DocumentWorkspace workspace(nullptr, QString(), noStandardLibrary());
        workspace.setRemote(remote, Project0, Commit0);
        const auto result = workspace.update({sysml("package Doc { part def X; part y : X; }")}, "doc");
        QCOMPARE(warningsAndErrors(result), 0);
        QVERIFY(!result.NeedsRemoteDownload);
        QCOMPARE(server.Fetches, 0);
        QVERIFY(QDir(cache.path()).entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty());
    }

    // ---- how the project usages travel to and from the server ----

    void projectUsagesAreReadWithAQueryOnTheType() {
        const auto usageRow = [](const char *id, const QString &project, const QString &commit, const char *type) {
            return nlohmann::json{{"@id", id}, {"@type", type}, {"usedProjectCommit", {{"@id", commit.toStdString()}}},
                                  {"usedProject", {{"@id", project.toStdString()}}}};
        };
        std::vector<std::string> gets;
        std::vector<std::pair<std::string, std::string>> posts;   // url, body
        CommunicationService service(
            "http://server/",
            [&](const std::string &url, const std::string &) {
                gets.push_back(url);
                return HttpResponse{200, "[]", ""};
            },
            [&](const std::string &url, const std::string &, const std::string &body) {
                posts.emplace_back(url, body);
                // Two pages: the Link header of the first one carries the cursor and keeps the other parameters, like the server does.
                if (posts.size() == 1)
                    return HttpResponse{200, nlohmann::json::array({usageRow("10000000-0000-0000-0000-000000000002", Project1, Commit1, "ProjectUsage")}).dump(),
                                        "Link: <http://server/projects/x/query-results?commitId=" + Commit0.toStdString() + "&page%5Bafter%5D=abc>; rel=\"next\"\r\n"};
                return HttpResponse{200, nlohmann::json::array({usageRow("10000000-0000-0000-0000-000000000003", Project2, Commit2, "projectusage")}).dump(), ""};
            });

        const auto generator = boost::uuids::string_generator();
        const auto data = service.getCommitData(generator(Project0.toStdString()), generator(Commit0.toStdString()), false);

        // A usages-only read asks the query and nothing else: the elements are not requested.
        QVERIFY(gets.empty());
        QCOMPARE(posts.size(), size_t(2));
        QCOMPARE(QString::fromStdString(posts[0].first), QStringLiteral("http://server/projects/%1/query-results?commitId=%2").arg(Project0, Commit0));
        QVERIFY(posts[1].first.find("page%5Bafter%5D=abc") != std::string::npos);
        QVERIFY(posts[1].first.find("commitId=" + Commit0.toStdString()) != std::string::npos);
        QCOMPARE(posts[0].second, posts[1].second);   // every page is asked with the same query

        const auto body = nlohmann::json::parse(posts[0].second);
        qInfo() << QString::fromStdString(posts[0].first) << QString::fromStdString(body.dump());
        QCOMPARE(body["@type"].get<std::string>(), std::string("Query"));
        QCOMPARE(body["where"]["@type"].get<std::string>(), std::string("PrimitiveConstraint"));
        QCOMPARE(body["where"]["property"].get<std::string>(), std::string("@type"));
        QCOMPARE(body["where"]["operator"].get<std::string>(), std::string("="));
        QCOMPARE(body["where"]["value"].get<std::string>(), std::string("ProjectUsage"));

        QCOMPARE(data.Usages.size(), size_t(2));   // the second row has the type in lower case
        QCOMPARE(QString::fromStdString(boost::uuids::to_string(data.Usages[0]->getUsedProject()->getId())), Project1);
        QCOMPARE(QString::fromStdString(boost::uuids::to_string(data.Usages[1]->getUsedProjectCommit()->getId())), Commit2);
        QVERIFY(data.Elements.empty());

        // With texts the elements are requested as well (and only their TextualRepresentations are read).
        gets.clear();
        posts.clear();
        [[maybe_unused]] const auto withTexts = service.getCommitData(generator(Project0.toStdString()), generator(Commit0.toStdString()), true);
        QCOMPARE(gets.size(), size_t(1));
        QVERIFY(gets[0].find("/commits/" + Commit0.toStdString() + "/elements") != std::string::npos);
    }

    void usageChangesAreCommitChanges() {
        const auto generator = boost::uuids::string_generator();
        const auto request = CommunicationService::buildProjectUsageRequest(
            "Update", {generator("10000000-0000-0000-0000-000000000002")}, {{generator(Project1.toStdString()), generator(Commit1.toStdString())}});
        const auto json = nlohmann::json::parse(request->serializeToJson());
        const auto text = QString::fromStdString(json.dump());
        qInfo() << text;
        QVERIFY(text.contains("ProjectUsage"));
        QVERIFY(text.contains(Commit1));
        QVERIFY(text.contains("10000000-0000-0000-0000-000000000002"));   // the deletion refers to the identity of the old usage
        QVERIFY(text.contains("null"));                                   // ... and has no payload
        QVERIFY(!text.contains("previousCommit"));   // the server takes the head of the branch as the parent
        QVERIFY_EXCEPTION_THROWN(CommunicationService::buildProjectUsageRequest("Nothing", {}, {}), std::invalid_argument);
    }

    void entriesWithoutUsagesAreLookedAtOnceMore() {
        QTemporaryDir cache;
        FakeServer server;
        server.add(Project0, Commit0, "Model", {}, {});   // the server did not list the usages (yet)
        RemoteLibraries remote(cache.path(), server.fetcher());
        QCOMPARE(remote.ensureClosure(Project0, Commit0), 1);

        server.add(Project0, Commit0, "Model", {ref(Project1, Commit1, "Motor")}, {});
        server.add(Project1, Commit1, "Motor", {}, {"package Motor { }"});
        // The empty answer is not final: it is read once more (and replaced in place), then the used commit follows.
        QCOMPARE(remote.ensureClosure(Project0, Commit0), 2);
        QCOMPARE(remote.closure(Project0, Commit0).Libraries.size(), 1);
        QCOMPARE(remote.ensureClosure(Project0, Commit0), 0);   // everything is final now
    }

    void remoteLibrariesImportFromTheUsagesNotFromLocalFiles() {
        QTemporaryDir cache;
        FakeServer server;
        server.add(Project0, Commit0, "Model", {ref(Project1, Commit1, "A")}, {});
        server.add(Project1, Commit1, "A", {ref(Project2, Commit2, "B")}, {"package A { import B::*; part def X :> B::Y; }"});
        server.add(Project2, Commit2, "B", {}, {"package B { part def Y; }"});
        auto remote = std::make_shared<RemoteLibraries>(cache.path(), server.fetcher());
        remote->ensureClosure(Project0, Commit0);

        // A local file defines a package B as well; it sits above the remote library A and could never be seen by it.
        write("b.sysml", "package B { part def Y; part def Local; }");
        DocumentWorkspace workspace(Libraries, QString(), noStandardLibrary());
        workspace.setRemote(remote, Project0, Commit0);
        const auto result = workspace.update({sysml("package Doc { import A::*; part p : X; }")}, "doc");

        QVERIFY(!workspace.loadedLibraryFiles().contains("b.sysml"));
        QCOMPARE(workspace.loadedLibraryFiles().size(), 2);   // the remote A and the remote B
        QVERIFY(mentions(result, "exists in the project"));
        QVERIFY(!mentions(result, "cannot be resolved"));
        for (const auto &diagnostic : result.Diagnostics)
            QVERIFY2(diagnostic.Severity == 2 && !diagnostic.Message.contains("Unresolved"), qPrintable(diagnostic.Message));
    }

    void sameRootInSeveralUsagesIsReported() {
        QTemporaryDir cache;
        FakeServer server;
        server.add(Project0, Commit0, "Model", {ref(Project1, Commit1, "One"), ref(Project2, Commit2, "Two")}, {});
        server.add(Project1, Commit1, "One", {}, {"package Dup { part def FromOne; }"});
        server.add(Project2, Commit2, "Two", {}, {"package Dup { part def FromTwo; }"});
        auto remote = std::make_shared<RemoteLibraries>(cache.path(), server.fetcher());
        remote->ensureClosure(Project0, Commit0);

        DocumentWorkspace workspace(nullptr, QString(), noStandardLibrary());
        workspace.setRemote(remote, Project0, Commit0);
        const auto result = workspace.update({sysml("package Doc { import Dup::*; }")}, "doc");
        QVERIFY(mentions(result, "defined by several project usages"));
        QCOMPARE(workspace.loadedLibraryFiles().size(), 1);
    }

    void usedProjectWithoutTextIsReported() {
        QTemporaryDir cache;
        FakeServer server;
        server.add(Project0, Commit0, "Model", {ref(Project1, Commit1, "Empty")}, {});
        server.add(Project1, Commit1, "Empty", {}, {});
        auto remote = std::make_shared<RemoteLibraries>(cache.path(), server.fetcher());
        remote->ensureClosure(Project0, Commit0);

        DocumentWorkspace workspace(nullptr, QString(), noStandardLibrary());
        workspace.setRemote(remote, Project0, Commit0);
        const auto result = workspace.update({sysml("package Doc { import Missing::*; }")}, "doc");
        QVERIFY(mentions(result, "contains no SysML or KerML text"));
    }

    void cacheOfAnotherServerIsNotUsed() {
        QTemporaryDir cache;
        FakeServer server;
        server.add(Project0, Commit0, "Model", {ref(Project1, Commit1, "Motor")}, {});
        server.add(Project1, Commit1, "Motor", {}, {"package Motor { }"});
        RemoteLibraries remote(cache.path() + "/serverA", server.fetcher());
        remote.ensureClosure(Project0, Commit0);
        QVERIFY(remote.closure(Project0, Commit0).RootKnown);
        remote.setCacheDirectory(cache.path() + "/serverB");
        QVERIFY(!remote.closure(Project0, Commit0).RootKnown);
    }

    void cancelledDownloadFetchesNothing() {
        QTemporaryDir cache;
        FakeServer server;
        server.add(Project0, Commit0, "Model", {}, {});
        RemoteLibraries remote(cache.path(), server.fetcher());
        remote.cancel();
        QCOMPARE(remote.ensureClosure(Project0, Commit0), 0);
        QCOMPARE(server.Fetches, 0);
    }

    void serverChangeDuringDownloadStoresNothingInTheNewCache() {
        QTemporaryDir cache;
        FakeServer server;
        server.add(Project0, Commit0, "Model", {ref(Project1, Commit1, "Motor")}, {});
        server.add(Project1, Commit1, "Motor", {}, {"package Motor { }"});

        // The fetch of the used commit is in progress when the user switches to another server.
        RemoteLibraries *self = nullptr;
        auto fetch = server.fetcher();
        RemoteLibraries remote(cache.path() + "/serverA",
                               [&](const QString &project, const QString &commit, bool sources, const std::atomic<bool> &cancelled) {
                                   const auto content = fetch(project, commit, sources, cancelled);
                                   if (project == Project1)
                                       self->setCacheDirectory(cache.path() + "/serverB");
                                   return content;
                               });
        self = &remote;
        remote.ensureClosure(Project0, Commit0);

        // What came from the first server is neither in its cache (it was dropped) nor in the cache of the second.
        QVERIFY(!remote.cached(Project1, Commit1, true).has_value());
        QVERIFY(!QDir(cache.path() + "/serverB/" + Project1).exists());
        QVERIFY(!QDir(cache.path() + "/serverA/" + Project1).exists());
        QVERIFY(remote.failure(Project1, Commit1).isEmpty());
    }

    void severalBlocksSeeEachOther() {
        DocumentWorkspace workspace(Libraries, QString(), noStandardLibrary());
        const std::vector<DocumentSource> sources{sysml("package One { part def A; }", 0), sysml("package Two { import One::*; part a : A; }", 3)};
        const auto result = workspace.update(sources, "doc");
        QCOMPARE(warningsAndErrors(result), 0);
        QCOMPARE(result.Rows.size(), size_t(2));
        QCOMPARE(result.Rows[1].Row, 3);
        QCOMPARE(Libraries->fileReadCount(), 0);
    }
};

QTEST_GUILESS_MAIN(LibraryWorkspaceTests)

#include "tst_LibraryWorkspace.moc"
