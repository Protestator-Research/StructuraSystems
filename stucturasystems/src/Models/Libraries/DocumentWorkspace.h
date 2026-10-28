//
// Created by Moritz Herzog on 24.10.26.
//

#ifndef STRUCTURASYSTEMS_DOCUMENTWORKSPACE_H
#define STRUCTURASYSTEMS_DOCUMENTWORKSPACE_H

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include <QList>
#include <QString>
#include <QStringList>

#include "LibraryWorkspace.h"

namespace KerML::Entities {
    class Element;
    class Namespace;
}

namespace SysMLv2::Files {
    class Workspace;
}

namespace StructuraSystems::Client {
    /** One SysML / KerML text block of the document. */
    struct DocumentSource {
        int Row = -1;
        bool KerML = false;
        std::string Text;
    };

    /** A problem found while parsing and resolving. Plain data. */
    struct WorkspaceDiagnostic {
        int Row = -1;        ///< Row of the DocumentSource, -1 if the problem belongs to the document or to a library
        int Line = -1;       ///< 1-based, -1 if unknown
        int Column = -1;     ///< 0-based, -1 if unknown
        int Severity = 3;    ///< 2 = warning, 3 = error
        QString Message;
    };

    /** The model of one DocumentSource. */
    struct WorkspaceRow {
        int Row = -1;
        std::vector<std::shared_ptr<KerML::Entities::Element>> Elements;
        std::shared_ptr<KerML::Entities::Namespace> Root;   ///< Owner of the top level elements, null for empty text
        bool HasSyntaxErrors = false;
    };

    struct WorkspaceResult {
        std::vector<WorkspaceRow> Rows;
        QList<WorkspaceDiagnostic> Diagnostics;
    };

    /**
     * Called by DocumentWorkspace::update() for every row while the workspace is locked. The model of the row may only be read
     * here: once update() returned, the workspace can be recycled (and the model disposed) by another thread.
     */
    using RowVisitor = std::function<void(const WorkspaceRow &)>;

    /**
     * The name resolution of one document: a SysMLv2::Files::Workspace with the standard library (level 0), the local
     * libraries the document needs (levels 1..n, see LibraryWorkspace) and the text blocks of the document
     * (LibraryWorkspace::DocumentLevel).
     *
     * A run first resolves the blocks against the standard library only. Only if references stay unresolved, the first
     * segments of their names are looked up in the package index of the opened folder and the files that define them are
     * loaded, repeatedly (a library can import further libraries). On later runs the loaded libraries are kept and only the
     * blocks of the document are parsed again. If a library changed on disk, its level or preferred source changed, or another
     * folder was opened, the library sources and the document blocks are replaced together; the standard library, which is
     * expensive to parse, is never parsed again.
     *
     * The model of a Workspace refers to itself in cycles and is only freed when its sources are replaced, so a workspace is
     * disposed (all sources replaced by an empty text) before it is dropped; the destructor does that (except while the application
     * shuts down, see beginShutdown()).
     *
     * Thread safety of the model: the elements in a WorkspaceResult stay valid objects, but a recycled or reloaded workspace takes
     * them apart. Read the model inside update() (RowVisitor) or on the thread that owns the document while the document is open
     * and its workspace is not recycled; never use it after the document was closed.
     *
     * update() is thread safe (calls are serialized), so it can run on a worker thread. setLibraries() never waits for a
     * running update(), it takes effect at the start of the next one.
     */
    class DocumentWorkspace {
    public:
        /**
         * @param libraries index and levels of the local libraries, null to resolve against the standard library only.
         * @param ownFile path of the file of the document relative to the opened folder (empty if it has none). The file is
         *        never loaded as a library of itself.
         * @param standardLibrary the level 0 texts, null for the whole standard library (LibraryWorkspace::standardLibrary()).
         *        Parsing the whole standard library takes seconds, a smaller set is meant for tests.
         */
        explicit DocumentWorkspace(std::shared_ptr<LibraryWorkspace> libraries = nullptr, QString ownFile = QString(),
                                   std::shared_ptr<const std::vector<StandardSource>> standardLibrary = nullptr);
        ~DocumentWorkspace();
        DocumentWorkspace(const DocumentWorkspace &) = delete;
        DocumentWorkspace &operator=(const DocumentWorkspace &) = delete;

        /** Sets the libraries and the file of the document. The loaded libraries are dropped by the next update(). */
        void setLibraries(std::shared_ptr<LibraryWorkspace> libraries, QString ownFile);

        /**
         * Loads and resolves the standard library now (it takes seconds), so that the first update() is fast. Meant for a worker
         * thread. Stops early (and leaves nothing behind) if beginShutdown() is called meanwhile.
         */
        void warmUp();

        /** The application quits: running warm-ups stop, and workspaces are no longer disposed (the process ends anyway). */
        static void beginShutdown();

        /**
         * Makes the workspace ready for another document: the document blocks and the libraries are disposed, the standard
         * library stays loaded. Meant for a worker thread.
         */
        void recycle();

        /**
         * Parses and resolves the blocks. A block that the parser cannot handle is reported on its row and does not stop the
         * others. Exceptions that leave the workspace in an unknown state are rethrown after the workspace was reset.
         * @param documentName used as prefix of the source names.
         * @param visitor called with every row while the workspace is locked, see RowVisitor.
         */
        WorkspaceResult update(const std::vector<DocumentSource> &sources, const QString &documentName, const RowVisitor &visitor = {});

        /** Library files that are currently loaded, relative to the opened folder. */
        [[nodiscard]] QStringList loadedLibraryFiles() const;

    private:
        struct LoadedLibrary {
            QString File;
            qint64 Stamp = -1;
            int Level = 1;
            QStringList Triggers;            ///< Package names that made the file necessary
            std::vector<size_t> Sources;
        };

        void ensureSpace();
        /** Replaces all sources but the standard library by an empty text, so that their model is freed. */
        void disposeNonStandard();
        void disposeAll();
        void dropLibraries();
        void applyPending();
        void refreshLibraries(QList<WorkspaceDiagnostic> &diagnostics);
        void reloadLibrary(LoadedLibrary &library);
        void loadLibraryFile(const QString &file, const QString &trigger, QList<WorkspaceDiagnostic> &diagnostics);
        void resolveWithLibraries(QList<WorkspaceDiagnostic> &diagnostics);
        const PackageLookup &lookup(const QString &package);
        LibraryLevelInfo levelOf(const QString &file);
        [[nodiscard]] QString hintFor(const std::string &written, bool isImport);

        mutable std::mutex Mutex;
        std::mutex PendingMutex;
        bool HasPending = false;
        std::shared_ptr<LibraryWorkspace> PendingLibraries;
        QString PendingOwnFile;

        std::shared_ptr<LibraryWorkspace> Libraries;
        QString OwnFile;
        std::shared_ptr<const std::vector<StandardSource>> StandardLibrary;
        std::unique_ptr<SysMLv2::Files::Workspace> Space;
        quint64 LibraryGeneration = 0;
        bool ForceReparse = false;
        std::vector<size_t> DocumentSlots;           ///< Source index per block of the document
        std::vector<std::string> DocumentTexts;
        std::vector<bool> DocumentKerML;
        std::vector<bool> DocumentFailed;            ///< The block could not be parsed; it is tried again with the next update
        std::vector<LoadedLibrary> Loaded;
        std::set<QString> LoadedFiles;
        QStringList AmbiguousRoots;                  ///< Packages that several files define, found while loading the libraries

        // Answers of the library index during one update(): each package and file is asked once, and the folder is checked
        // again at most once.
        std::map<QString, PackageLookup> LookupCache;
        std::map<QString, LibraryLevelInfo> LevelCache;
        bool Rescanned = false;
    };
}

#endif //STRUCTURASYSTEMS_DOCUMENTWORKSPACE_H
