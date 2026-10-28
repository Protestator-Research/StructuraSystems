//
// Created by Moritz Herzog on 07.10.26.
//

#include "AppController.h"

#include <algorithm>
#include <exception>
#include <stdexcept>
#include <chrono>
#include <map>
#include <utility>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFuture>
#include <QTextStream>
#include <QThreadPool>
#include <QTimer>
#include <QtConcurrent/QtConcurrent>

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/string_generator.hpp>
#include <kerml/root/elements/Element.h>
#include <sysmlv2/rest/entities/Branch.h>
#include <sysmlv2/rest/entities/Commit.h>
#include <sysmlv2/rest/entities/CommitRequest.h>
#include <sysmlv2/rest/entities/DataVersion.h>
#include <sysmlv2/rest/entities/Project.h>
#include <sysmlv2/rest/entities/Tag.h>

#include "BranchListModel.h"
#include "CommitHistoryModel.h"
#include "DigitalTwinProjectModel.h"
#include "DocumentModel.h"
#include "LibraryController.h"
#include "LocalProjectTreeModel.h"
#include "OpenDocumentsModel.h"
#include "ProblemListModel.h"
#include "SettingsController.h"
#include "../Models/ItemModels/ProjectItemModel.h"
#include "../Models/Libraries/DocumentWorkspace.h"
#include "../Models/Parser/StructuraSystemsParser.h"
#include "../Services/BECommunicationService.h"
#include "../Services/CommitDelta.h"
#include "../Services/entities/DigitalTwin.h"
#include "../Services/entities/DigitalTwinRequest.h"

namespace StructuraSystems::Client {
    namespace {
        using ProjectList = std::vector<std::shared_ptr<SysMLv2::REST::Project>>;

        // Result types of the worker functions. They only contain plain data and shared_ptrs, never QObjects.
        struct ConnectResult {
            std::shared_ptr<CommunicationService> Service;
            ProjectList Projects;
        };

        struct OnlineProjectContent {
            std::shared_ptr<SysMLv2::REST::Project> Project;
            std::shared_ptr<SysMLv2::REST::Commit> Commit;
            std::vector<std::shared_ptr<KerML::Entities::Element>> Elements;
            QString BranchId;
            QString BranchName;
        };

        /** The elements of a commit, that are loaded into an already opened document. */
        struct CommitContent {
            std::shared_ptr<SysMLv2::REST::Commit> Commit;
            std::vector<std::shared_ptr<KerML::Entities::Element>> Elements;
            /** Branch the content is shown for. Empty id if the branch does not exist on the server. */
            QString BranchId;
            QString BranchName;
            QString BranchHeadCommitId;
        };

        /** The branch created on the server and, if the document switches to a commit other than the loaded one, its content. */
        struct BranchCreation {
            QString BranchId;
            QString BranchName;
            bool HasContent = false;
            CommitContent Content;
        };

        /** @return why the name cannot be used for a branch, empty if it is fine. Expects a trimmed name. */
        QString branchNameProblem(const QString &name) {
            if (name.isEmpty())
                return QObject::tr("The name must not be empty.");
            for (const auto character : name)
                if (character.category() == QChar::Other_Control)
                    return QObject::tr("The name must not contain control characters.");
            return {};
        }

        struct HistoryLoadResult {
            QString ProjectId;
            QList<BranchInfo> Branches;
            QList<CommitInfo> Commits;
        };

        struct UploadResult {
            std::shared_ptr<SysMLv2::REST::Project> Project;
            std::shared_ptr<SysMLv2::REST::Commit> Commit;
            ProjectList Projects;
        };

        struct TwinLoadResult {
            QList<DigitalTwinProject> Projects;
            int FailedProjects = 0;
            QString FirstError;
        };

        template<typename T>
        struct Outcome {
            T Value{};
            QString Error;
        };

        QString uuidText(const boost::uuids::uuid &id) {
            return QString::fromStdString(boost::uuids::to_string(id));
        }

        /** Downloads a commit and its elements. */
        CommitContent downloadCommit(const std::shared_ptr<CommunicationService> &service, const boost::uuids::uuid &projectId,
                                     const boost::uuids::uuid &commitId) {
            CommitContent content;
            content.Commit = service->getCommitWithId(projectId, commitId);
            if (content.Commit == nullptr)
                throw std::runtime_error("The commit does not exist on the server.");
            content.Elements = service->getAllElements(content.Commit->getId(), projectId);
            return content;
        }

        const QStringList PROJECT_FILE_FILTERS = {"*.md", "*.kerml", "*.sysml", "*.xml", "*.json"};
    }

    AppController::AppController(QObject *parent) :
        QObject(parent),
        Settings(new SettingsController(this)),
        LocalProjects(new ProjectItemModel(this)),
        LocalProjectTree(new LocalProjectTreeModel(LocalProjects, this)),
        Libraries(new LibraryController(this)),
        OnlineProjects(new ProjectItemModel(this)),
        DigitalTwinProjects(new DigitalTwinProjectModel(this)),
        Documents(new OpenDocumentsModel(this)),
        Problems(new ProblemListModel(this)),
        Branches(new BranchListModel(this)),
        CommitHistory(new CommitHistoryModel(this)),
        StatusText(tr("Ready")) {
        Settings->initWithDefaultsIfEmpty();
        // Warm-ups and recycling of the document workspaces run one after another, never in parallel.
        WorkspacePool.setMaxThreadCount(1);
        connect(Settings, &SettingsController::saved, this, &AppController::onSettingsSaved);

        // The tree shows the level of the libraries; it has to follow the settings and the index.
        LocalProjectTree->setLibraryLevelProvider([this](const QString &path) { return Libraries->levelOf(path); });
        connect(Libraries, &LibraryController::changed, LocalProjectTree, &LocalProjectTreeModel::refreshLibraryLevels);
        connect(Libraries, &LibraryController::notify, this, &AppController::notify);

        // Deferred, so that the QML side is already connected to notify() when the folder is loaded.
        QTimer::singleShot(0, this, &AppController::openWorkingDirectoryIfPresent);
    }

    AppController::~AppController() {
        // Do not wait for a warm-up and do not take the workspaces apart on the way out: the process ends anyway.
        DocumentWorkspace::beginShutdown();
        WorkspacePool.clear();
        WorkspacePool.waitForDone();
    }

    bool AppController::connected() const { return Connected; }
    bool AppController::busy() const { return Busy; }
    QString AppController::statusText() const { return StatusText; }
    ProjectItemModel *AppController::localProjects() const { return LocalProjects; }
    LocalProjectTreeModel *AppController::localProjectTree() const { return LocalProjectTree; }
    ProjectItemModel *AppController::onlineProjects() const { return OnlineProjects; }
    DigitalTwinProjectModel *AppController::digitalTwinProjects() const { return DigitalTwinProjects; }
    bool AppController::twinsLoading() const { return TwinsLoading; }
    OpenDocumentsModel *AppController::documents() const { return Documents; }
    int AppController::currentIndex() const { return CurrentIndex; }
    DocumentModel *AppController::currentDocument() const { return CurrentDocument; }
    ProblemListModel *AppController::problems() const { return Problems; }
    SettingsController *AppController::settings() const { return Settings; }
    LibraryController *AppController::libraries() const { return Libraries; }
    BranchListModel *AppController::branches() const { return Branches; }
    CommitHistoryModel *AppController::commitHistory() const { return CommitHistory; }
    bool AppController::historyLoading() const { return HistoryLoading; }

    void AppController::setBusy(bool busy) {
        if (Busy == busy)
            return;
        Busy = busy;
        emit busyChanged();
    }

    void AppController::setConnected(bool connected) {
        if (Connected == connected)
            return;
        Connected = connected;
        emit connectedChanged();
    }

    void AppController::setTwinsLoading(bool loading) {
        if (TwinsLoading == loading)
            return;
        TwinsLoading = loading;
        emit twinsLoadingChanged();
    }

    void AppController::setHistoryLoading(bool loading) {
        if (HistoryLoading == loading)
            return;
        HistoryLoading = loading;
        emit historyLoadingChanged();
    }

    void AppController::setStatusText(const QString &text) {
        if (StatusText == text)
            return;
        StatusText = text;
        emit statusTextChanged();
    }

    void AppController::emitNotify(int level, const QString &message, const QString &details) {
        emit notify(level, message, details);
    }

    template<typename Result, typename Work, typename Done>
    void AppController::runAsync(const QString &description, Work work, Done done) {
        if (Busy) {
            emitNotify(2, tr("Another operation is still running. Please wait until it has finished."));
            return;
        }
        setBusy(true);
        setStatusText(description + QStringLiteral("..."));

        // Exceptions never cross the thread boundary, they are converted into the error text of the outcome.
        auto wrapped = [work = std::move(work)]() -> Outcome<Result> {
            Outcome<Result> outcome;
            try {
                outcome.Value = work();
            } catch (const std::exception &ex) {
                outcome.Error = QString::fromStdString(CommunicationService::describeBackendError(ex));
                if (outcome.Error.isEmpty())
                    outcome.Error = QStringLiteral("Unknown error");
            } catch (...) {
                outcome.Error = QStringLiteral("Unknown error");
            }
            return outcome;
        };

        // The continuation runs in the thread of "this", i.e. the GUI thread, and is dropped if "this" is destroyed.
        QtConcurrent::run(std::move(wrapped)).then(this, [this, description, done = std::move(done)](Outcome<Result> outcome) mutable {
            setBusy(false);
            if (!outcome.Error.isEmpty()) {
                setStatusText(tr("%1 failed").arg(description));
                emitNotify(3, tr("%1 failed.").arg(description), outcome.Error);
                return;
            }
            setStatusText(tr("Ready"));
            done(std::move(outcome.Value));
        });
    }

    bool AppController::requireConnection() {
        if (!Connected || BackendConnection == nullptr) {
            emitNotify(2, tr("Not connected to the backend."));
            return false;
        }
        return true;
    }

    // ------------------------------------------------------------------ local projects

    void AppController::openWorkingDirectoryIfPresent() {
        const auto directory = Settings->workingDirectory();
        if (!directory.isEmpty() && QDir(directory).exists())
            loadFolder(directory);
    }

    void AppController::onSettingsSaved() {
        const auto directory = Settings->workingDirectory();
        if (!directory.isEmpty() && directory != LoadedFolder && QDir(directory).exists())
            loadFolder(directory);
    }

    void AppController::openFolder(const QUrl &folder) {
        const auto path = folder.isLocalFile() ? folder.toLocalFile() : folder.toString();
        if (path.isEmpty() || !QDir(path).exists()) {
            emitNotify(3, tr("The folder does not exist."), path);
            return;
        }
        loadFolder(path);
        // Saving and creating projects is relative to the working directory, so it follows the opened folder.
        Settings->setWorkingDirectory(path);
        Settings->save();
    }

    void AppController::loadFolder(const QString &folder) {
        LoadedFolder = folder;
        LocalProjects->clear();

        const QDir directory(folder);
        const auto root = directory.absolutePath();
        // Only the settings of the folder are read, the files are not touched before a document needs a library.
        Libraries->setFolder(root);

        // Recursive, symbolic links are not followed. Hidden folders (for example .git) are skipped.
        QStringList files;
        QDirIterator iterator(root, PROJECT_FILE_FILTERS, QDir::Files, QDirIterator::Subdirectories);
        while (iterator.hasNext()) {
            const auto relative = directory.relativeFilePath(iterator.next());
            const auto segments = relative.split('/');
            const bool hidden = std::any_of(segments.begin(), segments.end() - 1,
                                            [](const QString &segment) { return segment.startsWith('.'); });
            if (!hidden)
                files.append(relative);
        }

        // Within each folder, subfolders come before files, each sorted alphabetically.
        std::sort(files.begin(), files.end(), [](const QString &left, const QString &right) {
            const auto leftParts = left.split('/');
            const auto rightParts = right.split('/');
            const auto common = std::min(leftParts.size(), rightParts.size());
            for (qsizetype i = 0; i < common; ++i) {
                if (leftParts[i] == rightParts[i])
                    continue;
                const bool leftIsFolder = i < leftParts.size() - 1;
                const bool rightIsFolder = i < rightParts.size() - 1;
                if (leftIsFolder != rightIsFolder)
                    return leftIsFolder;
                return leftParts[i].compare(rightParts[i], Qt::CaseInsensitive) < 0;
            }
            return leftParts.size() < rightParts.size();
        });

        for (const auto &relative : files)
            loadFile(directory.absoluteFilePath(relative), root, relative);

        setStatusText(tr("Opened %1 (%2 projects)").arg(folder).arg(LocalProjects->count()));
    }

    void AppController::loadFile(const QString &filePath, const QString &folder, const QString &name) {
        try {
            const auto project = LocalProjects->createProject(name.toStdString(), "Created from Filesystem");

            StructuraSystemsParser parser;
            const auto elements = parser.readFile(filePath);
            auto commit = std::make_shared<SysMLv2::REST::Commit>("Created from Filesystem", project);
            for (const auto &element : elements) {
                auto dataVersion = std::make_shared<SysMLv2::REST::DataVersion>(boost::uuids::random_generator()(), element);
                commit->addChange(dataVersion);
            }
            project->getDefaultBranch()->setHead(commit);
            LocalFolders.insert(name, folder);
        } catch (const std::exception &ex) {
            emitNotify(3, tr("Could not read %1.").arg(filePath), QString::fromUtf8(ex.what()));
        }
    }

    void AppController::openFiles(const QList<QUrl> &files) {
        for (const auto &url : files) {
            const auto path = url.isLocalFile() ? url.toLocalFile() : url.toString();
            if (path.isEmpty())
                continue;
            const QFileInfo info(path);
            const auto name = info.fileName();

            bool known = false;
            for (const auto &project : LocalProjects->getProjects())
                if (project != nullptr && project->getName() == name.toStdString())
                    known = true;

            if (!known)
                loadFile(info.absoluteFilePath(), info.absolutePath(), name);
            openLocalProjectByName(name);
        }
    }

    void AppController::openLocalProjectByName(const QString &name) {
        const auto projects = LocalProjects->getProjects();
        for (size_t i = 0; i < projects.size(); i++) {
            if (projects[i] != nullptr && projects[i]->getName() == name.toStdString()) {
                openLocalProject(int(i));
                return;
            }
        }
    }

    void AppController::openDocument(const QString &key, DocumentModel *document) {
        // The file of the document counts as its own only if it belongs to the opened folder (files opened from elsewhere
        // may have the same name as a file of the folder).
        QString ownFile;
        if (document->isLocalFile() && QDir::cleanPath(LocalFolders.value(document->title())) == QDir::cleanPath(Libraries->workspace()->root()))
            ownFile = document->title();
        document->attachLibraries(Libraries->workspace(), ownFile);
        // The next document will probably be parsed too; its workspace is made ready meanwhile.
        prepareSpareWorkspace();
        connect(document, &DocumentModel::problemsChanged, this, [this, document]() {
            if (document == CurrentDocument)
                refreshProblems();
        });
        // Commit, branch switch and checkout change the context of the history.
        const auto contextChanged = [this, document]() {
            if (document == CurrentDocument)
                updateHistoryContext();
        };
        connect(document, &DocumentModel::identityChanged, this, contextChanged);
        connect(document, &DocumentModel::branchChanged, this, contextChanged);
        const int index = Documents->append(key, document);
        updateCurrentDocument(index);
    }

    std::shared_ptr<DocumentWorkspace> AppController::takeWorkspace() {
        std::shared_ptr<DocumentWorkspace> workspace;
        {
            std::lock_guard lock(SpareMutex);
            workspace = std::move(SpareWorkspace);
            SpareWorkspace.reset();
        }
        if (workspace == nullptr)
            workspace = std::make_shared<DocumentWorkspace>();
        return workspace;
    }

    void AppController::prepareSpareWorkspace() {
        std::lock_guard lock(SpareMutex);
        if (SpareWorkspace != nullptr)
            return;
        // Parsing the standard library takes seconds. The first parser run of the document that gets this workspace waits for it
        // if it is not ready yet.
        SpareWorkspace = std::make_shared<DocumentWorkspace>();
        const auto workspace = SpareWorkspace;
        WorkspacePool.start([workspace]() { workspace->warmUp(); });
    }

    void AppController::recycleWorkspace(std::shared_ptr<DocumentWorkspace> workspace) {
        if (workspace == nullptr)
            return;
        // Everything happens on the pool thread, including dropping the last reference of a workspace that is not needed:
        // taking a model apart takes a moment and must not block the user interface.
        WorkspacePool.start([this, workspace = std::move(workspace)]() mutable {
            workspace->recycle();
            {
                std::lock_guard lock(SpareMutex);
                if (SpareWorkspace == nullptr)
                    SpareWorkspace = std::move(workspace);
            }
            // Outside the lock: disposing a surplus workspace must not block takeWorkspace() on the GUI thread.
            workspace.reset();
        });
    }

    void AppController::openLocalProject(int row) {
        const auto projects = LocalProjects->getProjects();
        if (row < 0 || row >= int(projects.size()) || projects[row] == nullptr)
            return;

        try {
            const auto &project = projects[row];
            const auto key = QStringLiteral("local:") + QString::fromStdString(project->getName());
            const int existing = Documents->indexOfKey(key);
            if (existing >= 0) {
                updateCurrentDocument(existing);
                return;
            }

            auto commit = project->getDefaultBranch()->getHead();
            openDocument(key, new DocumentModel(project, commit));
        } catch (const std::exception &ex) {
            emitNotify(3, tr("Could not open the project."), QString::fromUtf8(ex.what()));
        }
    }

    void AppController::createLocalProject(const QString &name, const QString &description) {
        const auto trimmedName = name.trimmed();
        if (trimmedName.isEmpty() || trimmedName.contains('/') || trimmedName.contains('\\')) {
            emitNotify(2, tr("Please enter a valid project name."));
            return;
        }
        const auto workingDirectory = Settings->workingDirectory();
        if (workingDirectory.isEmpty() || !QDir(workingDirectory).exists()) {
            emitNotify(2, tr("The working directory does not exist. Please choose one in the settings."), workingDirectory);
            return;
        }

        const auto fileName = trimmedName.endsWith(".md") ? trimmedName : trimmedName + ".md";
        const auto filePath = QDir(workingDirectory).filePath(fileName);
        if (QFile::exists(filePath)) {
            emitNotify(2, tr("A project with this name already exists."), filePath);
            return;
        }

        QFile file(filePath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            emitNotify(3, tr("Could not create the project file."), file.errorString());
            return;
        }
        {
            // Same YAML header as written by the former MainWindowModel.
            QTextStream stream(&file);
            stream << "---\r\n";
            stream << "name: " << trimmedName << "\r\n";
            stream << "title: " << trimmedName << "\r\n";
            stream << "description: " << description << "\r\n";
            stream << "maintainer: " << "\r\n";
            stream << "usage: " << "\r\n";
            stream << "-----\r\n";
        }
        file.flush();
        file.close();

        loadFolder(workingDirectory);
        emitNotify(1, tr("Project %1 created.").arg(fileName));
        openLocalProjectByName(fileName);
    }

    void AppController::deleteLocalProject(int row, const QString &expectedName) {
        const auto projects = LocalProjects->getProjects();
        if (row < 0 || row >= int(projects.size()) || projects[row] == nullptr
            || QString::fromStdString(projects[row]->getName()) != expectedName) {
            emitNotify(2, tr("The project list has changed. Please try again."));
            return;
        }

        const auto name = expectedName;
        const auto filePath = QDir(LocalFolders.value(name, Settings->workingDirectory())).filePath(name);

        if (!QFile::moveToTrash(filePath)) {
            emitNotify(3, tr("Could not move %1 to the recycle bin.").arg(name), filePath);
            return;
        }

        const int openIndex = Documents->indexOfKey(QStringLiteral("local:") + name);
        if (openIndex >= 0)
            closeDocument(openIndex);
        LocalProjects->removeProjectAt(row);
        LocalFolders.remove(name);
        emitNotify(1, tr("Project %1 moved to the recycle bin.").arg(name));
    }

    // ------------------------------------------------------------------ documents

    void AppController::setCurrentIndex(int index) {
        if (index < -1 || index >= Documents->count())
            index = -1;
        updateCurrentDocument(index);
    }

    void AppController::updateCurrentDocument(int newIndex) {
        const auto newDocument = Documents->at(newIndex);
        const bool indexChanged = newIndex != CurrentIndex;
        const bool documentChanged = newDocument != CurrentDocument;
        CurrentIndex = newIndex;
        CurrentDocument = newDocument;
        if (indexChanged)
            emit currentIndexChanged();
        if (documentChanged) {
            emit currentDocumentChanged();
            refreshProblems();
            updateHistoryContext();
            loadHistory();
        }
    }

    void AppController::refreshProblems() {
        if (CurrentDocument)
            Problems->setProblems(CurrentDocument->problems());
        else
            Problems->clear();
    }

    void AppController::closeDocument(int index) {
        if (index < 0 || index >= Documents->count())
            return;

        int newIndex = CurrentIndex;
        if (index < CurrentIndex)
            newIndex--;
        else if (index == CurrentIndex)
            newIndex = qMin(CurrentIndex, Documents->count() - 2);

        const auto closing = Documents->at(index);
        auto workspace = closing != nullptr ? closing->releaseWorkspace() : nullptr;
        Documents->remove(index);
        updateCurrentDocument(newIndex);
        recycleWorkspace(std::move(workspace));
    }

    bool AppController::isLocalProjectOpen(int row) const {
        const auto projects = LocalProjects->getProjects();
        if (row < 0 || row >= int(projects.size()) || projects[row] == nullptr)
            return false;
        return Documents->indexOfKey(QStringLiteral("local:") + QString::fromStdString(projects[row]->getName())) >= 0;
    }

    bool AppController::isOnlineProjectOpen(int row) const {
        const auto projects = OnlineProjects->getProjects();
        if (row < 0 || row >= int(projects.size()) || projects[row] == nullptr)
            return false;
        return Documents->indexOfKey(QStringLiteral("online:") + uuidText(projects[row]->getId())) >= 0;
    }

    void AppController::saveCurrent() {
        const auto document = CurrentDocument;
        if (!document)
            return;
        if (!document->isLocalFile()) {
            emitNotify(2, tr("Online projects are stored with a commit, not as file."));
            return;
        }

        const auto baseFolder = LocalFolders.value(document->title(), Settings->workingDirectory());
        if (document->save(baseFolder))
            emitNotify(1, tr("%1 saved.").arg(document->title()));
        else
            emitNotify(3, tr("Could not save %1.").arg(document->title()), baseFolder);
    }

    bool AppController::hasUnsavedChanges() const {
        for (int i = 0; i < Documents->count(); i++) {
            const auto document = Documents->at(i);
            if (document != nullptr && document->modified())
                return true;
        }
        return false;
    }

    bool AppController::hasUnsavedLocalChanges() const {
        for (int i = 0; i < Documents->count(); i++) {
            const auto document = Documents->at(i);
            if (document != nullptr && document->modified() && document->isLocalFile())
                return true;
        }
        return false;
    }

    bool AppController::saveAllLocal() {
        bool success = true;
        for (int i = 0; i < Documents->count(); i++) {
            const auto document = Documents->at(i);
            if (document == nullptr || !document->modified() || !document->isLocalFile())
                continue;
            const auto baseFolder = LocalFolders.value(document->title(), Settings->workingDirectory());
            if (!document->save(baseFolder)) {
                success = false;
                emitNotify(3, tr("Could not save %1.").arg(document->title()), baseFolder);
            }
        }
        return success;
    }

    void AppController::parseCurrent() {
        const auto document = CurrentDocument;
        if (!document)
            return;

        const auto inputs = document->parseInputs();
        if (inputs.isEmpty()) {
            emitNotify(0, tr("The document contains no SysML or KerML text to parse."));
            return;
        }

        const auto title = document->title();
        if (!document->hasWorkspace())
            document->adoptWorkspace(takeWorkspace());
        const auto workspace = document->workspace();
        QPointer<DocumentModel> guard(document);
        runAsync<ParseResult>(tr("Parsing %1").arg(title),
            [inputs, title, workspace]() { return DocumentModel::runParser(inputs, title, workspace); },
            [this, guard, title](ParseResult result) {
                // The parser may have built the package index of the opened folder.
                Libraries->indexUsed();
                if (!guard)
                    return;
                int errors = 0;
                int warnings = 0;
                for (const auto &problem : std::as_const(result.Problems))
                    (problem.Severity >= 3 ? errors : warnings)++;
                const auto elementCount = result.InstanceElements.size();
                guard->applyParseResult(std::move(result));

                const auto summary = tr("%1: %n error(s), %2 warning(s), %3 element(s)", nullptr, errors).arg(title).arg(warnings).arg(elementCount);
                setStatusText(summary);
                emitNotify(errors > 0 ? 3 : (warnings > 0 ? 2 : 1), summary);
            });
    }

    // ------------------------------------------------------------------ backend

    void AppController::populateOnlineProjects(const ProjectList &projects) {
        OnlineProjects->clear();
        for (const auto &project : projects)
            OnlineProjects->appendProject(project);
        // The project list is shown immediately, the digital twins follow in the background.
        loadDigitalTwins(projects);
    }

    void AppController::loadDigitalTwins(const ProjectList &projects) {
        if (!Connected || BackendConnection == nullptr)
            return;

        const auto generation = ++TwinLoadGeneration;
        setTwinsLoading(true);

        // Own connection: the API implementation is not thread safe and runAsync may use BackendConnection meanwhile.
        const auto connection = BackendConnection->createIndependentConnection();
        auto work = [connection, projects]() {
            TwinLoadResult result;
            for (const auto &project : projects) {
                if (project == nullptr)
                    continue;
                try {
                    DigitalTwinProject entry;
                    entry.ProjectId = QString::fromStdString(boost::uuids::to_string(project->getId()));
                    entry.Name = QString::fromStdString(project->getName());
                    entry.Description = QString::fromStdString(project->getDescription());
                    for (const auto &twin : connection->getAllDigitalTwinsForProject(project->getId())) {
                        if (twin == nullptr)
                            continue;
                        DigitalTwinInfo info;
                        info.Id = QString::fromStdString(boost::uuids::to_string(twin->getId()));
                        info.Name = QString::fromStdString(twin->getName());
                        if (const auto commit = twin->referencedCommit(); commit != nullptr)
                            info.CommitId = QString::fromStdString(boost::uuids::to_string(commit->getId()));
                        const auto created = std::chrono::duration_cast<std::chrono::milliseconds>(twin->created().time_since_epoch());
                        info.Created = QDateTime::fromMSecsSinceEpoch(created.count());
                        entry.Twins.append(info);
                    }
                    result.Projects.append(entry);
                } catch (const std::exception &ex) {
                    if (result.FailedProjects++ == 0)
                        result.FirstError = QString::fromStdString(CommunicationService::describeBackendError(ex));
                } catch (...) {
                    if (result.FailedProjects++ == 0)
                        result.FirstError = QStringLiteral("Unknown error");
                }
            }
            return result;
        };

        QtConcurrent::run(std::move(work)).then(this, [this, generation](TwinLoadResult result) {
            if (generation != TwinLoadGeneration)
                return;
            setTwinsLoading(false);
            DigitalTwinProjects->setProjects(result.Projects);
            if (result.FailedProjects > 0)
                emitNotify(2, tr("The digital twins of %n project(s) could not be loaded.", nullptr, result.FailedProjects),
                           result.FirstError);
        });
    }

    void AppController::cancelDigitalTwinLoading() {
        ++TwinLoadGeneration;
        setTwinsLoading(false);
        DigitalTwinProjects->clear();
    }

    void AppController::refreshDigitalTwins() {
        if (!requireConnection())
            return;
        loadDigitalTwins(OnlineProjects->getProjects());
    }

    void AppController::openDigitalTwinProject(int row) {
        const auto projectId = DigitalTwinProjects->projectIdAt(row).toStdString();
        const auto projects = OnlineProjects->getProjects();
        for (size_t i = 0; i < projects.size(); i++) {
            if (projects[i] != nullptr && boost::uuids::to_string(projects[i]->getId()) == projectId) {
                openOnlineProject(int(i));
                return;
            }
        }
        emitNotify(2, tr("The project is no longer in the list of online projects. Please refresh."));
    }

    void AppController::connectToBackend() {
        if (Connected) {
            emitNotify(2, tr("Online connection already established. Disconnect first to reconnect."));
            return;
        }
        const auto serverPath = Settings->serverPath().toStdString();
        if (serverPath.empty()) {
            emitNotify(2, tr("No server configured. Please enter a server address in the settings."));
            return;
        }
        const auto username = Settings->username().toStdString();
        const auto password = Settings->password().toStdString();
        if (Settings->passwordLoading()) {
            // Another call is still waiting for the keychain, the password read above is not the real one.
            emitNotify(2, tr("The password is still being read from the keychain. Please try again in a moment."));
            return;
        }

        runAsync<ConnectResult>(tr("Connecting to backend"),
            [serverPath, username, password]() {
                ConnectResult result;
                result.Service = std::make_shared<CommunicationService>(serverPath);
                if (!result.Service->setUserForLoginInBackend(username, password))
                    throw std::runtime_error("The login was rejected by the server.");
                result.Projects = result.Service->getAllProjects();
                return result;
            },
            [this](ConnectResult result) {
                BackendConnection = std::move(result.Service);
                setConnected(true);
                populateOnlineProjects(result.Projects);
                // Documents that stayed open while offline get their history back.
                loadHistory();
                setStatusText(tr("Connected to %1").arg(Settings->serverPath()));
                emitNotify(1, tr("Connected to the backend."));
            });
    }

    void AppController::disconnectFromBackend() {
        if (Busy) {
            emitNotify(2, tr("Another operation is still running. Please wait until it has finished."));
            return;
        }
        BackendConnection.reset();
        cancelDigitalTwinLoading();
        clearHistory();
        OnlineProjects->clear();
        setConnected(false);
        setStatusText(tr("Disconnected"));
    }

    void AppController::refreshOnlineProjects() {
        if (!requireConnection())
            return;
        const auto service = BackendConnection;
        runAsync<ProjectList>(tr("Refreshing online projects"),
            [service]() { return service->getAllProjects(); },
            [this](ProjectList projects) { populateOnlineProjects(projects); });
    }

    void AppController::deleteOnlineProject(int row, const QString &expectedName) {
        if (!requireConnection())
            return;
        const auto projects = OnlineProjects->getProjects();
        if (row < 0 || row >= int(projects.size()) || projects[row] == nullptr
            || QString::fromStdString(projects[row]->getName()) != expectedName) {
            emitNotify(2, tr("The project list has changed. Please try again."));
            return;
        }

        const auto service = BackendConnection;
        const auto projectId = projects[row]->getId();
        const auto idText = uuidText(projectId);
        const auto name = QString::fromStdString(projects[row]->getName());
        runAsync<ProjectList>(tr("Deleting %1").arg(name),
            [service, projectId]() {
                service->deleteProject(projectId);
                return service->getAllProjects();
            },
            [this, idText, name](ProjectList remaining) {
                const int openIndex = Documents->indexOfKey(QStringLiteral("online:") + idText);
                if (openIndex >= 0)
                    closeDocument(openIndex);
                if (HistoryProjectId == idText)
                    clearHistory();
                populateOnlineProjects(remaining);
                emitNotify(1, tr("Online project %1 deleted.").arg(name));
            });
    }

    void AppController::openOnlineProject(int row) {
        if (!requireConnection())
            return;
        const auto projects = OnlineProjects->getProjects();
        if (row < 0 || row >= int(projects.size()) || projects[row] == nullptr)
            return;

        const auto project = projects[row];
        const auto key = QStringLiteral("online:") + QString::fromStdString(boost::uuids::to_string(project->getId()));
        const int existing = Documents->indexOfKey(key);
        if (existing >= 0) {
            updateCurrentDocument(existing);
            return;
        }

        const auto service = BackendConnection;
        const auto name = QString::fromStdString(project->getName());
        runAsync<OnlineProjectContent>(tr("Downloading %1").arg(name),
            [service, project]() {
                OnlineProjectContent content;
                content.Project = project;

                if (project->getDefaultBranch() == nullptr)
                    throw std::runtime_error("The project has no default branch.");
                // A new project has a branch without commits: its document is empty and gets the first commit later.
                auto state = service->loadBranchState(project->getId(), project->getDefaultBranch()->getId());
                content.BranchId = uuidText(state.Branch->getId());
                content.BranchName = QString::fromStdString(state.Branch->getName());
                content.Commit = std::move(state.Commit);
                content.Elements = std::move(state.Elements);
                return content;
            },
            [this, key](OnlineProjectContent content) {
                try {
                    // The document could have been opened in the meantime.
                    const int existing = Documents->indexOfKey(key);
                    if (existing >= 0) {
                        updateCurrentDocument(existing);
                        return;
                    }
                    auto *document = new DocumentModel(content.Project, content.Commit, std::move(content.Elements));
                    document->setBranch(content.BranchId, content.BranchName);
                    openDocument(key, document);
                } catch (const std::exception &ex) {
                    emitNotify(3, tr("Could not open the project."), QString::fromUtf8(ex.what()));
                }
            });
    }

    void AppController::commitCurrent(const QString &message) {
        const auto document = CurrentDocument;
        if (!document || !requireConnection())
            return;
        if (!document->isOnline()) {
            emitNotify(2, tr("This project is not online yet. Upload it first."));
            return;
        }
        if (document->detached()) {
            emitNotify(2, tr("You are viewing an older commit. Return to the branch head to commit."));
            return;
        }

        const auto service = BackendConnection;
        const auto project = document->getProject();
        // The commit goes to the branch of the document, the default branch of the project is the fallback.
        auto branchId = document->branchId();
        if (branchId.isEmpty() && project->getDefaultBranch() != nullptr)
            branchId = uuidText(project->getDefaultBranch()->getId());
        if (branchId.isEmpty()) {
            emitNotify(2, tr("The branch of this document is unknown. Refresh the history."));
            return;
        }
        const auto documentBranchId = document->branchId();
        const auto branchIdText = branchId.toStdString();
        QPointer<DocumentModel> guard(document);
        // A commit only contains the changes against the head of the branch on the server. So the head is loaded first, and the
        // delta is computed in the GUI thread, where the elements of the document are not changed concurrently.
        runAsync<BranchState>(tr("Checking %1").arg(document->title()),
            [service, project, branchIdText]() {
                return service->loadBranchState(project->getId(), boost::uuids::string_generator()(branchIdText));
            },
            [this, guard, message, service, project, branchIdText, documentBranchId](BranchState state) {
                if (!guard || guard->branchId() != documentBranchId)
                    return;

                // Other users could have committed since the document was loaded. Committing now would overwrite their work.
                const auto headId = state.Commit != nullptr ? uuidText(state.Commit->getId()) : QString();
                if (headId != guard->commitId()) {
                    emitNotify(3, tr("The branch has new commits on the server."), tr("Reload the project before committing."));
                    return;
                }

                const auto delta = guard->commitDelta(state.Elements);
                if (delta.empty()) {
                    emitNotify(0, tr("Nothing to commit."));
                    return;
                }
                const auto request = buildCommitRequest(message.toStdString(), delta);
                const auto branchName = QString::fromStdString(state.Branch->getName());
                runAsync<std::shared_ptr<SysMLv2::REST::Commit>>(tr("Committing %1").arg(guard->title()),
                    [service, project, request, branchIdText]() {
                        return service->postCommitToBranch(project->getId(), boost::uuids::string_generator()(branchIdText), request);
                    },
                    [this, guard, branchIdText, branchName](std::shared_ptr<SysMLv2::REST::Commit> commit) {
                        if (guard) {
                            // A document without branch continues on the branch that received the commit.
                            if (guard->branchId().isEmpty())
                                guard->setBranch(QString::fromStdString(branchIdText), branchName);
                            guard->applyCommit(std::move(commit));
                        }
                        emitNotify(1, tr("Commit created."));
                        loadHistory();
                    });
            });
    }

    void AppController::uploadCurrent() {
        const auto document = CurrentDocument;
        if (!document || !requireConnection())
            return;
        if (document->isOnline()) {
            emitNotify(2, tr("This project is already online. Use commit to publish changes."));
            return;
        }

        const auto service = BackendConnection;
        const auto project = document->getProject();
        // The project is new, so everything is created. A document without elements only creates the project: the server
        // rejects a commit without changes.
        const auto delta = document->commitDelta({});
        const auto request = delta.empty() ? nullptr : buildCommitRequest("Upload from Local Project, by Structura Systems", delta);
        auto projectName = project->getName();
        if (projectName.empty())
            projectName = document->title().toStdString();
        QPointer<DocumentModel> guard(document);
        runAsync<UploadResult>(tr("Uploading %1").arg(document->title()),
            [service, project, projectName, request]() {
                UploadResult result;
                result.Project = service->postProject(projectName, project->getDescription(), "Main");
                if (request != nullptr) {
                    // The commit goes to the default branch of the new project, by its id.
                    boost::uuids::uuid branchId{};
                    if (result.Project->getDefaultBranch() != nullptr) {
                        branchId = result.Project->getDefaultBranch()->getId();
                    } else {
                        const auto branches = service->getAllBranchesForProjectWithID(result.Project->getId());
                        if (branches.empty() || branches.front() == nullptr)
                            throw std::runtime_error("The new project has no branch.");
                        branchId = branches.front()->getId();
                    }
                    result.Commit = service->postCommitToBranch(result.Project->getId(), branchId, request);
                }
                result.Projects = service->getAllProjects();
                return result;
            },
            [this, guard](UploadResult result) {
                if (guard) {
                    guard->applyUpload(result.Project, result.Commit);
                    if (const auto defaultBranch = result.Project->getDefaultBranch(); defaultBranch != nullptr)
                        guard->setBranch(uuidText(defaultBranch->getId()), QString::fromStdString(defaultBranch->getName()));
                }
                populateOnlineProjects(result.Projects);
                loadHistory();
                emitNotify(1, tr("Project uploaded."));
            });
    }

    void AppController::createOnlineProject(const QString &name, const QString &description, const QString &visibility) {
        if (!requireConnection())
            return;
        if (name.trimmed().isEmpty()) {
            emitNotify(2, tr("Please enter a valid project name."));
            return;
        }

        const auto service = BackendConnection;
        const auto projectName = name.trimmed().toStdString();
        const auto projectDescription = description.toStdString();
        const auto visibilityText = visibility.toStdString();
        runAsync<ProjectList>(tr("Creating online project"),
            [service, projectName, projectDescription, visibilityText]() {
                if (visibilityText != "Private" && visibilityText != "Internal" && visibilityText != "Public")
                    throw std::invalid_argument("Unknown visibility: " + visibilityText);
                // The server uses the logged in user as owner. No group is selectable in the UI yet.
                service->postProject(projectName, projectDescription, "Main");
                return service->getAllProjects();
            },
            [this](ProjectList projects) {
                populateOnlineProjects(projects);
                emitNotify(1, tr("Online project created."));
            });
    }

    void AppController::createDigitalTwin(const QString &name) {
        const auto document = CurrentDocument;
        if (!document || !requireConnection())
            return;
        if (!document->isOnline() || document->getCommit() == nullptr) {
            emitNotify(2, tr("A digital twin can only be created for an online project."));
            return;
        }
        if (name.trimmed().isEmpty()) {
            emitNotify(2, tr("Please enter a name for the digital twin."));
            return;
        }

        const auto twinRequest = std::make_shared<SysMLv2::REST::DigitalTwinRequest>(name.trimmed().toStdString(),
                                                                                      document->getCommit()->getId());
        const auto service = BackendConnection;
        const auto projectId = document->getProject()->getId();
        runAsync<std::shared_ptr<SysMLv2::REST::DigitalTwin>>(tr("Creating digital twin"),
            [service, projectId, twinRequest]() { return service->postDigitalTwinToProject(projectId, twinRequest); },
            [this](std::shared_ptr<SysMLv2::REST::DigitalTwin> digitalTwin) {
                emitNotify(1, tr("Digital twin %1 created.").arg(QString::fromStdString(digitalTwin->getName())));
                refreshDigitalTwins();
            });
    }

    // ------------------------------------------------------------------ branches and history

    void AppController::updateHistoryContext() {
        const auto document = CurrentDocument;
        if (document && document->isOnline()) {
            Branches->setCurrentBranch(document->branchId());
            CommitHistory->setContext(document->commitId(), document->branchId());
        } else {
            Branches->setCurrentBranch(QString());
            CommitHistory->setContext(QString(), QString());
        }
    }

    void AppController::clearHistory() {
        ++HistoryGeneration;
        setHistoryLoading(false);
        HistoryProjectId.clear();
        Branches->clear();
        CommitHistory->clear();
    }

    void AppController::loadHistory() {
        const auto document = CurrentDocument;
        if (!document || !document->isOnline() || !Connected || BackendConnection == nullptr || document->getProject() == nullptr) {
            clearHistory();
            return;
        }

        // The history of another project must not stay visible while the new one loads.
        const auto projectIdText = document->projectId();
        if (projectIdText != HistoryProjectId) {
            Branches->clear();
            CommitHistory->clear();
        }
        HistoryProjectId = projectIdText;

        const auto generation = ++HistoryGeneration;
        setHistoryLoading(true);

        // Own connection, like the digital twins: it does not wait for runAsync and does not share the not thread safe API implementation.
        const auto connection = BackendConnection->createIndependentConnection();
        const auto projectId = document->getProject()->getId();
        QString defaultBranchId;
        if (const auto defaultBranch = document->getProject()->getDefaultBranch(); defaultBranch != nullptr)
            defaultBranchId = uuidText(defaultBranch->getId());

        auto work = [connection, projectId, projectIdText, defaultBranchId]() {
            Outcome<HistoryLoadResult> outcome;
            try {
                outcome.Value.ProjectId = projectIdText;
                for (const auto &branch : connection->getAllBranchesForProjectWithID(projectId)) {
                    if (branch == nullptr)
                        continue;
                    BranchInfo info;
                    info.Id = uuidText(branch->getId());
                    info.Name = QString::fromStdString(branch->getName());
                    if (const auto head = branch->getHead(); head != nullptr)
                        info.HeadCommitId = uuidText(head->getId());
                    info.IsDefault = info.Id == defaultBranchId;
                    outcome.Value.Branches.append(info);
                }

                std::map<boost::uuids::uuid, std::chrono::system_clock::time_point> timestamps;
                for (const auto &commit : connection->getAllCommitsForProject(projectId, &timestamps)) {
                    if (commit == nullptr)
                        continue;
                    CommitInfo info;
                    info.Id = uuidText(commit->getId());
                    info.Description = QString::fromStdString(commit->getDescription());
                    if (const auto created = timestamps.find(commit->getId()); created != timestamps.end()) {
                        const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(created->second.time_since_epoch());
                        info.Created = QDateTime::fromMSecsSinceEpoch(milliseconds.count());
                    }
                    for (const auto &previous : commit->getPreviousCommits())
                        if (previous != nullptr)
                            info.PreviousIds.append(uuidText(previous->getId()));
                    outcome.Value.Commits.append(info);
                }
            } catch (const std::exception &ex) {
                outcome.Error = QString::fromStdString(CommunicationService::describeBackendError(ex));
                if (outcome.Error.isEmpty())
                    outcome.Error = QStringLiteral("Unknown error");
            } catch (...) {
                outcome.Error = QStringLiteral("Unknown error");
            }
            return outcome;
        };

        QtConcurrent::run(std::move(work)).then(this, [this, generation](Outcome<HistoryLoadResult> outcome) {
            if (generation != HistoryGeneration)
                return;
            setHistoryLoading(false);
            if (!outcome.Error.isEmpty()) {
                emitNotify(2, tr("The branches and commits of the project could not be loaded."), outcome.Error);
                return;
            }

            const auto document = CurrentDocument;
            if (!document || document->projectId() != outcome.Value.ProjectId)
                return;

            // Documents without branch (uploaded ones) are on the default branch, the name of a branch is only known from the list.
            QString branchId = document->branchId();
            if (branchId.isEmpty()) {
                for (const auto &branch : std::as_const(outcome.Value.Branches))
                    if (branch.IsDefault)
                        branchId = branch.Id;
            }
            for (const auto &branch : std::as_const(outcome.Value.Branches))
                if (branch.Id == branchId && !branch.Name.isEmpty())
                    document->setBranch(branch.Id, branch.Name);

            updateHistoryContext();
            Branches->setBranches(outcome.Value.Branches);
            CommitHistory->setHistory(outcome.Value.Commits, outcome.Value.Branches);
        });
    }

    void AppController::refreshHistory() {
        if (!requireConnection())
            return;
        if (!CurrentDocument || !CurrentDocument->isOnline()) {
            emitNotify(2, tr("Branches and history are only available for online projects."));
            return;
        }
        loadHistory();
    }

    void AppController::switchBranch(const QString &branchId) {
        const auto document = CurrentDocument;
        if (!document || !requireConnection())
            return;
        if (!document->isOnline()) {
            emitNotify(2, tr("Branches are only available for online projects."));
            return;
        }

        const auto service = BackendConnection;
        const auto projectId = document->getProject()->getId();
        const auto branchIdText = branchId.toStdString();
        QPointer<DocumentModel> guard(document);
        runAsync<CommitContent>(tr("Switching branch"),
            [service, projectId, branchIdText]() {
                const auto id = boost::uuids::string_generator()(branchIdText);
                // The head is read again, other users may have committed since the branch list was loaded.
                auto state = service->loadBranchState(projectId, id);
                CommitContent content;
                content.BranchId = uuidText(state.Branch->getId());
                content.BranchName = QString::fromStdString(state.Branch->getName());
                // A branch without commits shows an empty document.
                if (state.Commit != nullptr)
                    content.BranchHeadCommitId = uuidText(state.Commit->getId());
                content.Commit = std::move(state.Commit);
                content.Elements = std::move(state.Elements);
                return content;
            },
            [this, guard](CommitContent content) {
                if (!guard)
                    return;
                guard->setBranch(content.BranchId, content.BranchName);
                guard->setDetached(false);
                guard->replaceContent(content.Commit, std::move(content.Elements));
                loadHistory();
                emitNotify(1, tr("Switched to branch %1.").arg(content.BranchName));
            });
    }

    void AppController::checkoutCommit(const QString &commitId) {
        const auto document = CurrentDocument;
        if (!document || !requireConnection())
            return;
        if (!document->isOnline()) {
            emitNotify(2, tr("The history is only available for online projects."));
            return;
        }

        const auto service = BackendConnection;
        const auto projectId = document->getProject()->getId();
        const auto commitIdText = commitId.toStdString();
        const auto branchIdText = document->branchId().toStdString();
        QPointer<DocumentModel> guard(document);
        runAsync<CommitContent>(tr("Loading commit %1").arg(commitId.left(8)),
            [service, projectId, commitIdText, branchIdText]() {
                auto content = downloadCommit(service, projectId, boost::uuids::string_generator()(commitIdText));
                // The document is only editable if the commit is the head of its branch.
                if (!branchIdText.empty()) {
                    const auto id = boost::uuids::string_generator()(branchIdText);
                    for (const auto &branch : service->getAllBranchesForProjectWithID(projectId)) {
                        if (branch != nullptr && branch->getId() == id && branch->getHead() != nullptr) {
                            content.BranchId = uuidText(branch->getId());
                            content.BranchName = QString::fromStdString(branch->getName());
                            content.BranchHeadCommitId = uuidText(branch->getHead()->getId());
                        }
                    }
                }
                return content;
            },
            [this, guard, commitId](CommitContent content) {
                if (!guard)
                    return;
                const bool detached = content.BranchHeadCommitId.isEmpty()
                                      || uuidText(content.Commit->getId()) != content.BranchHeadCommitId;
                guard->setDetached(detached);
                guard->replaceContent(content.Commit, std::move(content.Elements));
                if (detached)
                    emitNotify(0, tr("Showing commit %1 (read-only).").arg(commitId.left(8)));
                else
                    emitNotify(0, tr("Showing the head of branch %1.").arg(guard->branchName()));
            });
    }

    void AppController::returnToBranchHead() {
        const auto document = CurrentDocument;
        if (!document || !document->isOnline())
            return;

        auto branchId = document->branchId();
        if (branchId.isEmpty()) {
            for (const auto &branch : Branches->branches())
                if (branch.IsDefault)
                    branchId = branch.Id;
        }
        if (branchId.isEmpty()) {
            emitNotify(2, tr("The branch of this document is unknown. Refresh the history."));
            return;
        }
        switchBranch(branchId);
    }

    bool AppController::branchNameAvailable(const QString &name) const {
        const auto trimmed = name.trimmed();
        if (!branchNameProblem(trimmed).isEmpty())
            return false;
        for (const auto &branch : Branches->branches())
            if (branch.Name.compare(trimmed, Qt::CaseInsensitive) == 0)
                return false;
        return true;
    }

    void AppController::createBranch(const QString &name, const QString &fromCommitId, bool switchToBranch) {
        const auto document = CurrentDocument;
        if (!document || !requireConnection())
            return;
        if (!document->isOnline() || document->getProject() == nullptr) {
            emitNotify(2, tr("Branches are only available for online projects."));
            return;
        }

        const auto branchName = name.trimmed();
        if (const auto problem = branchNameProblem(branchName); !problem.isEmpty()) {
            emitNotify(2, tr("The branch could not be created."), problem);
            return;
        }
        if (!branchNameAvailable(branchName)) {
            emitNotify(2, tr("The branch could not be created."), tr("A branch named %1 already exists.").arg(branchName));
            return;
        }

        const auto loadedCommitId = document->commitId();
        const auto headCommitId = fromCommitId.isEmpty() ? loadedCommitId : fromCommitId;
        if (headCommitId.isEmpty()) {
            emitNotify(2, tr("The branch could not be created."), tr("The project has no commit to start the branch from."));
            return;
        }
        // Switching to a branch that starts at another commit shows that commit, like checkoutCommit.
        const bool loadContent = switchToBranch && headCommitId != loadedCommitId;

        const auto service = BackendConnection;
        const auto projectId = document->getProject()->getId();
        const auto branchNameText = branchName.toStdString();
        const auto headCommitIdText = headCommitId.toStdString();
        QPointer<DocumentModel> guard(document);
        runAsync<BranchCreation>(tr("Creating branch %1").arg(branchName),
            [service, projectId, branchNameText, headCommitIdText, loadContent]() {
                const auto headId = boost::uuids::string_generator()(headCommitIdText);
                const auto branch = service->postBranch(projectId, branchNameText, headId);
                if (branch == nullptr)
                    throw std::runtime_error("The server did not return the new branch.");
                BranchCreation creation;
                creation.BranchId = uuidText(branch->getId());
                creation.BranchName = QString::fromStdString(branch->getName());
                if (loadContent) {
                    creation.Content = downloadCommit(service, projectId, headId);
                    creation.HasContent = true;
                }
                return creation;
            },
            [this, guard, switchToBranch](BranchCreation creation) {
                if (guard && switchToBranch) {
                    // The unsaved changes stay, the new head is the loaded commit (like git checkout -b).
                    guard->setBranch(creation.BranchId, creation.BranchName);
                    guard->setDetached(false);
                    if (creation.HasContent)
                        guard->replaceContent(creation.Content.Commit, std::move(creation.Content.Elements));
                }
                loadHistory();
                if (guard && switchToBranch)
                    emitNotify(1, tr("Created branch %1 and switched to it.").arg(creation.BranchName));
                else
                    emitNotify(1, tr("Created branch %1.").arg(creation.BranchName));
            });
    }
}
