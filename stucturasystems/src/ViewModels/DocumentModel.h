//
// Created by Moritz Herzog on 07.10.26.
//

#ifndef STRUCTURASYSTEMS_DOCUMENTMODEL_H
#define STRUCTURASYSTEMS_DOCUMENTMODEL_H

#include <memory>
#include <vector>
#include <QAbstractListModel>
#include <QByteArray>
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
    class NameIndex;
    class LibraryWorkspace;
    class RemoteLibraries;

    /**
     * Input of one parser run for a single textual element. Plain data, so it can be handed to a worker thread.
     */
    struct ParseInput {
        int Row = -1;      ///< Row at the time of DocumentModel::parseInputs; only the key towards the DocumentWorkspace, not kept in the result
        QString Language;  ///< Normalized language ("SysMLv2" or "KerML")
        std::string Body;
        QString ElementId; ///< Id of the TextualRepresentation of the row; the result is bound to it, so cards may move while the parser runs
        /**
         * DocumentModel::contentEpoch() when the inputs were collected, -1 if unknown (inputs built by hand). Only if every input
         * has an epoch and an element id, the result carries a fingerprint and can be recognized as stale.
         */
        qint64 Epoch = -1;
    };

    /**
     * Result of a parser run. Plain data, produced by DocumentModel::runParser and applied by DocumentModel::applyParseResult.
     * Everything that belongs to a card is bound to the element id of the card (ParseInput::ElementId), not to a row index.
     */
    struct ParseResult {
        /** Problem::ElementId is set for the problems of a card (empty: the whole document); Problem::Row is only set by applyParseResult. */
        QList<Problem> Problems;
        QHash<QString, QList<ElementTreeNode>> ElementTrees;   ///< Root namespace and its owned elements per ParseInput::ElementId, only for error-free rows
        /**
         * The elements of the parsed texts. They are only counted and kept while the document is open; the model of a closed
         * document is taken apart when its workspace is recycled, so it must not be walked after that. (The element trees
         * above are plain data, made while the workspace was locked.)
         */
        std::vector<std::shared_ptr<KerML::Entities::Element>> InstanceElements;
        /** Names are unresolved and the libraries of the project usages on the server are not downloaded yet (see DocumentWorkspace). */
        bool NeedsRemoteDownload = false;
        /** The names of the model (libraries and document) for the code completion, a snapshot of plain data. Null if there is none. */
        std::shared_ptr<const NameIndex> Names;
        /** True if Epoch and Fingerprint describe the inputs (see ParseInput::Epoch). Without them the result is never stale. */
        bool HasProvenance = false;
        qint64 Epoch = -1;           ///< DocumentModel::contentEpoch() of the inputs
        /**
         * What the result was computed from: one digest of (element id, language, body) per input, sorted, so the order of the
         * cards does not matter. The cards refer to each other, so the result is only valid for exactly this set of cards.
         */
        QList<QByteArray> Fingerprint;
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
        /**
         * The elements of all SysML/KerML cards as one tree in card order (see DocumentMerge). Packages of the same name in several
         * cards are one node whose cardRows lists the contributing cards. The pointer never changes.
         *
         * Derived from the element trees of the LAST PARSE, not from the current text: a parse result replaces it, moving, inserting
         * and removing cards re-arranges it (card order decides which card is "first"), but editing a body without a new parse
         * leaves it describing the old text, like the problems of the parser. A card with syntax errors has no tree and
         * contributes nothing until it parses, so "first" cards and merge warnings can shift while a card is broken. If a row edit
         * only changes the rows of the cards, the model is updated in place (dataChanged), otherwise it is reset.
         */
        Q_PROPERTY(StructuraSystems::Client::ElementTreeModel *outline READ outline CONSTANT)
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
            ElementCountRole,
            /**
             * QVariantList of {name, row}: the packages of the card that an earlier card declares first (row = current row of that
             * card). Derived from the trees of the last parse like outline (see there): no tree, no entries.
             */
            ContinuedPackagesRole
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
        [[nodiscard]] ElementTreeModel *outline() const;

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
        /**
         * Lets the document use the libraries of the project usages of its project on the server (SysML v2 API: ProjectUsage),
         * from the cache of @p remote. Only online documents have usages.
         */
        void attachRemote(std::shared_ptr<RemoteLibraries> remote);
        /**
         * Takes over the commit that was created on the server for something else than the content of this document (a project
         * usage). Unlike applyCommit the unsaved changes are kept.
         */
        void updateCommit(std::shared_ptr<SysMLv2::REST::Commit> commit);
        /** Uses a workspace that was prepared (or recycled) elsewhere instead of creating one on the first parser run. */
        void adoptWorkspace(std::shared_ptr<DocumentWorkspace> workspace);
        /** Hands the workspace over (to be recycled when the document is closed). The document creates a new one if it is parsed again. */
        [[nodiscard]] std::shared_ptr<DocumentWorkspace> releaseWorkspace();
        /**
         * Stores the result of a parser run and updates the per row problem counts. GUI thread only.
         * The result is matched to the cards by element id, so it may be applied after cards were moved, inserted or removed since
         * parseInputs; what belongs to a card that is gone is dropped.
         *
         * A result is stale as a whole if the SysML/KerML cards (element id, language, body; the order and Markdown/YAML cards do
         * not matter) differ from the ones it was computed from, or if replaceContent was called since. A stale result is not
         * applied at all: problems, element trees, names and instance elements stay as they were. A result without provenance
         * (inputs built by hand, see ParseInput::Epoch) is always applied.
         * @return false if the result was stale and therefore ignored.
         */
        bool applyParseResult(ParseResult result);
        /**
         * Forgets everything of the last parse: problems, element trees, names and instance elements. For a document that has no
         * SysML/KerML card left. Emits problemRowsChanged (not problemsChanged) if there were problems.
         */
        void clearParseResults();
        /** Incremented by replaceContent; a parse that started before is stale even if ids and texts are equal. */
        [[nodiscard]] qint64 contentEpoch() const;
        /** Synchronous convenience: parseInputs + runParser + applyParseResult. @return problems() */
        QList<Problem> parse();
        /**
         * The problems of the parser followed by the warnings about names that several cards declare ("'A::X' is also declared in
         * card 1", severity 2, on the later card). The latter are derived data: recomputed from the trees of the last parse
         * (see outline) on every parse result and row edit, not from edited text; a card without tree (syntax errors, not parsed
         * yet) takes no part, so the warnings can change while a card is broken.
         */
        [[nodiscard]] QList<Problem> problems() const;
        /**
         * The names of the model as of the last parse, for the code completion. Plain data that can be read on the GUI thread
         * while the document is parsed again. Null before the first parse.
         */
        [[nodiscard]] std::shared_ptr<const NameIndex> nameIndex() const;
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
        /**
         * Emitted when stored problems only changed their row or were dropped because the cards were moved, inserted or removed
         * (not for a parse result or replaceContent, which emit problemsChanged). Read the new state with problems().
         */
        void problemRowsChanged();

    private:
        struct Row {
            std::shared_ptr<KerML::Entities::TextualRepresentation> Element;
            bool Selected = false;
            int ProblemCount = 0;
            ElementTreeModel *ElementTree = nullptr;   ///< Owned by the DocumentModel (QObject parent)
            QString HeaderTitle;
            QString HeaderAuthor;
            QVariantList ContinuedPackages;   ///< Role ContinuedPackagesRole, set by remergeOutline
        };

        void loadRows();
        /** Gives the workspace the project and commit whose usages it may use. */
        void syncRemote();
        void updateHeader(Row &row) const;
        void syncElementOrder();
        void rebuildTableOfContents();
        void setModified(bool modified);
        void markEdited(int row, const QList<int> &roles);
        [[nodiscard]] bool validRow(int row) const;
        /**
         * Sets Problem::Row of the stored problems from their element id and the ProblemCount of the rows; drops the problems of
         * cards that are gone. Call after the rows were changed.
         * @return true if a problem was moved or dropped; the caller emits the signal that fits.
         */
        bool remapProblems();
        /**
         * Merges the element trees the cards hold now (in card order, see DocumentMerge) and updates the outline, the continued
         * packages of the cards and the merge warnings (MergeProblems, with their rows and card numbers). Cheap plain-data work, call
         * whenever the trees or the order of the cards may have changed, before remapProblems (which counts the problems per card).
         * @return true if the merge warnings changed (also only their row or card number).
         */
        bool remergeOutline();
        /** remergeOutline and remapProblems after the rows were changed by the user; emits problemRowsChanged if something changed. */
        void followRowChange();
        /** Drops the problems and the element tree of the card at @p row (it is no code card any more); emits problemRowsChanged if a problem went. */
        void dropResultsOf(int row);

        std::shared_ptr<SysMLv2::REST::Project> Project;
        std::shared_ptr<SysMLv2::REST::Commit> Commit;
        std::vector<std::shared_ptr<KerML::Entities::Element>> Elements;
        std::vector<Row> Rows;
        std::shared_ptr<DocumentWorkspace> Workspace;
        std::shared_ptr<LibraryWorkspace> Libraries;
        QString OwnFile;
        std::shared_ptr<RemoteLibraries> Remote;
        std::vector<std::shared_ptr<KerML::Entities::Element>> InstanceElements;
        QList<Problem> ParserProblems;
        QList<Problem> MergeProblems;     ///< Derived from the trees by remergeOutline, never kept across a change
        ElementTreeModel *Outline = nullptr;   ///< Owned by the DocumentModel (QObject parent)
        std::shared_ptr<const NameIndex> Names;
        qint64 ContentEpoch = 0;
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
