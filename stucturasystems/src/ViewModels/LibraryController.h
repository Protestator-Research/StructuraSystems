//
// Created by Moritz Herzog on 24.10.26.
//

#ifndef STRUCTURASYSTEMS_LIBRARYCONTROLLER_H
#define STRUCTURASYSTEMS_LIBRARYCONTROLLER_H

#include <memory>
#include <QAbstractListModel>
#include <QFileSystemWatcher>
#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include "../Models/Libraries/LibraryWorkspace.h"

namespace StructuraSystems::Client {
    /**
     * The local libraries of the opened folder for the settings page: one row per file that is a library (it declares a
     * `library package` or the user set its level).
     */
    class LibraryListModel : public QAbstractListModel {
        Q_OBJECT
        QML_ELEMENT
        QML_UNCREATABLE("LibraryListModel instances are owned by LibraryController.")
        Q_PROPERTY(int count READ count NOTIFY countChanged)
    public:
        enum Roles {
            FileRole = Qt::UserRole + 1,
            LevelRole,
            OverriddenRole,
            DeclaredRole,
            InCycleRole,
            PackagesRole
        };
        Q_ENUM(Roles)

        explicit LibraryListModel(QObject *parent = nullptr);

        int rowCount(const QModelIndex &parent = QModelIndex()) const override;
        QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
        QHash<int, QByteArray> roleNames() const override;

        [[nodiscard]] int count() const;
        void setEntries(QList<LibraryEntry> entries);

    signals:
        void countChanged();

    private:
        QList<LibraryEntry> Entries;
    };

    /**
     * QML facing side of the LibraryWorkspace: the library levels of the opened folder. Level 0 is the standard library,
     * 1..n are the local libraries (set automatically from the imports, or manually by the user) and the open document is
     * always on top. The manual settings are stored in `.structurasystems/workspace.json` of the opened folder.
     *
     * Nothing of the folder is read when it is opened. The package index is built when a document needs it, or when the
     * settings page asks for the list of the libraries (refresh()).
     */
    class LibraryController : public QObject {
        Q_OBJECT
        QML_ELEMENT
        QML_UNCREATABLE("LibraryController is owned by AppController.")
        Q_PROPERTY(StructuraSystems::Client::LibraryListModel *entries READ entries CONSTANT)
        /** True while the files of the folder are scanned in the background. */
        Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
        /** True if a folder is open, only then levels can be stored. */
        Q_PROPERTY(bool hasFolder READ hasFolder NOTIFY changed)
    public:
        explicit LibraryController(QObject *parent = nullptr);
        ~LibraryController() override;

        [[nodiscard]] LibraryListModel *entries() const;
        [[nodiscard]] bool scanning() const;
        [[nodiscard]] bool hasFolder() const;
        /** The index and levels, shared with the documents. */
        [[nodiscard]] std::shared_ptr<LibraryWorkspace> workspace() const;

        /** Sets the opened folder. Reads its workspace file, scans nothing. */
        void setFolder(const QString &folder);
        /** Called after a parser run: the index may have been built meanwhile, then the levels and the file watching are brought up to date. */
        void indexUsed();

        /** @return the level of a file as a library, -1 if the file is no library or the index was not built yet. */
        [[nodiscard]] Q_INVOKABLE int levelOf(const QString &path) const;
        /** @return true if the user set the level of the file. */
        [[nodiscard]] Q_INVOKABLE bool hasOverride(const QString &path) const;
        /** @return true if the file declares a `library package` (it is a library without a manual setting). */
        [[nodiscard]] Q_INVOKABLE bool isDeclaredLibrary(const QString &path) const;
        /** Makes the file a library on the level it has now (automatic from its imports). */
        Q_INVOKABLE void useAsLibrary(const QString &path);
        /** Removes the manual level; the file is a library only if it declares a `library package`. */
        Q_INVOKABLE void clearLibrary(const QString &path);
        Q_INVOKABLE void setLevel(const QString &path, int level);
        /**
         * Like setLevel, but stored when the value was not changed for a moment (a SpinBox changes in steps while it is edited).
         * The pending change lives here, so it survives a reset of the list that shows it.
         */
        Q_INVOKABLE void setLevelLater(const QString &path, int level);
        /**
         * The packages a file declares on its top level, as far as the index knows them. If the index was not built yet it is built
         * in the background and the changed() signal follows; nothing is read on the GUI thread.
         */
        [[nodiscard]] Q_INVOKABLE QStringList packagesOf(const QString &path) const;
        [[nodiscard]] Q_INVOKABLE QString preferredSource(const QString &package) const;
        /** Uses the file whenever the package is needed and several files define it. An empty path removes the preference. */
        Q_INVOKABLE void setPreferredSource(const QString &package, const QString &path);
        /** Scans the folder (in the background) and updates the list of the libraries. */
        Q_INVOKABLE void refresh();

    signals:
        void changed();
        void scanningChanged();
        /** @param level 0 = info, 1 = success, 2 = warning, 3 = error */
        void notify(int level, const QString &message, const QString &details);

    private:
        void setScanning(bool scanning);
        void applyEntries(const QList<LibraryEntry> &entries);
        void syncWatcher();
        void onFilesChanged();
        void saved();

        std::shared_ptr<LibraryWorkspace> Workspace;
        LibraryListModel *Entries;
        QString Folder;
        bool Scanning = false;
        bool RefreshPending = false;
        QHash<QString, int> Levels;
        QSet<QString> Declared;
        quint64 IndexedGeneration = 0;
        bool IndexSeen = false;
        QFileSystemWatcher *Watcher;
        QTimer *WatcherTimer;
        QTimer *LevelTimer;
        QHash<QString, int> PendingLevels;
    };
}

#endif //STRUCTURASYSTEMS_LIBRARYCONTROLLER_H
