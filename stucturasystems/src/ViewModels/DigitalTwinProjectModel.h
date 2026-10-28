//
// Created by Moritz Herzog on 08.10.26.
//

#ifndef STRUCTURASYSTEMS_DIGITALTWINPROJECTMODEL_H
#define STRUCTURASYSTEMS_DIGITALTWINPROJECTMODEL_H

#include <QAbstractListModel>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QtQml/qqmlregistration.h>

namespace StructuraSystems::Client {
    /**
     * Plain data of one digital twin. Safe to create in worker threads.
     */
    struct DigitalTwinInfo {
        QString Id;
        QString Name;
        QString CommitId;
        QDateTime Created;
    };

    /**
     * Plain data of an online project together with its digital twins. Safe to create in worker threads.
     */
    struct DigitalTwinProject {
        QString ProjectId;
        QString Name;
        QString Description;
        QList<DigitalTwinInfo> Twins;
    };

    /**
     * List of the online projects that have at least one digital twin on the server.
     */
    class DigitalTwinProjectModel : public QAbstractListModel {
        Q_OBJECT
        QML_ELEMENT
        QML_UNCREATABLE("DigitalTwinProjectModel instances are owned by AppController.")
        Q_PROPERTY(int count READ count NOTIFY countChanged)
    public:
        enum Roles {
            ProjectIdRole = Qt::UserRole + 1,
            NameRole,
            DescriptionRole,
            TwinCountRole,
            /** List of maps with the keys id, name, commitId and created. */
            TwinsRole
        };
        Q_ENUM(Roles)

        explicit DigitalTwinProjectModel(QObject *parent = nullptr);
        ~DigitalTwinProjectModel() override = default;

        int rowCount(const QModelIndex &parent = QModelIndex()) const override;
        QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
        QHash<int, QByteArray> roleNames() const override;

        [[nodiscard]] int count() const;
        [[nodiscard]] QString projectIdAt(int row) const;

        /** Replaces the content. Projects without digital twins are skipped. */
        void setProjects(const QList<DigitalTwinProject> &projects);
        void clear();

    signals:
        void countChanged();

    private:
        QList<DigitalTwinProject> Projects;
    };
}

#endif //STRUCTURASYSTEMS_DIGITALTWINPROJECTMODEL_H
