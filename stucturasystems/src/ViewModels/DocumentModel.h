//
// Created by Moritz Herzog on 07.10.26.
//

#ifndef STRUCTURASYSTEMS_DOCUMENTMODEL_H
#define STRUCTURASYSTEMS_DOCUMENTMODEL_H

#include <memory>
#include <vector>
#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include "../Services/CommitDelta.h"
#include "ElementTreeModel.h"
#include "ProblemListModel.h"

namespace SysMLv2::REST {
    class Project;
    class Commit;
}

namespace KerML::Entities {
    class Element;
    class TextualRepresentation;
}

namespace StructuraSystems::Client {
    class DocumentWorkspace;
    class LibraryWorkspace;

    /**
     * Input of one parser run for a single textual element. Plain data, so it can be handed to a worker thread.
     */
    struct ParseInput {
        int Row = -1;
        QString Language;  ///< Normalized language ("SysMLv2" or "KerML")
        std::string Body;
    };

    /**
     * Result of a parser run. Plain data, produced by DocumentModel::runParser and applied by DocumentModel::applyParseResult.
     */
    struct ParseResult {
        QList<Problem> Problems;
        QHash<int, QList<ElementTreeNode>> ElementTrees;   ///< Root namespace and its owned elements per ParseInput::Row, only for error-free rows
        /**
         * The elements of the parsed texts. They are only counted and kept while the document is open; the model of a closed
         * document is taken apart when its workspace is recycled, so it must not be walked after that. (The element trees
         * above are plain data, made while the workspace was locked.)
         */
        std::vector<std::shared_ptr<KerML::Entities::Element>> InstanceElements;
    };

    /**
     * Content of one opened project/commit. Each row is a TextualRepresentation element (markdown, yaml header, SysML or KerML text).
     * Replaces the former CodeWidgetModel and the MarkdownElement widget logic.
     */
    class DocumentModel : public QAbstractListModel {
        Q_OBJECT
        QML_ELEMENT
        QML_UNCREATABLE("DocumentModel instances are owned by AppController.")
        Q_PROPERTY(QString title READ title CONSTANT)
        Q_PROPERTY(bool modified READ modified NOTIFY modifiedChanged)
        Q_PROPERTY(bool isOnline READ isOnline NOTIFY identityChanged)
        Q_PROPERTY(bool isLocalFile READ isLocalFile CONSTANT)
        Q_PROPERTY(int count READ count NOTIFY countChanged)
        Q_PROPERTY(QString projectId READ projectId NOTIFY identityChanged)
        Q_PROPERTY(QString commitId READ commitId NOTIFY identityChanged)
        /** Branch of an online document that the content belongs to. Empty for local documents. */
        Q_PROPERTY(QString branchId READ branchId NOTIFY branchChanged)
        Q_PROPERTY(QString branchName READ branchName NOTIFY branchChanged)
        /** True while an older commit, not the head of the branch, is loaded. The content is read-only then. */
        Q_PROPERTY(bool detached READ detached NOTIFY detachedChanged)
        /** Headings (level 1-3) of all Markdown rows as list of {level, title, row}, used by the [toc] element. */
        Q_PROPERTY(QVariantList tableOfContents READ tableOfContents NOTIFY tableOfContentsChanged)
    public:
        enum Roles {
            BodyRole = Qt::UserRole + 1,
            LanguageRole,
            ElementIdRole,
            HeaderTitleRole,
            HeaderAuthorRole,
            SelectedRole,
            ProblemCountRole,
            ElementTreeRole,
            ElementCountRole
        };
        Q_ENUM(Roles)

        /**
         * Creates a document of a local project, its elements are read from the commit (same as the former CodeWidgetModel).
         */
        DocumentModel(std::shared_ptr<SysMLv2::REST::Project> project, std::shared_ptr<SysMLv2::REST::Commit> commit, QObject *parent = nullptr);

        /**
         * Creates a document of an online project, with the elements already downloaded.
         */
        DocumentModel(std::shared_ptr<SysMLv2::REST::Project> project, std::shared_ptr<SysMLv2::REST::Commit> commit,
                      std::vector<std::shared_ptr<KerML::Entities::Element>> elements, QObject *parent = nullptr);

        ~DocumentModel() override = default;

        int rowCount(const QModelIndex &parent = QModelIndex()) const override;
        QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
        bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
        QHash<int, QByteArray> roleNames() const override;

        [[nodiscard]] QString title() const;
        [[nodiscard]] bool modified() const;
        [[nodiscard]] bool isOnline() const;
        [[nodiscard]] bool isLocalFile() const;
        [[nodiscard]] int count() const;
        [[nodiscard]] QString projectId() const;
        [[nodiscard]] QString commitId() const;
        [[nodiscard]] QString branchId() const;
        [[nodiscard]] QString branchName() const;
        [[nodiscard]] bool detached() const;
        [[nodiscard]] QVariantList tableOfContents() const;

        Q_INVOKABLE void setBody(int row, const QString &body);
        /** @param language one of "Markdown", "YAML", "SysMLv2", "KerML" */
        Q_INVOKABLE void setLanguage(int row, const QString &language);
        /** Moves the row so that it ends up at index @p to. */
        Q_INVOKABLE void moveRow(int from, int to);
        /** Inserts a new, empty element of the given language so that it becomes row @p row. */
        Q_INVOKABLE void insertElement(int row, const QString &language);
        Q_INVOKABLE void removeElement(int row);
        Q_INVOKABLE QStringList selectedElementIds() const;
        Q_INVOKABLE void clearSelection();

        // ---- C++ API used by AppController ----

        [[nodiscard]] std::shared_ptr<SysMLv2::REST::Project> getProject() const;
        [[nodiscard]] std::shared_ptr<SysMLv2::REST::Commit> getCommit() const;

        /**
         * Writes the elements to <basePath>/<project name>. Resets the modified flag on success.
         * @return true if the target file exists after writing.
         */
        bool save(const QString &basePath);

        /** Collects the textual SysML/KerML rows. Must be called on the GUI thread. */
        [[nodiscard]] QList<ParseInput> parseInputs() const;
        /**
         * Thread safe. Parses all inputs, parser exceptions are converted into problems. The names are resolved against the
         * standard library and, if the inputs refer to packages that are not defined in them, against the libraries the
         * workspace finds in the opened folder (see DocumentWorkspace).
         * @param workspace the workspace of the document (see workspace()), which keeps the loaded libraries between the runs.
         *        Null: a workspace that is only used for this run.
         */
        static ParseResult runParser(const QList<ParseInput> &inputs, const QString &documentTitle,
                                     std::shared_ptr<DocumentWorkspace> workspace = nullptr);
        /** The name resolution state of this document. Pass it to runParser. */
        [[nodiscard]] std::shared_ptr<DocumentWorkspace> workspace();
        /**
         * Lets the document use the local libraries of the opened folder.
         * @param libraries index and levels of the libraries, null for none.
         * @param ownFile path of the file of the document relative to the opened folder, empty if the document is not a file of
         *        that folder. The file is not loaded as a library of the document itself.
         */
        void attachLibraries(std::shared_ptr<LibraryWorkspace> libraries, const QString &ownFile = QString());
        [[nodiscard]] bool hasWorkspace() const;
        /** Uses a workspace that was prepared (or recycled) elsewhere instead of creating one on the first parser run. */
        void adoptWorkspace(std::shared_ptr<DocumentWorkspace> workspace);
        /** Hands the workspace over (to be recycled when the document is closed). The document creates a new one if it is parsed again. */
        [[nodiscard]] std::shared_ptr<DocumentWorkspace> releaseWorkspace();
        /** Stores the result of a parser run and updates the per row problem counts. GUI thread only. */
        void applyParseResult(ParseResult result);
        /** Synchronous convenience: parseInputs + runParser + applyParseResult. @return the problems found. */
        QList<Problem> parse();
        [[nodiscard]] QList<Problem> problems() const;
        [[nodiscard]] const std::vector<std::shared_ptr<KerML::Entities::Element>> &instanceElements() const;

        /**
         * First half of a commit (GUI thread): the changes between the state of the branch on the server and this document.
         * A commit only contains what is created, changed or deleted; see computeCommitDelta for the rules.
         * @param base The elements at the head of the branch on the server, empty for a branch without commits.
         */
        [[nodiscard]] CommitDelta commitDelta(const std::vector<std::shared_ptr<KerML::Entities::Element>> &base) const;

        /** Second half of a commit (GUI thread): takes over the commit the backend returned. */
        void applyCommit(std::shared_ptr<SysMLv2::REST::Commit> commit);

        /** Sets the branch of an online document. Has no effect on local documents. */
        void setBranch(const QString &branchId, const QString &branchName);

        /** Marks the document as showing an older commit (read-only). Has no effect on local documents. */
        void setDetached(bool detached);

        /**
         * Replaces the content of an online document by the elements of another commit (branch switch, checkout of a
         * commit). The title stays, the problems, the selection and the modified flag are reset. The branch and the
         * detached state are not touched, see setBranch and setDetached.
         * @return false (and nothing is changed) for documents that are not online.
         */
        bool replaceContent(std::shared_ptr<SysMLv2::REST::Commit> commit, std::vector<std::shared_ptr<KerML::Entities::Element>> elements);

        /** Takes over project and commit created on the backend by an upload of this local document. */
        void applyUpload(std::shared_ptr<SysMLv2::REST::Project> project, std::shared_ptr<SysMLv2::REST::Commit> commit);

        /** Normalizes arbitrary language strings of elements to "Markdown", "YAML", "SysMLv2" or "KerML". */
        static QString normalizeLanguage(const QString &language);

    signals:
        void modifiedChanged();
        void countChanged();
        void identityChanged();
        void branchChanged();
        void detachedChanged();
        void tableOfContentsChanged();
        /** Emitted whenever the content of a row was changed by the user. */
        void edited();
        /** Emitted after a parse result was applied. */
        void problemsChanged();

    private:
        struct Row {
            std::shared_ptr<KerML::Entities::TextualRepresentation> Element;
            bool Selected = false;
            int ProblemCount = 0;
            ElementTreeModel *ElementTree = nullptr;   ///< Owned by the DocumentModel (QObject parent)
            QString HeaderTitle;
            QString HeaderAuthor;
        };

        void loadRows();
        void updateHeader(Row &row) const;
        void syncElementOrder();
        void rebuildTableOfContents();
        void setModified(bool modified);
        void markEdited(int row, const QList<int> &roles);
        [[nodiscard]] bool validRow(int row) const;

        std::shared_ptr<SysMLv2::REST::Project> Project;
        std::shared_ptr<SysMLv2::REST::Commit> Commit;
        std::vector<std::shared_ptr<KerML::Entities::Element>> Elements;
        std::vector<Row> Rows;
        std::shared_ptr<DocumentWorkspace> Workspace;
        std::shared_ptr<LibraryWorkspace> Libraries;
        QString OwnFile;
        std::vector<std::shared_ptr<KerML::Entities::Element>> InstanceElements;
        QList<Problem> ParserProblems;
        bool Modified = false;
        bool IsOnline = false;
        bool IsLocalFile = false;
        QString BranchId;
        QString BranchName;
        bool Detached = false;
        QVariantList TableOfContents;
    };
}

#endif //STRUCTURASYSTEMS_DOCUMENTMODEL_H
