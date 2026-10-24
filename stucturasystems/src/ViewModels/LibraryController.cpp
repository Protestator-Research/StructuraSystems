//
// Created by Moritz Herzog on 24.10.26.
//

#include "LibraryController.h"

#include <algorithm>
#include <utility>
#include <QPair>

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QtConcurrent/QtConcurrent>

#include "../Models/Libraries/CodeScanner.h"

namespace StructuraSystems::Client {
    namespace {
        /** The watcher keeps one handle per path (inotify on Linux has a limit), bigger folders are checked when a document is parsed. */
        constexpr int MAX_WATCHED_PATHS = 1500;
    }

    // ------------------------------------------------------------------ LibraryListModel

    LibraryListModel::LibraryListModel(QObject *parent) : QAbstractListModel(parent) {
    }

    int LibraryListModel::rowCount(const QModelIndex &parent) const {
        return parent.isValid() ? 0 : int(Entries.size());
    }

    QVariant LibraryListModel::data(const QModelIndex &index, int role) const {
        if (!index.isValid() || index.row() < 0 || index.row() >= Entries.size())
            return {};
        const auto &entry = Entries[index.row()];
        switch (role) {
            case Qt::DisplayRole:
            case FileRole: return entry.File;
            case LevelRole: return entry.Info.Level;
            case OverriddenRole: return entry.Info.Overridden;
            case DeclaredRole: return entry.Info.DeclaresLibrary;
            case InCycleRole: return entry.Info.InCycle;
            case PackagesRole: return entry.Packages.join(QStringLiteral(", "));
            default: return {};
        }
    }

    QHash<int, QByteArray> LibraryListModel::roleNames() const {
        return {
            {Qt::DisplayRole, "display"},
            {FileRole, "file"},
            {LevelRole, "level"},
            {OverriddenRole, "overridden"},
            {DeclaredRole, "declared"},
            {InCycleRole, "inCycle"},
            {PackagesRole, "packages"}
        };
    }

    int LibraryListModel::count() const { return int(Entries.size()); }

    void LibraryListModel::setEntries(QList<LibraryEntry> entries) {
        const auto before = Entries.size();
        beginResetModel();
        Entries = std::move(entries);
        endResetModel();
        if (before != Entries.size())
            emit countChanged();
    }

    // ------------------------------------------------------------------ LibraryController

    LibraryController::LibraryController(QObject *parent) :
        QObject(parent),
        Workspace(std::make_shared<LibraryWorkspace>()),
        Entries(new LibraryListModel(this)),
        Watcher(new QFileSystemWatcher(this)),
        WatcherTimer(new QTimer(this)),
        LevelTimer(new QTimer(this)) {
        // Many events arrive at once (a save, a git checkout), they are handled together.
        WatcherTimer->setSingleShot(true);
        WatcherTimer->setInterval(400);
        LevelTimer->setSingleShot(true);
        LevelTimer->setInterval(600);
        connect(LevelTimer, &QTimer::timeout, this, [this]() {
            const auto pending = std::exchange(PendingLevels, {});
            for (auto it = pending.begin(); it != pending.end(); ++it)
                Workspace->setLevelOverride(it.key(), it.value());
            if (!pending.isEmpty())
                saved();
        });
        connect(WatcherTimer, &QTimer::timeout, this, &LibraryController::onFilesChanged);
        connect(Watcher, &QFileSystemWatcher::directoryChanged, WatcherTimer, qOverload<>(&QTimer::start));
        connect(Watcher, &QFileSystemWatcher::fileChanged, WatcherTimer, qOverload<>(&QTimer::start));
    }

    LibraryController::~LibraryController() = default;

    LibraryListModel *LibraryController::entries() const { return Entries; }
    bool LibraryController::scanning() const { return Scanning; }
    bool LibraryController::hasFolder() const { return !Folder.isEmpty(); }
    std::shared_ptr<LibraryWorkspace> LibraryController::workspace() const { return Workspace; }

    void LibraryController::setScanning(bool scanning) {
        if (Scanning == scanning)
            return;
        Scanning = scanning;
        emit scanningChanged();
    }

    void LibraryController::setFolder(const QString &folder) {
        Folder = folder;
        Workspace->setRoot(folder);
        RefreshPending = false;
        PendingLevels.clear();
        Levels.clear();
        Declared.clear();
        IndexSeen = false;
        Entries->setEntries({});
        const auto watched = Watcher->files() + Watcher->directories();
        if (!watched.isEmpty())
            Watcher->removePaths(watched);
        emit changed();
    }

    void LibraryController::indexUsed() {
        if (!Workspace->indexBuilt())
            return;
        // The first parser run that needed the index: the levels of the libraries are known now.
        if (!IndexSeen || IndexedGeneration != Workspace->generation()) {
            IndexSeen = true;
            IndexedGeneration = Workspace->generation();
            refresh();
        } else {
            syncWatcher();
        }
    }

    int LibraryController::levelOf(const QString &path) const {
        return Levels.value(path, -1);
    }

    bool LibraryController::isDeclaredLibrary(const QString &path) const {
        return Declared.contains(path);
    }

    bool LibraryController::hasOverride(const QString &path) const {
        return Workspace->levelOverride(path).has_value();
    }

    void LibraryController::saved() {
        if (!Workspace->lastSaveSucceeded())
            emit notify(3, tr("Could not save the library settings."), QDir(Folder).filePath(WorkspaceConfig::relativeFilePath()));
        refresh();
    }

    void LibraryController::useAsLibrary(const QString &path) {
        // The level the file has now needs the index (the file may import other libraries), which can take a moment.
        const auto workspace = Workspace;
        QtConcurrent::run([workspace, path]() {
            workspace->setLevelOverride(path, workspace->levelInfo(path).Level);
        }).then(this, [this]() { saved(); });
    }

    void LibraryController::clearLibrary(const QString &path) {
        Workspace->clearLevelOverride(path);
        saved();
    }

    void LibraryController::setLevel(const QString &path, int level) {
        Workspace->setLevelOverride(path, level);
        saved();
    }

    void LibraryController::setLevelLater(const QString &path, int level) {
        PendingLevels.insert(path, level);
        LevelTimer->start();
    }

    QStringList LibraryController::packagesOf(const QString &path) const {
        if (Folder.isEmpty())
            return {};
        if (!Workspace->indexBuilt()) {
            // The menu is shown with what is known; the index is built in the background and changed() follows.
            const_cast<LibraryController *>(this)->refresh();
            return {};
        }
        return Workspace->cachedPackagesOf(path);
    }

    QString LibraryController::preferredSource(const QString &package) const {
        return Workspace->preferredSource(package);
    }

    void LibraryController::setPreferredSource(const QString &package, const QString &path) {
        if (path.isEmpty())
            Workspace->clearPreferredSource(package);
        else
            Workspace->setPreferredSource(package, path);
        saved();
    }

    void LibraryController::refresh() {
        if (Folder.isEmpty()) {
            applyEntries({});
            return;
        }
        if (Scanning) {
            RefreshPending = true;
            return;
        }
        setScanning(true);
        const auto workspace = Workspace;
        // The result carries the folder generation it was made for: if another folder was opened meanwhile, it is dropped.
        QtConcurrent::run([workspace]() {
            const auto generation = workspace->generation();
            workspace->refreshIndex();
            return std::make_pair(generation, workspace->libraries());
        }).then(this, [this](std::pair<quint64, QList<LibraryEntry>> result) {
            setScanning(false);
            if (result.first != Workspace->generation()) {
                RefreshPending = false;
                refresh();
                return;
            }
            IndexSeen = true;
            IndexedGeneration = result.first;
            applyEntries(result.second);
            if (RefreshPending) {
                RefreshPending = false;
                refresh();
            }
        });
    }

    void LibraryController::applyEntries(const QList<LibraryEntry> &entries) {
        Levels.clear();
        Declared.clear();
        for (const auto &entry : entries) {
            Levels.insert(entry.File, entry.Info.Level);
            if (entry.Info.DeclaresLibrary)
                Declared.insert(entry.File);
        }
        Entries->setEntries(entries);
        syncWatcher();
        emit changed();
    }

    void LibraryController::syncWatcher() {
        QSet<QString> wanted;
        if (!Folder.isEmpty() && Workspace->indexBuilt()) {
            const QDir root(Folder);
            wanted.insert(root.absolutePath());
            for (const auto &file : Workspace->indexedFilesIfBuilt()) {
                if (wanted.size() >= MAX_WATCHED_PATHS)
                    break;
                const auto path = root.absoluteFilePath(file);
                wanted.insert(QFileInfo(path).absolutePath());
                wanted.insert(path);
            }
        }

        // Copies first: files() and directories() return temporaries, their iterators must not be mixed.
        const auto watchedFiles = Watcher->files();
        const auto watchedDirectories = Watcher->directories();
        QSet<QString> current(watchedFiles.cbegin(), watchedFiles.cend());
        current.unite(QSet<QString>(watchedDirectories.cbegin(), watchedDirectories.cend()));
        QStringList add;
        QStringList remove;
        for (const auto &path : std::as_const(wanted))
            if (!current.contains(path))
                add.append(path);
        for (const auto &path : current)
            if (!wanted.contains(path))
                remove.append(path);
        if (!remove.isEmpty())
            Watcher->removePaths(remove);
        if (!add.isEmpty())
            Watcher->addPaths(add);
    }

    void LibraryController::onFilesChanged() {
        // Documents notice changed libraries (modification time) at their next parser run; the index checks the folder again.
        Workspace->invalidate();
        if (IndexSeen)
            refresh();
    }
}
