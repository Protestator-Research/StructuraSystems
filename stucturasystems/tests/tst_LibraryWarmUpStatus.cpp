//
// LibraryWarmUpStatus: what the status bar shows while the standard library loads (texts, progress, loading flag) and the hand-over of the
// steps from the thread of the warm-up to the GUI thread.
//

#include <memory>
#include <thread>
#include <vector>

#include <QSignalSpy>
#include <QtTest>

#include "../src/ViewModels/LibraryWarmUpStatus.h"

using namespace StructuraSystems::Client;

namespace {
    WarmUpStep step(WarmUpStep::Kind kind, int done = 0, int total = 0) {
        WarmUpStep result;
        result.Step = kind;
        result.Done = done;
        result.Total = total;
        return result;
    }
}

class LibraryWarmUpStatusTests : public QObject {
    Q_OBJECT

private slots:
    void idleAtTheStart() {
        LibraryWarmUpStatus status;
        QVERIFY(!status.loading());
        QCOMPARE(status.progress(), 0.0);
        QVERIFY(status.status().isEmpty());
    }

    void theStepsInOrder() {
        LibraryWarmUpStatus status;
        QSignalSpy changed(&status, &LibraryWarmUpStatus::changed);
        double last = -1;
        const auto check = [&](const WarmUpStep &s, const QString &text) {
            status.apply(s);
            QVERIFY(status.loading());
            QCOMPARE(status.status(), text);
            QVERIFY2(status.progress() >= last, "the progress went back");
            QVERIFY(status.progress() <= 1.0);
            last = status.progress();
        };
        check(step(WarmUpStep::Loading, 0, 94), "Loading the standard library... 0/94");
        QCOMPARE(status.progress(), 0.0);
        for (int done = 1; done <= 94; ++done) {
            check(step(WarmUpStep::Loading, done, 94), QString("Loading the standard library... %1/94").arg(done));
            QVERIFY(status.progress() <= 0.85 + 1e-9);
        }
        QCOMPARE(status.progress(), 0.85);
        check(step(WarmUpStep::Resolving), "Resolving the names of the standard library...");
        QCOMPARE(status.progress(), 0.9);
        check(step(WarmUpStep::Indexing), "Building the name index...");
        QCOMPARE(status.progress(), 0.97);

        status.apply(step(WarmUpStep::Ready));
        QVERIFY(!status.loading());
        QVERIFY(status.status().isEmpty());
        QCOMPARE(status.progress(), 0.0);
        QVERIFY(changed.count() > 90);
    }

    void aStepThatRepeatsChangesNothing() {
        LibraryWarmUpStatus status;
        status.apply(step(WarmUpStep::Resolving));
        QSignalSpy changed(&status, &LibraryWarmUpStatus::changed);
        status.apply(step(WarmUpStep::Resolving));
        QCOMPARE(changed.count(), 0);
    }

    void theBarNeverGoesBackWithinOneWarmUp() {
        LibraryWarmUpStatus status;
        status.apply(step(WarmUpStep::Indexing));
        status.apply(step(WarmUpStep::Loading, 10, 94));   // (out of order: the bar stays)
        QCOMPARE(status.progress(), 0.97);
    }

    void failEndsTheLoadingState() {
        LibraryWarmUpStatus status;
        status.apply(step(WarmUpStep::Loading, 40, 94));
        QVERIFY(status.loading());
        QSignalSpy changed(&status, &LibraryWarmUpStatus::changed);
        status.fail();
        QVERIFY(!status.loading());
        QVERIFY(status.status().isEmpty());
        QCOMPARE(status.progress(), 0.0);
        QCOMPARE(changed.count(), 1);
        status.fail();   // (again: nothing changes)
        QCOMPARE(changed.count(), 1);

        // the next warm-up starts from 0 again
        status.apply(step(WarmUpStep::Loading, 1, 94));
        QVERIFY(status.loading());
        QVERIFY(status.progress() < 0.02);
    }

    void stepsPostedFromAnotherThreadArriveOnTheGuiThreadInOrder() {
        LibraryWarmUpStatus status;
        std::vector<int> seen;
        std::vector<bool> onGuiThread;
        const auto *gui = QThread::currentThread();
        connect(&status, &LibraryWarmUpStatus::changed, this, [&]() {
            onGuiThread.push_back(QThread::currentThread() == gui);
            seen.push_back(status.loading() ? static_cast<int>(status.progress() * 1000) : -1);
        });
        std::thread worker([&] {
            status.post(step(WarmUpStep::Loading, 0, 94));
            for (int done = 1; done <= 94; ++done)
                status.post(step(WarmUpStep::Loading, done, 94));
            status.post(step(WarmUpStep::Resolving));
            status.post(step(WarmUpStep::Indexing));
            status.post(step(WarmUpStep::Ready));
        });
        worker.join();
        // (nothing has been applied yet: the events wait for the event loop of this thread)
        QVERIFY(!status.loading());
        QVERIFY(seen.empty());
        QTRY_VERIFY(!seen.empty() && seen.back() == -1);
        QVERIFY(!status.loading());
        for (const bool gui : onGuiThread)
            QVERIFY(gui);
        QVERIFY(seen.size() > 90);
        int previous = -1;
        for (size_t i = 0; i + 1 < seen.size(); ++i) {
            QVERIFY(seen[i] >= previous);
            previous = seen[i];
        }
    }

    void aFailurePostedFromAnotherThreadEndsTheLoadingState() {
        LibraryWarmUpStatus status;
        std::thread worker([&] {
            status.post(step(WarmUpStep::Loading, 3, 94));
            status.postFailure();
        });
        worker.join();
        QTRY_VERIFY(status.loading() == false && status.progress() == 0.0);
        QCOMPARE(status.status(), QString());
    }

    void eventsForADeletedObjectAreDropped() {
        // The application quits while a warm-up reports: the queued steps die with the object.
        auto *status = new LibraryWarmUpStatus;
        std::thread worker([&] {
            for (int done = 0; done <= 94; ++done)
                status->post(step(WarmUpStep::Loading, done, 94));
        });
        worker.join();
        delete status;
        QCoreApplication::processEvents();   // (must not crash)
        QVERIFY(true);
    }
};

QTEST_GUILESS_MAIN(LibraryWarmUpStatusTests)

#include "tst_LibraryWarmUpStatus.moc"
