//
// DocumentWorkspace::warmUp(): the steps and their progress, isWarm(), the cancellation by beginShutdown() and the equivalence with a
// workspace that loads the standard library lazily (the first update()).
//
// beginShutdown() is process-wide: the tests that use it come last (slots are run in the order of their declaration) and undo it with
// resetShutdownForTests() before they end, so that nothing that follows is affected.
//

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <tuple>
#include <vector>

#include <QtTest>

#include "../src/Models/Libraries/DocumentWorkspace.h"
#include "../src/Models/Libraries/LibraryWorkspace.h"

using namespace StructuraSystems::Client;

namespace {
    using Clock = std::chrono::steady_clock;

    double msSince(Clock::time_point start) {
        return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    }

    DocumentSource block(int row, const char *text) {
        DocumentSource source;
        source.Row = row;
        source.Text = text;
        return source;
    }

    std::vector<DocumentSource> blocks() {
        return {block(0, "package DocOne { private import ISQ::*; private import ScalarValues::*;"
                         " port def PowerPort { attribute voltage : ElectricPotentialDifferenceValue; attribute current : ElectricCurrentValue; } }"),
                block(1, "package DocTwo { private import ISQ::*; private import DocOne::*; part def Engine { attribute power : PowerValue;"
                         " port supply : PowerPort; part nothing : DoesNotExist; } }"),
                block(2, "package Broken { part def ; }")};
    }

    using Diagnostic = std::tuple<int, int, int, int, QString>;

    std::vector<Diagnostic> diagnosticsOf(const WorkspaceResult &result) {
        std::vector<Diagnostic> list;
        for (const auto &d: result.Diagnostics)
            list.emplace_back(d.Row, d.Line, d.Column, d.Severity, d.Message);
        return list;
    }

    std::multiset<QString> namesOf(const NameIndex &index) {
        std::multiset<QString> set;
        for (const auto &entry: index.entries())
            set.insert(entry.QualifiedName + "|" + entry.Type + "|" + QString::number(entry.InDocument));
        return set;
    }

    /// What the progress callback saw, in order.
    struct Steps {
        std::mutex Mutex;
        std::vector<WarmUpStep> List;
        std::atomic<int> LoadedFiles{0};
        std::atomic<int> Last{-1};

        WarmUpProgress callback(DocumentWorkspace *workspace = nullptr, std::vector<bool> *warmDuring = nullptr) {
            return [this, workspace, warmDuring](const WarmUpStep &step) {
                std::lock_guard lock(Mutex);
                List.push_back(step);
                Last = step.Step;
                if (step.Step == WarmUpStep::Loading)
                    LoadedFiles = step.Done;
                if (workspace != nullptr && warmDuring != nullptr)
                    warmDuring->push_back(workspace->isWarm());
            };
        }
    };

    void waitUntil(const std::function<bool()> &condition, int timeoutMs = 120000) {
        const auto start = Clock::now();
        while (!condition() && msSince(start) < timeoutMs)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

class WarmUpTests : public QObject {
    Q_OBJECT

private:
    // The warm workspace of stepsAndWarmFlag(), used by the tests that follow.
    std::unique_ptr<DocumentWorkspace> Warm;
    int Total = 0;

private slots:
    void initTestCase() {
        Total = static_cast<int>(LibraryWorkspace::standardLibrary(LibraryWorkspace::standardFormat()).size());
        qInfo() << "standard library:" << Total << "files," << (LibraryWorkspace::standardFormat() == StandardFormat::Xmi ? "XMI" : "text");
        // (the default: four threads read the files, fewer on a machine with fewer cores)
        QCOMPARE(DocumentWorkspace::loadThreads(), (std::min)(4u, (std::max)(1u, std::thread::hardware_concurrency())));
    }

    void stepsAndWarmFlag() {
        Warm = std::make_unique<DocumentWorkspace>();
        QVERIFY(!Warm->isWarm());
        Steps steps;
        std::vector<bool> warmDuring;
        const auto start = Clock::now();
        Warm->warmUp(steps.callback(Warm.get(), &warmDuring));
        qInfo() << "warmUp:" << msSince(start) << "ms";

        QVERIFY(Warm->isWarm());
        // isWarm() is false at every step but the last one: the flag is set before Ready is reported
        QVERIFY(!warmDuring.empty());
        for (size_t i = 0; i + 1 < warmDuring.size(); ++i)
            QVERIFY2(!warmDuring[i], "isWarm() was true before Ready");
        QVERIFY(warmDuring.back());

        // Loading (0, 1, 2 ... Total), Resolving, Indexing, Ready
        QCOMPARE(static_cast<int>(steps.List.size()), Total + 1 + 3);
        for (int i = 0; i <= Total; ++i) {
            QCOMPARE(steps.List[i].Step, WarmUpStep::Loading);
            QCOMPARE(steps.List[i].Done, i);
            QCOMPARE(steps.List[i].Total, Total);
        }
        QCOMPARE(steps.List[Total + 1].Step, WarmUpStep::Resolving);
        QCOMPARE(steps.List[Total + 2].Step, WarmUpStep::Indexing);
        QCOMPARE(steps.List[Total + 3].Step, WarmUpStep::Ready);
        QCOMPARE(Total, 94);

        // Again: nothing is loaded again, the steps of a warm workspace are the three that follow loading
        Steps again;
        const auto startAgain = Clock::now();
        Warm->warmUp(again.callback());
        qInfo() << "warmUp of a warm workspace:" << msSince(startAgain) << "ms";
        QVERIFY(Warm->isWarm());
        QCOMPARE(again.List.back().Step, WarmUpStep::Ready);
    }

    void firstNameIndexAfterWarmUpIsCheap() {
        QVERIFY(Warm != nullptr);
        const auto update = Warm->update(blocks(), "doc");
        Q_UNUSED(update);
        const auto start = Clock::now();
        const auto names = Warm->buildNameIndex();
        const double first = msSince(start);
        QVERIFY(names->count() > 1000);
        qInfo() << "buildNameIndex() after warmUp + update (the library part is built by warmUp):" << first << "ms";
    }

    void updateAfterWarmUpGivesWhatALazyWorkspaceGives() {
        QVERIFY(Warm != nullptr);
        const auto warmResult = Warm->update(blocks(), "doc");
        const auto warmNames = namesOf(*Warm->buildNameIndex());

        DocumentWorkspace lazy;   // no warmUp(): the first update() loads the standard library
        QVERIFY(!lazy.isWarm());
        const auto lazyResult = lazy.update(blocks(), "doc");
        QVERIFY(lazy.isWarm());   // (an update() loads and resolves the standard library, too)
        const auto lazyNames = namesOf(*lazy.buildNameIndex());

        QVERIFY(!warmResult.Diagnostics.isEmpty());   // (the unknown name and the syntax error)
        QCOMPARE(diagnosticsOf(warmResult) == diagnosticsOf(lazyResult), true);
        QCOMPARE(warmNames.size(), lazyNames.size());
        QVERIFY(warmNames == lazyNames);
        QCOMPARE(warmResult.Rows.size(), lazyResult.Rows.size());
    }

    void loadingOnSeveralThreadsGivesTheSameLibrary() {
        QVERIFY(Warm != nullptr);
        const auto expected = namesOf(*Warm->buildNameIndex());
        const auto expectedResult = Warm->update(blocks(), "doc");

        const auto defaultThreads = DocumentWorkspace::loadThreads();
        DocumentWorkspace::setLoadThreads(2);
        DocumentWorkspace parallel;
        Steps steps;
        parallel.warmUp(steps.callback());
        DocumentWorkspace::setLoadThreads(defaultThreads);
        QVERIFY(parallel.isWarm());
        // (the file counter rises by one up to the total, whatever the number of threads)
        int expectedDone = 0;
        for (const auto &step: steps.List) {
            if (step.Step != WarmUpStep::Loading)
                break;
            QCOMPARE(step.Done, expectedDone++);
        }
        QCOMPARE(expectedDone, Total + 1);
        const auto result = parallel.update(blocks(), "doc");
        QVERIFY(diagnosticsOf(result) == diagnosticsOf(expectedResult));
        QVERIFY(namesOf(*parallel.buildNameIndex()) == expected);
    }

    // ---- the cancellation: last, it changes the state of the process until resetShutdownForTests() ----

    void cancelDuringLoading() {
        DocumentWorkspace workspace;
        Steps steps;
        std::atomic<bool> finished{false};
        std::thread worker([&] {
            workspace.warmUp(steps.callback());
            finished = true;
        });
        waitUntil([&] { return steps.LoadedFiles >= 3 || finished; });
        QVERIFY(!finished);
        const auto start = Clock::now();
        DocumentWorkspace::beginShutdown();
        waitUntil([&] { return finished.load(); }, 20000);
        const double stopped = msSince(start);
        worker.join();
        qInfo() << "warmUp cancelled while loading (after" << steps.LoadedFiles << "of" << Total << "files) returned" << stopped << "ms after the flag";
        QVERIFY(finished);
        QVERIFY2(stopped < 1000, "the cancelled warm-up took too long to stop");
        QVERIFY(!workspace.isWarm());
        QVERIFY(steps.Last == WarmUpStep::Loading);   // (no Resolving, no Ready)

        // The workspace is empty: it loads from scratch once the flag is reset
        DocumentWorkspace::resetShutdownForTests();
        Steps again;
        workspace.warmUp(again.callback());
        QVERIFY(workspace.isWarm());
        QCOMPARE(again.List.back().Step, WarmUpStep::Ready);
        QCOMPARE(again.List.front().Done, 0);
        const auto result = workspace.update(blocks(), "doc");
        QVERIFY(!result.Diagnostics.isEmpty());
    }

    void cancelDuringResolving() {
        DocumentWorkspace workspace;
        Steps steps;
        std::atomic<bool> finished{false};
        std::thread worker([&] {
            workspace.warmUp(steps.callback());
            finished = true;
        });
        waitUntil([&] { return steps.Last == WarmUpStep::Resolving || finished; });
        QVERIFY(!finished);
        const auto start = Clock::now();
        DocumentWorkspace::beginShutdown();
        waitUntil([&] { return finished.load(); }, 20000);
        const double stopped = msSince(start);
        worker.join();
        qInfo() << "warmUp cancelled while resolving returned" << stopped << "ms after the flag";
        QVERIFY(finished);
        QVERIFY(!workspace.isWarm());
        QVERIFY2(stopped < 3000, "the cancelled warm-up took too long to stop");
        DocumentWorkspace::resetShutdownForTests();
    }

    void recycleUnderShutdownReturnsAtOnceAndDoesNotDispose() {
        DocumentWorkspace workspace;
        workspace.warmUp();
        workspace.update(blocks(), "doc");
        QVERIFY(workspace.isWarm());
        DocumentWorkspace::beginShutdown();
        const auto start = Clock::now();
        workspace.recycle();   // (the blocks cannot be replaced: the load is cancelled, the model is left alone instead of taken apart)
        const double took = msSince(start);
        qInfo() << "recycle() under shutdown:" << took << "ms";
        DocumentWorkspace::resetShutdownForTests();
        QVERIFY2(took < 100, "recycle() disposed the workspace while the application closes");
        QVERIFY(!workspace.isWarm());   // (a workspace that recycle() gave up is not a warm one)
    }

    void updateUnderShutdownReturnsAtOnceAndDoesNotDispose() {
        DocumentWorkspace workspace;
        workspace.warmUp();
        auto changed = blocks();
        changed[0].Text += " ";
        workspace.update(blocks(), "doc");
        DocumentWorkspace::beginShutdown();
        const auto start = Clock::now();
        try {
            workspace.update(changed, "doc");   // (returns with problems or throws: either way the model stays)
        } catch (const std::exception &) {
        }
        const double took = msSince(start);
        qInfo() << "update() under shutdown:" << took << "ms";
        DocumentWorkspace::resetShutdownForTests();
        QVERIFY2(took < 100, "update() disposed the workspace while the application closes");

        // An update() of a workspace that was never loaded fails at once, and is not warm; the next one works.
        DocumentWorkspace fresh;
        DocumentWorkspace::beginShutdown();
        QVERIFY_EXCEPTION_THROWN(fresh.update(blocks(), "doc"), std::exception);
        QVERIFY(!fresh.isWarm());
        DocumentWorkspace::resetShutdownForTests();
        const auto result = fresh.update(blocks(), "doc");
        QVERIFY(!result.Diagnostics.isEmpty());
        QVERIFY(fresh.isWarm());
    }

    void cleanupTestCase() {
        DocumentWorkspace::resetShutdownForTests();
        Warm.reset();
    }
};

QTEST_GUILESS_MAIN(WarmUpTests)

#include "tst_WarmUp.moc"
