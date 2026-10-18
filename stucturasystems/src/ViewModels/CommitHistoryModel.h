//
// Created by Moritz Herzog on 18.10.26.
//

#ifndef STRUCTURASYSTEMS_COMMITHISTORYMODEL_H
#define STRUCTURASYSTEMS_COMMITHISTORYMODEL_H

#include <QAbstractListModel>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

#include "BranchListModel.h"

namespace StructuraSystems::Client {
    /**
     * Plain data of one commit of an online project. Safe to create in worker threads.
     */
    struct CommitInfo {
        QString Id;
        QString Description;
        QDateTime Created;
        /** Ids of the previous commits, the first one is the first parent. */
        QStringList PreviousIds;
    };

    /**
     * The commits of the online project of the current document, newest first. The commits of the current branch
     * (first parent chain from its head) come first, the commits that are not reachable from the branch head follow.
     */
    class CommitHistoryModel : public QAbstractListModel {
        Q_OBJECT
        QML_ELEMENT
        QML_UNCREATABLE("CommitHistoryModel instances are owned by AppController.")
        Q_PROPERTY(int count READ count NOTIFY countChanged)
        Q_PROPERTY(int otherCount READ otherCount NOTIFY countChanged)
    public:
        enum Roles {
            CommitIdRole = Qt::UserRole + 1,
            ShortIdRole,
            DescriptionRole,
            CreatedRole,
            /** True for the commit that is loaded in the current document. */
            IsCurrentRole,
            /** True for the head of the current branch. */
            IsBranchHeadRole,
            /** Names of the branches whose head is this commit. */
            HeadOfRole,
            /** True if the commit is reachable from the head of the current branch. */
            OnBranchRole
        };
        Q_ENUM(Roles)

        explicit CommitHistoryModel(QObject *parent = nullptr);
        ~CommitHistoryModel() override = default;

        int rowCount(const QModelIndex &parent = QModelIndex()) const override;
        QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
        QHash<int, QByteArray> roleNames() const override;

        [[nodiscard]] int count() const;
        /** Number of commits that are not on the current branch. */
        [[nodiscard]] int otherCount() const;

        /** @return the creation time of the commit, an invalid date for unknown commits and commits without a time. */
        Q_INVOKABLE QDateTime createdOf(const QString &commitId) const;

        /** @return the description of the commit, empty for unknown commits and commits without a description. */
        Q_INVOKABLE QString descriptionOf(const QString &commitId) const;
        /** Replaces the commits and branches and sorts the commits. Previous ids that are unknown are ignored. */
        void setHistory(QList<CommitInfo> commits, QList<BranchInfo> branches);
        /** Sets the commit that is loaded in the document and the branch it belongs to, and sorts again. */
        void setContext(const QString &currentCommitId, const QString &currentBranchId);
        void clear();

    signals:
        void countChanged();

    private:
        struct Entry {
            CommitInfo Commit;
            QStringList HeadOf;
            bool OnBranch = true;
            bool IsBranchHead = false;
        };

        void rebuild();

        QList<CommitInfo> Commits;
        QList<BranchInfo> Branches;
        QString CurrentCommitId;
        QString CurrentBranchId;
        QList<Entry> Entries;
        int OtherCount = 0;
    };
}

#endif //STRUCTURASYSTEMS_COMMITHISTORYMODEL_H
