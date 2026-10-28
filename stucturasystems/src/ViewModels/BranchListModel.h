//
// Created by Moritz Herzog on 18.10.26.
//

#ifndef STRUCTURASYSTEMS_BRANCHLISTMODEL_H
#define STRUCTURASYSTEMS_BRANCHLISTMODEL_H

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QtQml/qqmlregistration.h>

namespace StructuraSystems::Client {
    /**
     * Plain data of one branch of an online project. Safe to create in worker threads.
     */
    struct BranchInfo {
        QString Id;
        QString Name;
        QString HeadCommitId;
        bool IsDefault = false;
    };

    /**
     * The branches of the online project of the current document. The default branch comes first.
     */
    class BranchListModel : public QAbstractListModel {
        Q_OBJECT
        QML_ELEMENT
        QML_UNCREATABLE("BranchListModel instances are owned by AppController.")
        Q_PROPERTY(int count READ count NOTIFY countChanged)
    public:
        enum Roles {
            NameRole = Qt::UserRole + 1,
            BranchIdRole,
            HeadCommitIdRole,
            IsDefaultRole,
            /** True for the branch that is loaded in the current document. */
            IsCurrentRole
        };
        Q_ENUM(Roles)

        explicit BranchListModel(QObject *parent = nullptr);
        ~BranchListModel() override = default;

        int rowCount(const QModelIndex &parent = QModelIndex()) const override;
        QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
        QHash<int, QByteArray> roleNames() const override;

        [[nodiscard]] int count() const;
        [[nodiscard]] QList<BranchInfo> branches() const;
        /** @return the branch with the given id, or an entry with an empty id if there is none. */
        [[nodiscard]] BranchInfo branchWithId(const QString &branchId) const;

        /** Replaces the content, the default branch is moved to the front. */
        void setBranches(QList<BranchInfo> branches);
        /** Marks the branch of the current document. */
        void setCurrentBranch(const QString &branchId);
        void clear();

    signals:
        void countChanged();

    private:
        QList<BranchInfo> Branches;
        QString CurrentBranchId;
    };
}

#endif //STRUCTURASYSTEMS_BRANCHLISTMODEL_H
