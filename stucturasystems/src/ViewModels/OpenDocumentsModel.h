//
// Created by Moritz Herzog on 07.10.26.
//

#ifndef STRUCTURASYSTEMS_OPENDOCUMENTSMODEL_H
#define STRUCTURASYSTEMS_OPENDOCUMENTSMODEL_H

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QtQml/qqmlregistration.h>

namespace StructuraSystems::Client {
    class DocumentModel;

    /**
     * List of the currently opened documents (tabs). Owns the DocumentModels.
     */
    class OpenDocumentsModel : public QAbstractListModel {
        Q_OBJECT
        QML_ELEMENT
        QML_UNCREATABLE("OpenDocumentsModel instances are owned by AppController.")
        Q_PROPERTY(int count READ count NOTIFY countChanged)
    public:
        enum Roles {
            TitleRole = Qt::UserRole + 1,
            ModifiedRole,
            DocumentRole
        };
        Q_ENUM(Roles)

        explicit OpenDocumentsModel(QObject *parent = nullptr);
        ~OpenDocumentsModel() override = default;

        int rowCount(const QModelIndex &parent = QModelIndex()) const override;
        QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
        QHash<int, QByteArray> roleNames() const override;

        [[nodiscard]] int count() const;

        /** Takes ownership of the document. @return the index of the new entry. */
        int append(const QString &key, DocumentModel *document);
        /** Removes and deletes the document. */
        void remove(int index);
        [[nodiscard]] int indexOfKey(const QString &key) const;
        [[nodiscard]] DocumentModel *at(int index) const;

    signals:
        void countChanged();

    private:
        struct Entry {
            QString Key;
            DocumentModel *Document = nullptr;
        };

        void documentChanged(DocumentModel *document);

        QList<Entry> Entries;
    };
}

#endif //STRUCTURASYSTEMS_OPENDOCUMENTSMODEL_H
