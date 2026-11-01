//
// The state of the warm-up of the standard library, for the status bar.
//

#include "LibraryWarmUpStatus.h"

#include <algorithm>

#include <QMetaObject>

namespace StructuraSystems::Client {
    LibraryWarmUpStatus::LibraryWarmUpStatus(QObject *parent) : QObject(parent) {
    }

    double LibraryWarmUpStatus::progressOf(const WarmUpStep &step) {
        switch (step.Step) {
            case WarmUpStep::Loading:
                return step.Total > 0 ? 0.85 * std::clamp(static_cast<double>(step.Done) / step.Total, 0.0, 1.0) : 0.0;
            case WarmUpStep::Resolving:
                return 0.9;
            case WarmUpStep::Indexing:
                return 0.97;
            case WarmUpStep::Ready:
                return 1.0;
        }
        return 0.0;
    }

    QString LibraryWarmUpStatus::textOf(const WarmUpStep &step) {
        switch (step.Step) {
            case WarmUpStep::Loading:
                return tr("Loading the standard library... %1/%2").arg(step.Done).arg(step.Total);
            case WarmUpStep::Resolving:
                return tr("Resolving the names of the standard library...");
            case WarmUpStep::Indexing:
                return tr("Building the name index...");
            case WarmUpStep::Ready:
                break;
        }
        return {};
    }

    void LibraryWarmUpStatus::set(bool loading, double progress, const QString &status) {
        if (Loading == loading && Progress == progress && Status == status)
            return;
        Loading = loading;
        Progress = progress;
        Status = status;
        emit changed();
    }

    void LibraryWarmUpStatus::apply(const WarmUpStep &step) {
        if (step.Step == WarmUpStep::Ready) {
            set(false, 0, QString());
            return;
        }
        // (the bar only moves forward within one warm-up; a new one starts from 0 after the state was left)
        const double progress = Loading ? std::max(Progress, progressOf(step)) : progressOf(step);
        set(true, progress, textOf(step));
    }

    void LibraryWarmUpStatus::fail() {
        set(false, 0, QString());
    }

    void LibraryWarmUpStatus::post(const WarmUpStep &step) {
        QMetaObject::invokeMethod(this, [this, step]() { apply(step); }, Qt::QueuedConnection);
    }

    void LibraryWarmUpStatus::postFailure() {
        QMetaObject::invokeMethod(this, [this]() { fail(); }, Qt::QueuedConnection);
    }
}
