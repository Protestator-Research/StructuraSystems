//
// Created by Moritz Herzog on 07.10.26.
//

#ifndef STRUCTURASYSTEMS_APPCONTROLLER_H
#define STRUCTURASYSTEMS_APPCONTROLLER_H

#include <memory>
#include <vector>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

namespace SysMLv2::REST {
    class Project;
}

namespace StructuraSystems::Client {
    class CommunicationService;
    class DigitalTwinProjectModel;
    class DocumentModel;
    class OpenDocumentsModel;
    class ProblemListModel;
    class ProjectItemModel;
    class SettingsController;

    /**
     * Central view model of the application (singleton in QML). Owns the settings, the backend connection, the project
     * lists and the opened documents. All backend communication runs asynchronously, results are applied on the GUI thread.
     * Errors and messages are reported by the notify signal, there is no dialog code in here.
     */
    class AppController : public QObject {
        Q_OBJECT
        QML_ELEMENT
        QML_SINGLETON
        Q_MOC_INCLUDE("DigitalTwinProjectModel.h")
        Q_MOC_INCLUDE("DocumentModel.h")
        Q_MOC_INCLUDE("OpenDocumentsModel.h")
        Q_MOC_INCLUDE("ProblemListModel.h")
        Q_MOC_INCLUDE("SettingsController.h")
        Q_MOC_INCLUDE("../Models/ItemModels/ProjectItemModel.h")
        Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
        Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
        Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
        Q_PROPERTY(StructuraSystems::Client::ProjectItemModel *localProjects READ localProjects CONSTANT)
        Q_PROPERTY(StructuraSystems::Client::ProjectItemModel *onlineProjects READ onlineProjects CONSTANT)
        Q_PROPERTY(StructuraSystems::Client::DigitalTwinProjectModel *digitalTwinProjects READ digitalTwinProjects CONSTANT)
        Q_PROPERTY(bool twinsLoading READ twinsLoading NOTIFY twinsLoadingChanged)
        Q_PROPERTY(StructuraSystems::Client::OpenDocumentsModel *documents READ documents CONSTANT)
        Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged)
        Q_PROPERTY(StructuraSystems::Client::DocumentModel *currentDocument READ currentDocument NOTIFY currentDocumentChanged)
        Q_PROPERTY(StructuraSystems::Client::ProblemListModel *problems READ problems CONSTANT)
        Q_PROPERTY(StructuraSystems::Client::SettingsController *settings READ settings CONSTANT)
    public:
        explicit AppController(QObject *parent = nullptr);
        ~AppController() override;

        [[nodiscard]] bool connected() const;
        [[nodiscard]] bool busy() const;
        [[nodiscard]] QString statusText() const;
        [[nodiscard]] ProjectItemModel *localProjects() const;
        [[nodiscard]] ProjectItemModel *onlineProjects() const;
        [[nodiscard]] DigitalTwinProjectModel *digitalTwinProjects() const;
        /** True while the digital twins are loaded. Independent of busy, the twins are loaded in the background. */
        [[nodiscard]] bool twinsLoading() const;
        [[nodiscard]] OpenDocumentsModel *documents() const;
        [[nodiscard]] int currentIndex() const;
        void setCurrentIndex(int index);
        [[nodiscard]] DocumentModel *currentDocument() const;
        [[nodiscard]] ProblemListModel *problems() const;
        [[nodiscard]] SettingsController *settings() const;

        /** Loads the projects of a folder into the local project list. Also becomes the working directory. */
        Q_INVOKABLE void openFolder(const QUrl &folder);
        /** Adds single files to the local project list and opens them. */
        Q_INVOKABLE void openFiles(const QList<QUrl> &files);
        Q_INVOKABLE void openLocalProject(int row);
        Q_INVOKABLE void openOnlineProject(int row);
        Q_INVOKABLE void closeDocument(int index);

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
        Q_INVOKABLE void commitCurrent(const QString &message);
        Q_INVOKABLE void uploadCurrent();

        Q_INVOKABLE void createLocalProject(const QString &name, const QString &description);
        /** @param visibility "Private", "Internal" or "Public" */
        Q_INVOKABLE void createOnlineProject(const QString &name, const QString &description, const QString &visibility);
        /** Creates a digital twin on the server, that references the current commit of the current online document. */
        Q_INVOKABLE void createDigitalTwin(const QString &name);

    signals:
        void connectedChanged();
        void busyChanged();
        void twinsLoadingChanged();
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
        void runAsync(const QString &description, Work work, Done done);

        void setBusy(bool busy);
        void setConnected(bool connected);
        void setTwinsLoading(bool loading);
        void setStatusText(const QString &text);
        void emitNotify(int level, const QString &message, const QString &details = QString());

        void loadFolder(const QString &folder);
        void loadFile(const QString &filePath, const QString &folder);
        void openLocalProjectByName(const QString &name);
        void openDocument(const QString &key, DocumentModel *document);
        void updateCurrentDocument(int newIndex);
        void refreshProblems();
        void populateOnlineProjects(const std::vector<std::shared_ptr<SysMLv2::REST::Project>> &projects);
        void loadDigitalTwins(const std::vector<std::shared_ptr<SysMLv2::REST::Project>> &projects);
        void cancelDigitalTwinLoading();
        void openWorkingDirectoryIfPresent();
        void onSettingsSaved();
        bool requireConnection();

        SettingsController *Settings;
        std::shared_ptr<CommunicationService> BackendConnection;
        ProjectItemModel *LocalProjects;
        ProjectItemModel *OnlineProjects;
        DigitalTwinProjectModel *DigitalTwinProjects;
        OpenDocumentsModel *Documents;
        ProblemListModel *Problems;

        int CurrentIndex = -1;
        QPointer<DocumentModel> CurrentDocument;
        bool Connected = false;
        bool Busy = false;
        bool TwinsLoading = false;
        /** Incremented for every twin load; results of an outdated load are dropped. */
        quint64 TwinLoadGeneration = 0;
        QString StatusText;
        QString LoadedFolder;
        QHash<QString, QString> LocalFolders;
    };
}

#endif //STRUCTURASYSTEMS_APPCONTROLLER_H
