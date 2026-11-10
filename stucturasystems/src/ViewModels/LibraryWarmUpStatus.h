//
// The state of the warm-up of the standard library, for the status bar.
//

#ifndef STRUCTURASYSTEMS_LIBRARYWARMUPSTATUS_H
#define STRUCTURASYSTEMS_LIBRARYWARMUPSTATUS_H

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include "../Models/Libraries/DocumentWorkspace.h"

namespace StructuraSystems::Client {
    /**
     * What the status bar shows while the standard library is loaded (DocumentWorkspace::warmUp()): whether it is loading, how far (a
     * determinate progress 0..1) and a text for the current step. Lives on the GUI thread: apply() and fail() are called there. The warm-up
     * runs on another thread; post() hands a step over (thread safe).
     */
    class LibraryWarmUpStatus : public QObject {
        Q_OBJECT
        QML_ELEMENT
        QML_UNCREATABLE("LibraryWarmUpStatus is owned by AppController.")
        Q_PROPERTY(bool loading READ loading NOTIFY changed)
        Q_PROPERTY(double progress READ progress NOTIFY changed)
        Q_PROPERTY(QString status READ status NOTIFY changed)
    public:
        explicit LibraryWarmUpStatus(QObject *parent = nullptr);

        [[nodiscard]] bool loading() const { return Loading; }
        /** 0..1, rises while loading; 0 when not loading. */
        [[nodiscard]] double progress() const { return Progress; }
        /** The text of the current step, empty when not loading. */
        [[nodiscard]] QString status() const { return Status; }

        /** The progress that a step stands for: loading the files covers 0 to 0.85, resolving is 0.9, indexing 0.97, ready 1. */
        static double progressOf(const WarmUpStep &step);
        /** The text of a step (empty for Ready). */
        static QString textOf(const WarmUpStep &step);

        /** Shows a step. Ready ends the loading state. GUI thread. */
        void apply(const WarmUpStep &step);
        /** The warm-up failed or was cancelled: nothing is shown any more. GUI thread. */
        void fail();

        /** Hands a step over from any thread: it is applied on the thread of this object (queued, in order). */
        void post(const WarmUpStep &step);
        /** Hands over a failure from any thread, see fail(). */
        void postFailure();

    signals:
        void changed();

    private:
        void set(bool loading, double progress, const QString &status);

        bool Loading = false;
        double Progress = 0;
        QString Status;
    };
}

#endif //STRUCTURASYSTEMS_LIBRARYWARMUPSTATUS_H
