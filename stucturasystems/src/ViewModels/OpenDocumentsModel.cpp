//
// Created by Moritz Herzog on 07.10.26.
//

#include "OpenDocumentsModel.h"
#include "DocumentModel.h"

namespace StructuraSystems::Client {
    OpenDocumentsModel::OpenDocumentsModel(QObject *parent) : QAbstractListModel(parent) {
    }

    int OpenDocumentsModel::rowCount(const QModelIndex &parent) const {
        if (parent.isValid())
            return 0;
        return int(Entries.size());
    }

    QVariant OpenDocumentsModel::data(const QModelIndex &index, int role) const {
        if (!index.isValid() || index.row() < 0 || index.row() >= Entries.size())
            return {};

        auto *document = Entries.at(index.row()).Document;
        switch (role) {
            case Qt::DisplayRole:
            case TitleRole:
                return document->title();
            case ModifiedRole:
                return document->modified();
            case DocumentRole:
                return QVariant::fromValue(document);
            default:
                return {};
        }
    }

    QHash<int, QByteArray> OpenDocumentsModel::roleNames() const {
        return {
            {TitleRole, "title"},
            {ModifiedRole, "modified"},
            {DocumentRole, "document"}
        };
    }

    int OpenDocumentsModel::count() const {
        return int(Entries.size());
    }

    int OpenDocumentsModel::append(const QString &key, DocumentModel *document) {
        const int row = int(Entries.size());
        document->setParent(this);
        beginInsertRows(QModelIndex(), row, row);
        Entries.append({key, document});
        endInsertRows();
        connect(document, &DocumentModel::modifiedChanged, this, [this, document]() { documentChanged(document); });
        emit countChanged();
        return row;
    }

    void OpenDocumentsModel::remove(int index) {
        if (index < 0 || index >= Entries.size())
            return;

        auto *document = Entries.at(index).Document;
        beginRemoveRows(QModelIndex(), index, index);
        Entries.removeAt(index);
        endRemoveRows();
        document->disconnect(this);
        document->deleteLater();
        emit countChanged();
    }

    int OpenDocumentsModel::indexOfKey(const QString &key) const {
        for (int i = 0; i < Entries.size(); i++)
            if (Entries.at(i).Key == key)
                return i;
        return -1;
    }

    DocumentModel *OpenDocumentsModel::at(int index) const {
        return (index >= 0 && index < Entries.size()) ? Entries.at(index).Document : nullptr;
    }

    void OpenDocumentsModel::documentChanged(DocumentModel *document) {
        for (int i = 0; i < Entries.size(); i++) {
            if (Entries.at(i).Document == document) {
                const auto changed = index(i);
                emit dataChanged(changed, changed, {ModifiedRole});
                return;
            }
        }
    }
}
