//
// Created by Moritz Herzog on 30.10.26.
//

#ifndef STRUCTURASYSTEMS_REMOTELIBRARIES_H
#define STRUCTURASYSTEMS_REMOTELIBRARIES_H

#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <set>

#include <QList>
#include <QString>
#include <QStringList>

#include "LibraryWorkspace.h"

namespace StructuraSystems::Client {
    /** A ProjectUsage as the server holds it: the used project, pinned to one of its commits. */
    struct RemoteUsageRef {
        QString UsageId;        ///< "@id" of the usage (the Data in the commit of the using project)
        QString ProjectId;      ///< The used project
        QString CommitId;       ///< The used commit
        QString ProjectName;    ///< Name of the used project, empty if it is not known
    };

    /**
     * The SysML / KerML text of one commit of a project, and the usages of that commit. A commit never changes, so what was
     * fetched is kept in the cache (see RemoteLibraries for the entries that are looked at again once).
     */
    struct RemoteLibraryContent {
        QString ProjectId;
        QString CommitId;
        QString ProjectName;
        /** False if a name could not be read from the server (the name of the project or of a used project); the entry is read again once. */
        bool NameResolved = true;
        /** False for a commit that was only read for its usages (the commit of a document): its texts were not kept. */
        bool HasSources = true;
        QList<LibrarySource> Sources;
        QList<RemoteUsageRef> Usages;
        QStringList Packages;           ///< Packages the sources declare at the top level (computed, not fetched)
    };

    /** A used commit of the chain of usages, on its level. */
    struct RemoteLibrary {
        RemoteLibraryContent Content;
        int Level = 1;
        bool InCycle = false;
        QString Name;                   ///< "<project name>@<first 8 characters of the commit id>", for messages
    };

    /** The libraries that the usages of a commit lead to, as far as they are in the cache. */
    struct RemoteClosure {
        bool RootKnown = false;                 ///< The usages of the commit itself are in the cache
        QList<RemoteLibrary> Libraries;         ///< Ordered by level, lowest first; only commits with SysML / KerML text
        QStringList Problems;                   ///< What is missing, failed or empty, as text for the user
        bool Missing = false;                   ///< Something is not in the cache (a download may help)
        bool Failed = false;                    ///< A download failed
        int MaxLevel = 0;
    };

    /**
     * The libraries that the project usages of an online project lead to (SysML v2 API: ProjectUsage). The usages are stored on
     * the server, in the commit of the using project; this class stores nothing about them. It keeps a cache of what was
     * fetched: for every used commit the SysML / KerML text of its TextualRepresentations and the usages of that commit, in
     * `<cache directory>/<project id>/<commit id>/`. (The application gives every server its own cache directory.) A commit is
     * immutable, so an entry is valid forever, with a precaution: an entry that holds no usages or no text, or a name that
     * could not be read, is looked at once more per session (a commit may have been read while the server was still working on it,
     * or the server was unreachable for the name; the usages themselves are read with a query on the type, see
     * CommunicationService::getCommitData). An entry is written to a temporary folder first and renamed; an existing entry is upgraded in place by writing the new
     * files next to the old ones and switching `meta.json` atomically, so a reader never sees an empty entry.
     *
     * Levels follow the chain of usages: a used project is below its user (level = 1 + the highest level of the projects it
     * uses); projects that use each other share a level.
     *
     * Reading the cache (cached(), closure()) never touches the network and is cheap. Only ensureClosure() calls the fetch
     * function; it is meant for a worker thread and can be cancelled. All methods are thread safe.
     */
    class RemoteLibraries {
    public:
        /**
         * Fetches one commit of a project from the server. Throws on any failure (the text of the exception is shown to the user).
         * @param withSources false: only the usages are needed (the commit of a document), the texts need not be read.
         * @param cancelled becomes true when the application quits; the fetch should stop at the next request and may abort the
         *        transfer in progress.
         */
        using Fetcher = std::function<RemoteLibraryContent(const QString &projectId, const QString &commitId, bool withSources,
                                                           const std::atomic<bool> &cancelled)>;

        RemoteLibraries(QString cacheDirectory, Fetcher fetcher);

        [[nodiscard]] QString cacheDirectory() const;
        /**
         * Another server (or the first call): the cache of that server is used from now on. Nothing is moved. A download that is
         * running notices the change and drops what it fetched from the other server.
         * @return true if the directory was different.
         */
        bool setCacheDirectory(const QString &directory);

        /**
         * A commit from the cache.
         * @param needSources false: an entry that only holds the usages is enough.
         */
        [[nodiscard]] std::optional<RemoteLibraryContent> cached(const QString &projectId, const QString &commitId, bool needSources) const;

        /** The libraries the usages of the commit lead to, from the cache (transitive usages included, cycles protected). */
        [[nodiscard]] RemoteClosure closure(const QString &projectId, const QString &commitId) const;

        /**
         * Makes the closure of the usages of a commit complete in the cache: fetches the usages of the commit and every used
         * commit that is not cached, transitively. A fetch that fails is remembered (see failure()) and the rest goes on.
         * Network access, call it on a worker thread.
         * @return the number of commits that were fetched.
         */
        int ensureClosure(const QString &projectId, const QString &commitId);

        /** Stops running and future ensureClosure() calls as soon as the fetch in progress is done (the application quits). */
        void cancel();

        /** Why the last fetch of the commit failed, empty if it did not (or was not tried). */
        [[nodiscard]] QString failure(const QString &projectId, const QString &commitId) const;
        void clearFailures();
        /** Increased whenever the cache or the list of failures changes; lets a reader know that its copy of closure() is outdated. */
        [[nodiscard]] quint64 generation() const;
        /** Number of fetches made so far. */
        [[nodiscard]] int fetchCount() const;

        /** @return true if the text can be a path component of the cache (a UUID); other ids are refused. */
        [[nodiscard]] static bool validId(const QString &id);

    private:
        // The helpers get the cache directory they work in: a download snapshots it (and its generation) at the start, so that a
        // change of the server in the middle never mixes the caches of two servers.
        [[nodiscard]] static QString entryPath(const QString &directory, const QString &projectId, const QString &commitId);
        [[nodiscard]] static bool needsRevalidation(const RemoteLibraryContent &content);
        [[nodiscard]] std::optional<RemoteLibraryContent> cachedIn(const QString &directory, const QString &projectId, const QString &commitId,
                                                                   bool needSources) const;
        bool store(const QString &directory, const RemoteLibraryContent &content, QString &error);
        bool fetchAndStore(const QString &directory, quint64 generation, const QString &projectId, const QString &commitId, bool needSources,
                           bool recordFailure);
        /** Reads an entry again once per session if it looks incomplete (see the class description). */
        void revalidate(const QString &directory, quint64 generation, const QString &projectId, const QString &commitId, bool needSources);

        mutable std::mutex Mutex;
        QString CacheDirectory;                   ///< Guarded by Mutex
        Fetcher Fetch;
        std::map<QString, QString> Failures;      ///< "<project>@<commit>" -> message; guarded by Mutex
        std::set<QString> Revalidated;            ///< Guarded by Mutex
        std::atomic<int> Fetches{0};
        std::atomic<quint64> Generation{0};
        std::atomic<bool> Cancelled{false};
        std::atomic<quint64> DirectoryGeneration{0};   ///< Increased by every change of the cache directory
    };
}

#endif //STRUCTURASYSTEMS_REMOTELIBRARIES_H
