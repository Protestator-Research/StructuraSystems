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
#include <QtQml/qqmlregistration.h>

#include "ProblemListModel.h"

namespace SysMLv2::REST {
    class Project;
    class Commit;
    class CommitRequest;
}

namespace KerML::Entities {
    class Element;
    class TextualRepresentation;
}

namespace StructuraSystems::Client {
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
    public:
        enum Roles {
            BodyRole = Qt::UserRole + 1,
            LanguageRole,
            ElementIdRole,
            HeaderTitleRole,
            HeaderAuthorRole,
            SelectedRole,
            ProblemCountRole
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
        /** Pure function, thread safe. Parses all inputs, parser exceptions are converted into problems. */
        static ParseResult runParser(const QList<ParseInput> &inputs, const QString &documentTitle);
        /** Stores the result of a parser run and updates the per row problem counts. GUI thread only. */
        void applyParseResult(ParseResult result);
        /** Synchronous convenience: parseInputs + runParser + applyParseResult. @return the problems found. */
        QList<Problem> parse();
        [[nodiscard]] QList<Problem> problems() const;
        [[nodiscard]] const std::vector<std::shared_ptr<KerML::Entities::Element>> &instanceElements() const;

        /**
         * First half of a commit (GUI thread): builds the request containing all elements of the document.
         */
        [[nodiscard]] std::shared_ptr<SysMLv2::REST::CommitRequest> buildCommitRequest(const QString &description) const;

        /** Second half of a commit (GUI thread): takes over the commit the backend returned. */
        void applyCommit(std::shared_ptr<SysMLv2::REST::Commit> commit);

        /** Takes over project and commit created on the backend by an upload of this local document. */
        void applyUpload(std::shared_ptr<SysMLv2::REST::Project> project, std::shared_ptr<SysMLv2::REST::Commit> commit);

        /** Normalizes arbitrary language strings of elements to "Markdown", "YAML", "SysMLv2" or "KerML". */
        static QString normalizeLanguage(const QString &language);

    signals:
        void modifiedChanged();
        void countChanged();
        void identityChanged();
        /** Emitted whenever the content of a row was changed by the user. */
        void edited();
        /** Emitted after a parse result was applied. */
        void problemsChanged();

    private:
        struct Row {
            std::shared_ptr<KerML::Entities::TextualRepresentation> Element;
            bool Selected = false;
            int ProblemCount = 0;
            QString HeaderTitle;
            QString HeaderAuthor;
        };

        void loadRows();
        void updateHeader(Row &row) const;
        void syncElementOrder();
        void setModified(bool modified);
        void markEdited(int row, const QList<int> &roles);
        [[nodiscard]] bool validRow(int row) const;

        std::shared_ptr<SysMLv2::REST::Project> Project;
        std::shared_ptr<SysMLv2::REST::Commit> Commit;
        std::vector<std::shared_ptr<KerML::Entities::Element>> Elements;
        std::vector<Row> Rows;
        std::vector<std::shared_ptr<KerML::Entities::Element>> InstanceElements;
        QList<Problem> ParserProblems;
        bool Modified = false;
        bool IsOnline = false;
        bool IsLocalFile = false;
    };
}

#endif //STRUCTURASYSTEMS_DOCUMENTMODEL_H
