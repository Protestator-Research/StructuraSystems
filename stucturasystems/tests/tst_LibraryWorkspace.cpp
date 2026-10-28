//
// Created by Moritz Herzog on 24.10.26.
//
// Tests of the library resolution: standard library only, import chains over several files, cycles, manual levels,
// ambiguous packages and the incremental behaviour of a document workspace.
//

#include <chrono>
#include <memory>
#include <vector>

#include <sysmlv2/ParserError.h>
#include <sysmlv2/Workspace.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

#include "../src/Models/Libraries/CodeScanner.h"
#include "../src/Models/Libraries/DocumentWorkspace.h"
#include "../src/Models/Libraries/LibraryWorkspace.h"
#include "../src/Models/Libraries/WorkspaceConfig.h"

using namespace StructuraSystems::Client;

namespace {
    DocumentSource sysml(const QString &text, int row = 0) {
        DocumentSource source;
        source.Row = row;
        source.Text = text.toStdString();
        return source;
    }

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
