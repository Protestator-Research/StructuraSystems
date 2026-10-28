//
// Created by Moritz Herzog on 24.10.26.
//

#ifndef STRUCTURASYSTEMS_LIBRARYWORKSPACE_H
#define STRUCTURASYSTEMS_LIBRARYWORKSPACE_H

#include <atomic>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include "CodeScanner.h"
#include "WorkspaceConfig.h"

namespace StructuraSystems::Client {
    /** A text of the standard library. The texts are read from the resources once and shared by all documents. */
    struct StandardSource {
        std::string Name;     ///< File name with suffix, e.g. "Base.kerml"
        std::string Text;
        bool KerML = false;
    };

    /** A SysML / KerML text of a project file, ready to be added to a SysMLv2::Files::Workspace. */
    struct LibrarySource {
        QString Name;         ///< Source name for diagnostics: the file path, with "#<n>" appended for the n-th block of a file with several.
        QString Language;     ///< "SysMLv2" or "KerML"
        QString Text;
    };

    /** The level of a project file and how it came about. */
    struct LibraryLevelInfo {
        int Level = 1;
        bool Overridden = false;        ///< The level is set by the user in the workspace file.
        bool DeclaresLibrary = false;   ///< The file declares a `library package`.
        bool InCycle = false;           ///< The file imports (indirectly) itself, the files of the cycle share a level.
    };

    /** Where the files that define a package are. */
    struct PackageLookup {
        QStringList Files;    ///< All files that define the package at the top level, sorted by path.
        QString Chosen;       ///< The file that is used: the preferred one if there is a valid preference, otherwise the first.
        bool Ambiguous = false;   ///< Several files define the package and the user has not picked one.
    };

    /** A local library as shown in the settings. */
    struct LibraryEntry {
        QString File;
        LibraryLevelInfo Info;
        QStringList Packages;
    };

    /**
     * Index and levels of the local libraries of the opened folder, and the cached texts of the standard library.
     *
     * Levels (see SysMLv2::Files::Workspace::setSourceLevel): 0 is the standard library, 1..n are the local libraries and
     * DocumentLevel is the document that is edited (it stays on top, whatever n is). The level of a local file is set
     * automatically as 1 + the highest level of the files whose packages it uses (files in a cycle share a level), unless
     * the user sets a level in `.structurasystems/workspace.json`.
     *
     * Nothing of the folder is read before it is needed: the package index (a lightweight scan of the files, cached by
     * path and modification time) is only built by the first query that needs it, which happens when a document has
     * references that the standard library cannot resolve. All methods are thread safe, so they can be used by the
     * worker thread that parses a document.
     */
    class LibraryWorkspace {
    public:
        static constexpr int StandardLevel = 0;
        static constexpr int MaxLibraryLevel = 999;
        /** Level of the edited document. Higher than any library level, the document is always the top of the stack. */
        static constexpr int DocumentLevel = 1000;

        LibraryWorkspace();
        ~LibraryWorkspace();
        LibraryWorkspace(const LibraryWorkspace &) = delete;
        LibraryWorkspace &operator=(const LibraryWorkspace &) = delete;

        /** The standard library, read from the resources on first use. */
        static const std::vector<StandardSource> &standardLibrary();

        // ---- folder ----

        /** Sets the opened folder, reads its workspace file and forgets the index. An empty path closes the folder. */
        void setRoot(const QString &folder);
        [[nodiscard]] QString root() const;
        /** Increased by every setRoot, so that a document can tell that the folder was replaced. */
        [[nodiscard]] quint64 generation() const;

        // ---- index ----

        [[nodiscard]] bool indexBuilt() const;
        /** The files changed on disk; the index is checked again (cached by modification time) by the next query. */
        void invalidate();
        /** Builds or updates the index now. */
        void refreshIndex();
        /** Relative paths of all indexed files. Builds the index. */
        QStringList indexedFiles();
        /** Relative paths of the files of the index as it is, empty if it was not built. Never reads anything. */
        QStringList indexedFilesIfBuilt() const;
        /**
         * The files that define the package at the top level. Builds the index.
         * @param rescanIfMissing check the folder again before the package is reported as missing (a file may have been added
         *        since the index was made); at most once per 500 ms.
         */
        PackageLookup findPackage(const QString &package, bool rescanIfMissing = false);
        /** Packages a file declares at the top level, as far as the index knows them. Never reads anything; empty for unknown files. */
        QStringList cachedPackagesOf(const QString &file);

        // ---- levels ----

        /** Level of a file; builds the index. */
        LibraryLevelInfo levelInfo(const QString &file);
        /**
         * Level of a file as far as it is known without reading anything: the level set by the user, or the level of a file
         * that the built index shows to declare a library. -1 for all other files.
         */
        [[nodiscard]] int knownLevel(const QString &file);
        /** Files that are libraries: they declare a library package or have a level set by the user. Builds the index. */
        QList<LibraryEntry> libraries();
        /** Why the levels of this file's imports do not fit (a manual level that points the wrong way, a cycle). Builds the index. */
        QStringList levelWarnings(const QString &file);

        // ---- user settings (stored in the workspace file) ----

        void setLevelOverride(const QString &file, int level);
        void clearLevelOverride(const QString &file);
        [[nodiscard]] std::optional<int> levelOverride(const QString &file) const;
        void setPreferredSource(const QString &package, const QString &file);
        void clearPreferredSource(const QString &package);
        [[nodiscard]] QString preferredSource(const QString &package) const;
        /** @return false if the folder has no workspace file that could be written (no folder open, or write error). */
        [[nodiscard]] bool lastSaveSucceeded() const;

        // ---- project files ----

        /** The SysML / KerML blocks of a file, to be added to a workspace. Counts as a file read. */
        QList<LibrarySource> readFile(const QString &file);
        [[nodiscard]] QString absolutePath(const QString &file) const;
        /** Modification time in milliseconds, -1 if the file is gone. Used to find out that a loaded library is outdated. */
        [[nodiscard]] qint64 fileStamp(const QString &file) const;

        // ---- diagnostics ----

        /** Number of project file contents that were read so far (scans and library loads). */
        [[nodiscard]] int fileReadCount() const;
        /** Number of times the index was built or updated. */
        [[nodiscard]] int indexBuildCount() const;

    private:
        struct IndexedFile {
            qint64 Size = -1;
            qint64 Stamp = -1;
            ScanResult Scan;
        };

        [[nodiscard]] bool indexStaleLocked() const;
        /** Brings the index up to date. The folder is scanned without holding Mutex, so that the user interface never waits for it. */
        void ensureIndex(bool force = false);
        void computeLevelsLocked();
        void ensureLevelsLocked();
        [[nodiscard]] QString chosenFileLocked(const QString &package, bool *ambiguous = nullptr, QStringList *files = nullptr) const;
        [[nodiscard]] QStringList dependenciesLocked(const QString &file) const;
        void saveConfigLocked();

        // Lock order: Mutex, then ConfigMutex. The settings have their own lock, so that setting a level in the user interface
        // never waits for a worker thread that is scanning the files.
        mutable std::mutex Mutex;
        std::mutex ScanMutex;           ///< Serializes the scans of the folder; never taken by the user interface
        mutable std::mutex ConfigMutex;
        QString Root;
        quint64 Generation = 0;
        WorkspaceConfig Config;        ///< Guarded by ConfigMutex
        QString ConfigRoot;            ///< Guarded by ConfigMutex, the folder the configuration is saved in
        bool SaveSucceeded = true;     ///< Guarded by ConfigMutex

        std::map<QString, IndexedFile> Files;                   ///< relative path -> scan, sorted by path
        QHash<QString, QStringList> PackageFiles;               ///< package -> files, sorted
        bool IndexBuilt = false;
        bool Dirty = true;
        QDateTime LastValidation;
        bool LastValidationValid = false;
        quint64 InvalidationCount = 0;    ///< Increased by invalidate(), so that a change during a scan is not lost

        std::atomic<bool> LevelsValid{false};
        std::atomic<quint64> ConfigRevision{0};   ///< Increased by every change of the settings, so that levels computed meanwhile are not trusted
        std::map<QString, LibraryLevelInfo> Levels;

        std::atomic<int> FileReads{0};
        std::atomic<int> IndexBuilds{0};
    };
}

#endif //STRUCTURASYSTEMS_LIBRARYWORKSPACE_H
