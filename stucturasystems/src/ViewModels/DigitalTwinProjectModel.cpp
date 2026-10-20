//
// Created by Moritz Herzog on 08.10.26.
//

#include "DigitalTwinProjectModel.h"

#include <QVariantList>
#include <QVariantMap>

namespace StructuraSystems::Client {
    DigitalTwinProjectModel::DigitalTwinProjectModel(QObject *parent) : QAbstractListModel(parent) {
    }

    int DigitalTwinProjectModel::rowCount(const QModelIndex &parent) const {
        if (parent.isValid())
            return 0;
        return int(Projects.size());
    }

    QVariant DigitalTwinProjectModel::data(const QModelIndex &index, int role) const {
        if (!index.isValid() || index.row() < 0 || index.row() >= Projects.size())
            return {};

        const auto &project = Projects.at(index.row());
        switch (role) {
            case ProjectIdRole:
                return project.ProjectId;
            case Qt::DisplayRole:
            case NameRole:
                return project.Name;
            case DescriptionRole:
                return project.Description;
            case TwinCountRole:
                return int(project.Twins.size());
            case TwinsRole: {
                QVariantList twins;
                for (const auto &twin : project.Twins)
                    twins.append(QVariantMap{
                        {QStringLiteral("id"), twin.Id},
                        {QStringLiteral("name"), twin.Name},
                        {QStringLiteral("commitId"), twin.CommitId},
                        {QStringLiteral("created"), twin.Created}
                    });
                return twins;
            }
            default:
                return {};
        }
    }

    QHash<int, QByteArray> DigitalTwinProjectModel::roleNames() const {
        return {
            {ProjectIdRole, "projectId"},
            {NameRole, "name"},
            {DescriptionRole, "description"},
            {TwinCountRole, "twinCount"},
            {TwinsRole, "twins"}
        };
    }

    int DigitalTwinProjectModel::count() const {
        return int(Projects.size());
    }

    QString DigitalTwinProjectModel::projectIdAt(int row) const {
        if (row < 0 || row >= Projects.size())
            return {};
        return Projects.at(row).ProjectId;
    }

    void DigitalTwinProjectModel::setProjects(const QList<DigitalTwinProject> &projects) {
        QList<DigitalTwinProject> withTwins;
        for (const auto &project : projects)
            if (!project.Twins.isEmpty())
                withTwins.append(project);

        const bool countDiffers = withTwins.size() != Projects.size();
        beginResetModel();
        Projects = withTwins;
        endResetModel();
        if (countDiffers)
            emit countChanged();
    }

    void DigitalTwinProjectModel::clear() {
        setProjects({});
    }
}
