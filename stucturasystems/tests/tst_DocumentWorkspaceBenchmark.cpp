//
// Benchmark of DocumentWorkspace (not a test of behaviour, not registered with ctest). Run the executable by hand; the results are
// printed with qInfo. A Debug build only gives relative figures.
//
// Measured: warmUp() (parses and resolves the whole standard library), the first update() of a representative document, update() after
// changing the text of one block, update() without a change and buildNameIndex().
//
// Also measured: the steps of warmUp() (loading, resolving, indexing the names), warmUp() with several loading threads, and the cost of the
// first text parse of the process (coldVsWarmParse: the cache of the generated ANTLR parsers is process-wide). Run that slot alone, in a
// fresh process, for the cold figure: `DocumentWorkspaceBenchmark coldVsWarmParse` (DOCUMENT_BENCHMARK_FORMAT=xmi|text picks the format; with
// the text format the library itself is parsed by warmUp() and warms the cache, with XMI the first parse of the process is a document).
//

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <future>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include <sysmlv2/Workspace.h>
#include <QtTest>

#include "../src/Models/Libraries/DocumentWorkspace.h"

using namespace StructuraSystems::Client;

namespace {
    using Clock = std::chrono::steady_clock;

    template <class F>
    double timed(F &&function) {
        const auto start = Clock::now();
        function();
        return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    }

    double median(std::vector<double> values) {
        if (values.empty())
            return 0;
        std::sort(values.begin(), values.end());
        const size_t middle = values.size() / 2;
        return values.size() % 2 == 1 ? values[middle] : (values[middle - 1] + values[middle]) / 2;
    }

    void report(const QString &name, const std::vector<double> &runs) {
        QString all;
        for (const double value: runs)
            all += QString::number(value, 'f', 0) + " ";
        qInfo().noquote() << QString("%1 median %2 ms   (%3)").arg(name, -50).arg(median(runs), 9, 'f', 1).arg(all.trimmed());
    }

    DocumentSource block(int row, const std::string &text) {
        DocumentSource source;
        source.Row = row;
        source.Text = text;
        return source;
    }

    /// A thread that stays: runs what it is given one after the other (a thread that has parsed text before).
    class Worker {
    public:
        Worker() : Thread([this] { loop(); }) {}
        ~Worker() {
            {
                std::lock_guard lock(Mutex);
                Stop = true;
            }
            Condition.notify_all();
            Thread.join();
        }
        void run(const std::function<void()> &task) {
            std::promise<void> done;
            {
                std::lock_guard lock(Mutex);
                Tasks.push([&] { task(); done.set_value(); });
            }
            Condition.notify_all();
            done.get_future().get();
        }

    private:
        void loop() {
            for (;;) {
                std::function<void()> task;
                {
                    std::unique_lock lock(Mutex);
                    Condition.wait(lock, [this] { return Stop || !Tasks.empty(); });
                    if (Tasks.empty())
                        return;
                    task = std::move(Tasks.front());
                    Tasks.pop();
                }
                task();
            }
        }
        std::mutex Mutex;
        std::condition_variable Condition;
        std::queue<std::function<void()>> Tasks;
        bool Stop = false;
        std::thread Thread;
    };

    /// Runs @p task on a new thread and waits for it.
    void onNewThread(const std::function<void()> &task) {
        std::thread thread(task);
        thread.join();
    }

    /// Five blocks that refer to each other; `variant` slightly changes the text of block 2.
    std::vector<DocumentSource> blocks(int variant) {
        const std::string extra = variant == 0 ? "" : " attribute extra" + std::to_string(variant) + " : Real;";
        return {
            block(0, "package DocOne { private import ISQ::*; private import ScalarValues::*;"
                     " port def PowerPort { attribute voltage : ElectricPotentialDifferenceValue; attribute current : ElectricCurrentValue; } }"),
            block(1, "package DocTwo { private import ISQ::*; private import ScalarValues::*; private import DocOne::*;"
                     " part def Engine { attribute power : PowerValue; port supply : PowerPort; } }"),
            block(2, "package DocThree { private import ISQ::*; private import SI::*; private import ScalarValues::*; private import DocOne::*;"
                     " private import DocTwo::*; part def Vehicle { attribute mass : MassValue; attribute name : String; part engine : Engine;"
                     " port power : PowerPort;" + extra + " } }"),
            block(3, "package DocFour { private import ISQ::*; private import DocThree::*; private import DocTwo::*;"
                     " part fleet : Vehicle[3] { attribute topSpeed : SpeedValue; } part spare : Engine; attribute total : LengthValue; }"),
            block(4, "package DocFive { private import ScalarValues::*; private import DocFour::*; attribute count : Integer; part second : fleet; }")};
    }
}

class DocumentWorkspaceBenchmark : public QObject {
    Q_OBJECT

private slots:
    // The SysMD pattern on a SysMLv2::Files::Workspace directly (no DocumentWorkspace): where does the time of update() go?
    void initTestCase() {
        // DOCUMENT_BENCHMARK_FORMAT=xmi measures the standard library in the XMI format (default: the format of the editor)
        if (qgetenv("DOCUMENT_BENCHMARK_FORMAT").toLower() == "xmi")
            LibraryWorkspace::setStandardFormat(StandardFormat::Xmi);
        else if (qgetenv("DOCUMENT_BENCHMARK_FORMAT").toLower() == "text")
            LibraryWorkspace::setStandardFormat(StandardFormat::Text);
        qInfo() << "standard library format:" << (LibraryWorkspace::standardFormat() == StandardFormat::Xmi ? "XMI" : "text");
    }

    void directWorkspace() {
        using SysMLv2::Files::SourceLanguage;
        constexpr int Runs = 3;
        std::vector<double> lib, libRes, add, addRes, idle, rep, repRes, elements, errors, warn, unres, unresOnce;
        const auto &library = LibraryWorkspace::standardLibrary();
        for (int run = 0; run < Runs; ++run) {
            SysMLv2::Files::Workspace space;
            lib.push_back(timed([&] {
                for (const auto &x: library)
                    space.addText(x.Text, x.Name, x.Xmi ? SourceLanguage::Xmi : (x.KerML ? SourceLanguage::KerML : SourceLanguage::SysML), LibraryWorkspace::StandardLevel);
            }));
            libRes.push_back(timed([&] { space.resolve(); }));
            const auto docs = blocks(0);
            std::vector<size_t> ids;
            add.push_back(timed([&] {
                for (const auto &d: docs)
                    ids.push_back(space.addText(d.Text, "doc#" + std::to_string(d.Row), SourceLanguage::SysML, LibraryWorkspace::DocumentLevel));
            }));
            addRes.push_back(timed([&] { space.resolve(); }));
            idle.push_back(timed([&] { space.resolve(); }));
            const auto changedDocs = blocks(run + 1);
            rep.push_back(timed([&] {
                for (size_t i = 0; i < ids.size(); ++i)
                    space.replaceSource(ids[i], changedDocs[i].Text, SourceLanguage::SysML);
            }));
            repRes.push_back(timed([&] { space.resolve(); }));
            elements.push_back(timed([&] { for (const auto s: ids) { auto e = space.elements(s); auto r = space.rootNamespace(s); } }));
            errors.push_back(timed([&] { for (const auto s: ids) { auto e = space.errors(s); } }));
            warn.push_back(timed([&] { for (const auto s: ids) { auto e = space.unresolvedAsWarnings(s); } }));
            unres.push_back(timed([&] { for (size_t i = 0; i < ids.size(); ++i) { auto e = space.unresolvedReferences(); } }));
            unresOnce.push_back(timed([&] { auto e = space.unresolvedReferences(); }));
        }
        report("direct: addText standard library", lib);
        report("direct: resolve standard library", libRes);
        report("direct: addText 5 blocks", add);
        report("direct: resolve after blocks (first)", addRes);
        report("direct: resolve without change", idle);
        report("direct: replaceSource 5 blocks", rep);
        report("direct: resolve after replace", repRes);
        report("direct: elements+rootNamespace x5", elements);
        report("direct: errors(slot) x5", errors);
        report("direct: unresolvedAsWarnings(slot) x5", warn);
        report("direct: unresolvedReferences() x5", unres);
        report("direct: unresolvedReferences() x1", unresOnce);
    }

    // The steps of warmUp(): where do the seconds go?
    void warmUpSteps() {
        constexpr int Runs = 3;
        std::vector<double> total, load, resolve, indexing, firstIndex, firstUpdate;
        for (int run = 0; run < Runs; ++run) {
            DocumentWorkspace workspace;
            Clock::time_point start = Clock::now(), resolveStart, indexStart, readyAt;
            workspace.warmUp([&](const WarmUpStep &step) {
                const auto now = Clock::now();
                if (step.Step == WarmUpStep::Resolving)
                    resolveStart = now;
                else if (step.Step == WarmUpStep::Indexing)
                    indexStart = now;
                else if (step.Step == WarmUpStep::Ready)
                    readyAt = now;
            });
            QVERIFY(workspace.isWarm());
            const auto ms = [](Clock::time_point from, Clock::time_point to) { return std::chrono::duration<double, std::milli>(to - from).count(); };
            total.push_back(ms(start, readyAt));
            load.push_back(ms(start, resolveStart));
            resolve.push_back(ms(resolveStart, indexStart));
            indexing.push_back(ms(indexStart, readyAt));
            firstUpdate.push_back(timed([&] { workspace.update(blocks(0), "doc"); }));
            firstIndex.push_back(timed([&] { workspace.buildNameIndex(); }));
        }
        report("warmUp steps: total", total);
        report("warmUp steps: Loading (addTexts, 1 thread)", load);
        report("warmUp steps: Resolving", resolve);
        report("warmUp steps: Indexing (library names)", indexing);
        report("first update() after warmUp", firstUpdate);
        report("first buildNameIndex() after warmUp + update", firstIndex);
    }

    // warmUp() with several loading threads (one run each; a debug build locks the heap of the runtime, so expect no gain there).
    void warmUpThreads() {
        qInfo() << "hardware threads:" << std::thread::hardware_concurrency();
        const auto defaultThreads = DocumentWorkspace::loadThreads();
        for (const unsigned threads: {1u, 2u, 4u, 8u}) {
            DocumentWorkspace::setLoadThreads(threads);
            DocumentWorkspace workspace;
            Clock::time_point loaded;
            const double all = timed([&] { workspace.warmUp([&](const WarmUpStep &step) { if (step.Step == WarmUpStep::Resolving) loaded = Clock::now(); }); });
            qInfo().noquote() << QString("warmUp with %1 loading thread(s): %2 ms, of which loading %3 ms")
                                     .arg(threads).arg(all, 0, 'f', 0)
                                     .arg(all - std::chrono::duration<double, std::milli>(Clock::now() - loaded).count(), 0, 'f', 0);
        }
        DocumentWorkspace::setLoadThreads(defaultThreads);
    }

    // The first text parse of the process against later parses on new threads and on a thread that has parsed before.
    // The generated parsers keep their prediction cache in process-wide statics, so only the very first parse pays for it.
    void coldVsWarmParse() {
        DocumentWorkspace workspace;
        workspace.warmUp();
        qInfo() << "(warm-up done; the standard library is read as" << (LibraryWorkspace::standardFormat() == StandardFormat::Xmi ? "XMI" : "text") << ")";
        Worker resident;
        double first = 0, newThread1 = 0, newThread2 = 0, resident1 = 0, resident2 = 0;
        resident.run([&] { first = timed([&] { workspace.update(blocks(1), "doc"); }); });             // the first parse of the process (cold if XMI)
        onNewThread([&] { newThread1 = timed([&] { workspace.update(blocks(2), "doc"); }); });         // a new thread, the cache is warm
        resident.run([&] { resident1 = timed([&] { workspace.update(blocks(3), "doc"); }); });         // the thread that parsed before
        onNewThread([&] { newThread2 = timed([&] { workspace.update(blocks(4), "doc"); }); });
        resident.run([&] { resident2 = timed([&] { workspace.update(blocks(5), "doc"); }); });
        qInfo().noquote() << QString("update() #1 on a fresh thread, first parse in the process (cold if XMI): %1 ms").arg(first, 0, 'f', 1);
        qInfo().noquote() << QString("update() #2 on another new thread:                                       %1 ms").arg(newThread1, 0, 'f', 1);
        qInfo().noquote() << QString("update() #3 on the thread of #1 (it parsed before):                      %1 ms").arg(resident1, 0, 'f', 1);
        qInfo().noquote() << QString("update() #4 on a new thread:                                             %1 ms").arg(newThread2, 0, 'f', 1);
        qInfo().noquote() << QString("update() #5 on the thread of #1:                                         %1 ms").arg(resident2, 0, 'f', 1);
    }

    void benchmark() {
        constexpr int Runs = 5;
        // warmUp() parses the whole standard library: measured once per run, so every run uses a fresh workspace.
        std::vector<double> warm, first, changed, unchanged, index, indexAgain, indexFlat;
        for (int run = 0; run < Runs; ++run) {
            DocumentWorkspace workspace;
            warm.push_back(timed([&] { workspace.warmUp(); }));
            WorkspaceResult result;
            first.push_back(timed([&] { result = workspace.update(blocks(0), "doc"); }));
            const int firstDiagnostics = result.Diagnostics.size();
            changed.push_back(timed([&] { result = workspace.update(blocks(run + 1), "doc"); }));
            const int changedDiagnostics = result.Diagnostics.size();
            unchanged.push_back(timed([&] { result = workspace.update(blocks(run + 1), "doc"); }));
            index.push_back(timed([&] {
                const auto names = workspace.buildNameIndex();
                QVERIFY(names != nullptr);
            }));
            // The names again, as with every update of the editor: the library part is built already (once per epoch).
            std::shared_ptr<const NameIndex> names;
            indexAgain.push_back(timed([&] { names = workspace.buildNameIndex(); }));
            // For comparison: a flat index over the same entries (what every call cost before the index had two stages).
            QList<NameEntry> all;
            for (const auto &entry: names->entries())
                all.append(entry);
            indexFlat.push_back(timed([&] { const NameIndex flat(all); QVERIFY(flat.count() == names->count()); }));
            if (run == 0)
                qInfo() << "diagnostics: first" << firstDiagnostics << "changed" << changedDiagnostics << "unchanged" << result.Diagnostics.size();
        }
        report("warmUp()", warm);
        report("update(): first (5 blocks)", first);
        report("update(): one block changed", changed);
        report("update(): without change", unchanged);
        report("buildNameIndex(): first call (builds the library part)", index);
        report("buildNameIndex(): again (library part kept)", indexAgain);
        report("flat NameIndex over the same entries (the cost before)", indexFlat);
    }
};

QTEST_GUILESS_MAIN(DocumentWorkspaceBenchmark)

#include "tst_DocumentWorkspaceBenchmark.moc"
