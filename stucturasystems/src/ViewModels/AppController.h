//
// Created by Moritz Herzog on 07.10.26.
//

#ifndef STRUCTURASYSTEMS_APPCONTROLLER_H
#define STRUCTURASYSTEMS_APPCONTROLLER_H

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QThreadPool>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

namespace SysMLv2::REST {
    class Project;
    class Commit;
}

namespace StructuraSystems::Client {
    class BranchListModel;
    class CommitHistoryModel;
    class CommunicationService;
    class DigitalTwinProjectModel;
    class DocumentModel;
    class DocumentWorkspace;
    class LibraryController;
    class LibraryWarmUpStatus;
    class LocalProjectTreeModel;
    class OpenDocumentsModel;
    class ProblemListModel;
    class ProjectItemModel;
    class RemoteLibraries;
    class RemoteUsageModel;
    struct RemoteConnection;
    class SettingsController;

    /** A branch of an online project with its head commit, offered when a project usage is added. */
    struct UsageBranch {
        QString Id;
        QString Name;
        QString HeadCommitId;
    };

    /**
     * Central view model of the application (singleton in QML). Owns the settings, the backend connection, the project
     * lists and the opened documents. All backend communication runs asynchronously, results are applied on the GUI thread.
     * Errors and messages are reported by the notify signal, there is no dialog code in here.
     */
    class AppController : public QObject {
        Q_OBJECT
        QML_ELEMENT
        QML_SINGLETON
        Q_MOC_INCLUDE("BranchListModel.h")
        Q_MOC_INCLUDE("CommitHistoryModel.h")
        Q_MOC_INCLUDE("DigitalTwinProjectModel.h")
        Q_MOC_INCLUDE("DocumentModel.h")
        Q_MOC_INCLUDE("LibraryController.h")
        Q_MOC_INCLUDE("LibraryWarmUpStatus.h")
        Q_MOC_INCLUDE("LocalProjectTreeModel.h")
        Q_MOC_INCLUDE("OpenDocumentsModel.h")
        Q_MOC_INCLUDE("ProblemListModel.h")
        Q_MOC_INCLUDE("SettingsController.h")
        Q_MOC_INCLUDE("../Models/ItemModels/ProjectItemModel.h")
        Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
        Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
        Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
        /** The warm-up of the standard library (loading, progress 0..1 and text), shown in the status bar. */
        Q_PROPERTY(StructuraSystems::Client::LibraryWarmUpStatus *libraryWarmUp READ libraryWarmUp CONSTANT)
        Q_PROPERTY(StructuraSystems::Client::ProjectItemModel *localProjects READ localProjects CONSTANT)
        Q_PROPERTY(StructuraSystems::Client::LocalProjectTreeModel *localProjectTree READ localProjectTree CONSTANT)
        Q_PROPERTY(StructuraSystems::Client::ProjectItemModel *onlineProjects READ onlineProjects CONSTANT)
        Q_PROPERTY(StructuraSystems::Client::DigitalTwinProjectModel *digitalTwinProjects READ digitalTwinProjects CONSTANT)
        Q_PROPERTY(bool twinsLoading READ twinsLoading NOTIFY twinsLoadingChanged)
        Q_PROPERTY(StructuraSystems::Client::OpenDocumentsModel *documents READ documents CONSTANT)
        Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged)
        Q_PROPERTY(StructuraSystems::Client::DocumentModel *currentDocument READ currentDocument NOTIFY currentDocumentChanged)
        Q_PROPERTY(StructuraSystems::Client::ProblemListModel *problems READ problems CONSTANT)
        Q_PROPERTY(StructuraSystems::Client::BranchListModel *branches READ branches CONSTANT)
        Q_PROPERTY(StructuraSystems::Client::CommitHistoryModel *commitHistory READ commitHistory CONSTANT)
        Q_PROPERTY(bool historyLoading READ historyLoading NOTIFY historyLoadingChanged)
        Q_PROPERTY(StructuraSystems::Client::SettingsController *settings READ settings CONSTANT)
        /** The local libraries of the opened folder: levels, preferred sources and the list for the settings. */
        Q_PROPERTY(StructuraSystems::Client::LibraryController *libraries READ libraries CONSTANT)
        /** The project usages of the current online document (stored on the server) and the libraries they lead to. */
        Q_PROPERTY(StructuraSystems::Client::RemoteUsageModel *remoteUsages READ remoteUsages CONSTANT)
        /** True while the usages of the current document are read or changed. */
        Q_PROPERTY(bool remoteUsagesLoading READ remoteUsagesLoading NOTIFY remoteUsagesLoadingChanged)
        /** Branch names of the project picked for a new usage, see loadUsageBranches(). */
        Q_PROPERTY(QStringList usageBranchNames READ usageBranchNames NOTIFY usageBranchNamesChanged)
        /** The ids of the branches in usageBranchNames, in the same order. */
        Q_PROPERTY(QStringList usageBranchIds READ usageBranchIds NOTIFY usageBranchNamesChanged)
        /** Index of the default branch in usageBranchNames, -1 if there is none. */
        Q_PROPERTY(int usageDefaultBranchIndex READ usageDefaultBranchIndex NOTIFY usageBranchNamesChanged)
        /** True while the branches of the project picked for a new usage are loaded. */
        Q_PROPERTY(bool usageBranchesLoading READ usageBranchesLoading NOTIFY usageBranchesLoadingChanged)
    public:
        /** Whether an online project can be added as a usage to the current document, see usageState(). */
        enum class UsageState {
            Ready,          ///< It can be added
            NoDocument,     ///< Nothing is open
            NotOnline,      ///< The current document is a local one
            Self,           ///< It is the project of the current document
            Detached,       ///< The document shows an older commit
            Used,           ///< It is already used (see usageIdOf)
            Offline,        ///< Not connected
            Invalid         ///< The id is empty or no project of the list has it (any more)
        };
        Q_ENUM(UsageState)

        explicit AppController(QObject *parent = nullptr);
        ~AppController() override;

        [[nodiscard]] bool connected() const;
        [[nodiscard]] bool busy() const;
        [[nodiscard]] QString statusText() const;
        [[nodiscard]] LibraryWarmUpStatus *libraryWarmUp() const { return WarmUpStatus; }
        [[nodiscard]] ProjectItemModel *localProjects() const;
        [[nodiscard]] LocalProjectTreeModel *localProjectTree() const;
        [[nodiscard]] ProjectItemModel *onlineProjects() const;
        [[nodiscard]] DigitalTwinProjectModel *digitalTwinProjects() const;
        /** True while the digital twins are loaded. Independent of busy, the twins are loaded in the background. */
        [[nodiscard]] bool twinsLoading() const;
        [[nodiscard]] OpenDocumentsModel *documents() const;
        [[nodiscard]] int currentIndex() const;
        void setCurrentIndex(int index);
        [[nodiscard]] DocumentModel *currentDocument() const;
        [[nodiscard]] ProblemListModel *problems() const;
        /** Branches of the project of the current online document, empty for local documents. */
        [[nodiscard]] BranchListModel *branches() const;
        /** Commits of the project of the current online document, empty for local documents. */
        [[nodiscard]] CommitHistoryModel *commitHistory() const;
        /** True while branches and commits are loaded. Independent of busy, the history is loaded in the background. */
        [[nodiscard]] bool historyLoading() const;
        [[nodiscard]] SettingsController *settings() const;
        [[nodiscard]] LibraryController *libraries() const;
        [[nodiscard]] RemoteUsageModel *remoteUsages() const;
        [[nodiscard]] bool remoteUsagesLoading() const;
        [[nodiscard]] QStringList usageBranchNames() const;
        [[nodiscard]] QStringList usageBranchIds() const;
        [[nodiscard]] int usageDefaultBranchIndex() const;
        [[nodiscard]] bool usageBranchesLoading() const;

        /** Loads the projects of a folder into the local project list. Also becomes the working directory. */
        Q_INVOKABLE void openFolder(const QUrl &folder);
        /** Adds single files to the local project list and opens them. */
        Q_INVOKABLE void openFiles(const QList<QUrl> &files);
        Q_INVOKABLE void openLocalProject(int row);
        Q_INVOKABLE void openOnlineProject(int row);
        Q_INVOKABLE void closeDocument(int index);
        /** @return true if the local project of the given row of localProjects is open as a document. */
        [[nodiscard]] Q_INVOKABLE bool isLocalProjectOpen(int row) const;
        /** @return true if the online project of the given row of onlineProjects is open as a document. */
        [[nodiscard]] Q_INVOKABLE bool isOnlineProjectOpen(int row) const;

        Q_INVOKABLE void connectToBackend();
        Q_INVOKABLE void disconnectFromBackend();
        Q_INVOKABLE void refreshOnlineProjects();
        /** Reloads the digital twins of the online projects in the background, without blocking other operations. */
        Q_INVOKABLE void refreshDigitalTwins();
        /** Opens the project of a row of digitalTwinProjects. */
        Q_INVOKABLE void openDigitalTwinProject(int row);

        Q_INVOKABLE void saveCurrent();
        /** @return true if any open document has unsaved changes. */
        [[nodiscard]] Q_INVOKABLE bool hasUnsavedChanges() const;
        /** @return true if any open local document has unsaved changes (those can be saved without a commit). */
        [[nodiscard]] Q_INVOKABLE bool hasUnsavedLocalChanges() const;
        /** Saves all modified local documents. Online documents are skipped (they need a commit).
         *  @return true if every attempted save succeeded. */
        Q_INVOKABLE bool saveAllLocal();
        Q_INVOKABLE void parseCurrent();
        /**
         * Reads the project usages of the current online document: from the cache, and, if connected, from the server (with the
         * libraries they lead to, which are cached then). The server is not asked while another download runs.
         * @param askServer false reads the cache only (used on a tab switch, where nothing remote is needed yet).
         */
        Q_INVOKABLE void refreshRemoteUsages(bool askServer = true);
        /** Loads the branches (and their heads) of an online project, for addRemoteUsage(). The result is usageBranchNames. */
        Q_INVOKABLE void loadUsageBranches(const QString &projectId);
        /**
         * Whether the online project can be added as a usage to the current document. The project is given by its id (the rows of
         * the list can change while a menu or dialog is open).
         */
        [[nodiscard]] Q_INVOKABLE StructuraSystems::Client::AppController::UsageState usageState(const QString &projectId) const;
        /** The id of the usage of the project in the current document, empty if it is not used. */
        [[nodiscard]] Q_INVOKABLE QString usageIdOf(const QString &projectId) const;
        /**
         * Adds a usage of the head of a branch of another online project to the current document: a new commit on the branch of
         * the document that holds a ProjectUsage. The commit contains nothing else, unsaved changes of the document stay.
         * @param projectId Id of the used project.
         * @param branchId Id of the branch (one of usageBranchIds) whose head is pinned.
         * @return true if the commit was started (the end is reported by the notifications and by busy); false if the request was
         *         refused, with a notification that says why.
         */
        Q_INVOKABLE bool addRemoteUsage(const QString &projectId, const QString &branchId);
        /** Removes a usage (a new commit that deletes the ProjectUsage). */
        Q_INVOKABLE void removeRemoteUsage(const QString &usageId);
        /** Replaces a usage by one of the head of the default branch of the used project (one commit). */
        Q_INVOKABLE void updateRemoteUsage(const QString &usageId);
        Q_INVOKABLE void commitCurrent(const QString &message);
        Q_INVOKABLE void uploadCurrent();

        /** Reloads the branches and the commits of the current online document in the background, without blocking other operations. */
        Q_INVOKABLE void refreshHistory();
        /**
         * Replaces the content of the current online document by the head commit of the given branch and makes it the
         * branch of the document. Unsaved changes are discarded, the QML side has to ask first.
         */
        Q_INVOKABLE void switchBranch(const QString &branchId);
        /**
         * Replaces the content of the current online document by the elements of the given commit. If it is not the
         * head of the branch of the document, the document becomes detached (read-only). Unsaved changes are discarded.
         */
        Q_INVOKABLE void checkoutCommit(const QString &commitId);
        /**
         * Creates a branch on the server, whose head is the given commit.
         * @param name Name of the branch, surrounding whitespace is removed. Must be unique in the project (case-insensitive).
         * @param fromCommitId Commit that becomes the head of the branch, empty for the commit loaded in the current document.
         * @param switchToBranch Makes the new branch the branch of the current document, which ends the detached state.
         *        Unsaved changes are kept if the commit is the loaded one, they are discarded if another commit is loaded.
         */
        Q_INVOKABLE void createBranch(const QString &name, const QString &fromCommitId, bool switchToBranch);
        /** @return true if the name (trimmed) can be used for a new branch: not empty, no control characters, not used yet. */
        [[nodiscard]] Q_INVOKABLE bool branchNameAvailable(const QString &name) const;
        /** Loads the head of the branch of the current document again and ends the detached state. */
        Q_INVOKABLE void returnToBranchHead();

        Q_INVOKABLE void createLocalProject(const QString &name, const QString &description);
        /** @param visibility "Private", "Internal" or "Public" */
        Q_INVOKABLE void createOnlineProject(const QString &name, const QString &description, const QString &visibility);
        /** Creates a digital twin on the server, that references the current commit of the current online document. */
        Q_INVOKABLE void createDigitalTwin(const QString &name);

        /**
         * Moves the file of a local project to the recycle bin and removes the project from the list. If that worked, an
         * open document of the project is closed, unsaved changes are lost, the QML side has to ask first. If the file
         * cannot be moved, nothing changes and the document stays open.
         * @param row Row of localProjects.
         * @param expectedName Name of the project the user confirmed. Nothing is deleted if the project at the row has
         *        another name, because the list changed in the meantime.
         */
        Q_INVOKABLE void deleteLocalProject(int row, const QString &expectedName);
        /**
         * Deletes a project on the server for all users. This cannot be undone, the QML side has to ask first.
         * An open document of the project is closed.
         * @param row Row of onlineProjects.
         * @param expectedName Name of the project the user confirmed. Nothing is deleted if the project at the row has
         *        another name, because the list changed in the meantime.
         */
        Q_INVOKABLE void deleteOnlineProject(int row, const QString &expectedName);

    signals:
        void connectedChanged();
        void busyChanged();
        void twinsLoadingChanged();
        void historyLoadingChanged();
        void remoteUsagesLoadingChanged();
        void usageBranchNamesChanged();
        void usageBranchesLoadingChanged();
        void statusTextChanged();
        void currentIndexChanged();
        void currentDocumentChanged();
        /**
         * @param level 0 = info, 1 = success, 2 = warning, 3 = error
         * @param message short text for the user
         * @param details optional technical details (e.g. the exception text), may be empty
         */
        void notify(int level, const QString &message, const QString &details);

    private:
        template<typename Result, typename Work, typename Done>
        bool runAsync(const QString &description, Work work, Done done);

        void setBusy(bool busy);
        void setConnected(bool connected);
        void setTwinsLoading(bool loading);
        void setHistoryLoading(bool loading);
        void setStatusText(const QString &text);
        void emitNotify(int level, const QString &message, const QString &details = QString());

        void loadFolder(const QString &folder);
        /**
         * Loads a file as local project.
         * @param name Project name, the path relative to folder (for example "sub/model.sysml").
         * @param folder Folder the name is relative to.
         */
        void loadFile(const QString &filePath, const QString &folder, const QString &name);
        void openLocalProjectByName(const QString &name);
        void openDocument(const QString &key, DocumentModel *document);
        /** Parses a document. @param automatic true for the run that follows a download of project usages (no message if busy). */
        void parseDocument(DocumentModel *document, bool automatic);
        /** Downloads the libraries of the project usages of the document in the background (no Busy), then parses it again. */
        void startRemoteDownload(DocumentModel *document);
        void setRemoteUsagesLoading(bool loading);
        /** Every server has its own cache of libraries: <AppData>/libraries/<hash of the server address>/. */
        void updateRemoteCacheDirectory(const QString &serverAddress);
        /** The current online document that usages can be changed on, and the branch of the commit. @return false (with a message) if not. */
        bool usageContext(DocumentModel *&document, QString &branchId);
        void finishUsageChange(const QPointer<DocumentModel> &document, const std::shared_ptr<SysMLv2::REST::Commit> &commit, const QString &message);
        /** The workspace for a document that is parsed for the first time: the spare one (see prepareSpareWorkspace), or a new one. */
        std::shared_ptr<DocumentWorkspace> takeWorkspace();
        /**
         * Starts loading the standard library into a spare workspace, so that the first parser run of the next document does not
         * wait for it. At most one spare exists; it is started when the application starts (with @p reportProgress, so that the status bar
         * shows the loading) and when a document is opened (silently: the progress is only shown for a warm-up that a document waits for,
         * see takeWorkspace()).
         * @param reportProgress show the steps of the warm-up in the status bar (LibraryWarmUpStatus)
         */
        void prepareSpareWorkspace(bool reportProgress = false);
        /** Recycles the workspace of a closed document in the background; it becomes the spare one if there is none. */
        void recycleWorkspace(std::shared_ptr<DocumentWorkspace> workspace);
        void updateCurrentDocument(int newIndex);
        void refreshProblems();
        void populateOnlineProjects(const std::vector<std::shared_ptr<SysMLv2::REST::Project>> &projects);
        void loadDigitalTwins(const std::vector<std::shared_ptr<SysMLv2::REST::Project>> &projects);
        void cancelDigitalTwinLoading();
        void loadHistory();
        void clearHistory();
        void updateHistoryContext();
        void openWorkingDirectoryIfPresent();
        void onSettingsSaved();
        bool requireConnection();

        SettingsController *Settings;
        std::shared_ptr<CommunicationService> BackendConnection;
        ProjectItemModel *LocalProjects;
        LocalProjectTreeModel *LocalProjectTree;
        LibraryController *Libraries;
        /** Name resolution state with the standard library already loaded, for the next document that is opened. */
        std::shared_ptr<DocumentWorkspace> SpareWorkspace;   ///< Guarded by SpareMutex
        /** Whether the warm-up of SpareWorkspace reports to the status bar (read by its thread; set when a document takes a workspace that is not warm). Guarded by SpareMutex. */
        std::shared_ptr<std::atomic<bool>> SpareReports;
        /** Shown in the status bar while the standard library loads; a child of this object, which outlives the warm-up threads (see ~AppController). */
        LibraryWarmUpStatus *WarmUpStatus = nullptr;
        std::mutex SpareMutex;
        /** The libraries of the project usages on the server, with their cache; filled by background downloads. */
        std::shared_ptr<RemoteLibraries> RemoteCache;
        std::shared_ptr<RemoteConnection> RemoteConnectionHolder;
        RemoteUsageModel *RemoteUsages = nullptr;
        /**
         * Key of the cache directory in use, and key of the server the live connection goes to (empty if not connected). Downloads
         * only happen if they are the same: content of one server never lands in the cache of another.
         */
        QString CacheKey;
        QString ConnectedKey;
        /** True until the first connection: then the cache follows the address in the settings, afterwards the address that was connected. */
        bool CacheFollowsSettings = true;
        bool RemoteBusy = false;               ///< A download or a refresh of the usages uses the connection of RemoteConnectionHolder
        bool RemoteUsagesLoading = false;
        quint64 RemoteUsageGeneration = 0;
        QSet<QString> RemoteAttempted;         ///< "<project>@<commit>" whose usages were downloaded automatically (not again until the next explicit parse)
        QList<UsageBranch> UsageBranches;
        quint64 UsageBranchGeneration = 0;
        int UsageDefaultBranch = -1;
        bool UsageBranchesLoading = false;
        /** One thread for the standard library warm-ups and the recycling, so that they neither pile up nor delay runAsync. */
        QThreadPool WorkspacePool;
        /** One thread for the downloads of project usages (they can take a while and are cancelled when the application quits). */
        QThreadPool RemotePool;
        ProjectItemModel *OnlineProjects;
        DigitalTwinProjectModel *DigitalTwinProjects;
        OpenDocumentsModel *Documents;
        ProblemListModel *Problems;
        BranchListModel *Branches;
        CommitHistoryModel *CommitHistory;

        int CurrentIndex = -1;
        QPointer<DocumentModel> CurrentDocument;
        bool Connected = false;
        bool Busy = false;
        bool TwinsLoading = false;
        /** Incremented for every twin load; results of an outdated load are dropped. */
        quint64 TwinLoadGeneration = 0;
        bool HistoryLoading = false;
        /** Incremented for every history load; results of an outdated load are dropped. */
        quint64 HistoryGeneration = 0;
        /** Project whose history is shown in the models. */
        QString HistoryProjectId;
        QString StatusText;
        QString LoadedFolder;
        QHash<QString, QString> LocalFolders;
    };
}

#endif //STRUCTURASYSTEMS_APPCONTROLLER_H
