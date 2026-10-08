//
// Created by Moritz Herzog on 07.10.26.
//

#include "AppController.h"

#include <exception>
#include <stdexcept>
#include <chrono>
#include <utility>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFuture>
#include <QTextStream>
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

#include "DigitalTwinProjectModel.h"
#include "DocumentModel.h"
#include "OpenDocumentsModel.h"
#include "ProblemListModel.h"
#include "SettingsController.h"
#include "../Models/ItemModels/ProjectItemModel.h"
#include "../Models/Parser/StructuraSystemsParser.h"
#include "../Services/BECommunicationService.h"
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

        const QStringList PROJECT_FILE_FILTERS = {"*.md", "*.kerml", "*.sysml", "*.xml", "*.json"};
    }

    AppController::AppController(QObject *parent) :
        QObject(parent),
        Settings(new SettingsController(this)),
        LocalProjects(new ProjectItemModel(this)),
        OnlineProjects(new ProjectItemModel(this)),
        DigitalTwinProjects(new DigitalTwinProjectModel(this)),
        Documents(new OpenDocumentsModel(this)),
        Problems(new ProblemListModel(this)),
        StatusText(tr("Ready")) {
        Settings->initWithDefaultsIfEmpty();
        connect(Settings, &SettingsController::saved, this, &AppController::onSettingsSaved);

        // Deferred, so that the QML side is already connected to notify() when the folder is loaded.
        QTimer::singleShot(0, this, &AppController::openWorkingDirectoryIfPresent);
    }

    AppController::~AppController() = default;

    bool AppController::connected() const { return Connected; }
    bool AppController::busy() const { return Busy; }
    QString AppController::statusText() const { return StatusText; }
    ProjectItemModel *AppController::localProjects() const { return LocalProjects; }
    ProjectItemModel *AppController::onlineProjects() const { return OnlineProjects; }
    DigitalTwinProjectModel *AppController::digitalTwinProjects() const { return DigitalTwinProjects; }
    bool AppController::twinsLoading() const { return TwinsLoading; }
    OpenDocumentsModel *AppController::documents() const { return Documents; }
    int AppController::currentIndex() const { return CurrentIndex; }
    DocumentModel *AppController::currentDocument() const { return CurrentDocument; }
    ProblemListModel *AppController::problems() const { return Problems; }
    SettingsController *AppController::settings() const { return Settings; }

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
                outcome.Error = QString::fromUtf8(ex.what());
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
        const auto files = directory.entryList(PROJECT_FILE_FILTERS, QDir::Files, QDir::Name);
        for (const auto &fileName : files)
            loadFile(directory.absoluteFilePath(fileName), directory.absolutePath());

        setStatusText(tr("Opened %1 (%2 projects)").arg(folder).arg(LocalProjects->count()));
    }

    void AppController::loadFile(const QString &filePath, const QString &folder) {
        try {
            const auto name = filePath.split("/").last();
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
                loadFile(info.absoluteFilePath(), info.absolutePath());
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
        connect(document, &DocumentModel::problemsChanged, this, [this, document]() {
            if (document == CurrentDocument)
                refreshProblems();
        });
        const int index = Documents->append(key, document);
        updateCurrentDocument(index);
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

        Documents->remove(index);
        updateCurrentDocument(newIndex);
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
        QPointer<DocumentModel> guard(document);
        runAsync<ParseResult>(tr("Parsing %1").arg(title),
            [inputs, title]() { return DocumentModel::runParser(inputs, title); },
            [this, guard, title](ParseResult result) {
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
                        result.FirstError = QString::fromUtf8(ex.what());
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

                auto branches = service->getAllBranchesForProjectWithID(project->getId());
                std::shared_ptr<SysMLv2::REST::Branch> mainBranch;
                for (const auto &branch : branches)
                    if (branch != nullptr && project->getDefaultBranch() != nullptr && branch->getId() == project->getDefaultBranch()->getId())
                        mainBranch = branch;
                if (mainBranch == nullptr || mainBranch->getHead() == nullptr)
                    throw std::runtime_error("The project has no default branch with a commit.");

                content.Commit = service->getCommitWithId(project->getId(), mainBranch->getHead()->getId());
                content.Elements = service->getAllElements(content.Commit->getId(), project->getId());
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
                    openDocument(key, new DocumentModel(content.Project, content.Commit, std::move(content.Elements)));
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

        const auto service = BackendConnection;
        const auto project = document->getProject();
        const auto request = document->buildCommitRequest(message);
        QPointer<DocumentModel> guard(document);
        runAsync<std::shared_ptr<SysMLv2::REST::Commit>>(tr("Committing %1").arg(document->title()),
            [service, project, request]() { return service->postCommitWithId(project->getId(), request); },
            [this, guard](std::shared_ptr<SysMLv2::REST::Commit> commit) {
                if (guard)
                    guard->applyCommit(std::move(commit));
                emitNotify(1, tr("Commit created."));
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
        const auto request = document->buildCommitRequest(QStringLiteral("Upload from Local Project, by Structura Systems"));
        QPointer<DocumentModel> guard(document);
        runAsync<UploadResult>(tr("Uploading %1").arg(document->title()),
            [service, project, request]() {
                UploadResult result;
                result.Project = service->postProject(project->getName(), project->getDescription(), "Main");
                result.Commit = service->postCommitWithId(result.Project->getId(), request);
                result.Projects = service->getAllProjects();
                return result;
            },
            [this, guard](UploadResult result) {
                if (guard)
                    guard->applyUpload(result.Project, result.Commit);
                populateOnlineProjects(result.Projects);
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
        const auto owner = Settings->username().toStdString();
        const auto visibilityText = visibility.toStdString();
        runAsync<ProjectList>(tr("Creating online project"),
            [service, projectName, projectDescription, owner, visibilityText]() {
                if (visibilityText == "Private")
                    service->postProject(projectName, projectDescription, "Main", owner);
                else if (visibilityText == "Internal")
                    service->postProject(projectName, projectDescription, "Main", owner); //TODO fix for group required
                else if (visibilityText == "Public")
                    service->postProject(projectName, projectDescription, "Main");
                else
                    throw std::invalid_argument("Unknown visibility: " + visibilityText);
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
}
