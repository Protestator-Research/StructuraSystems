//
// Created by Moritz Herzog on 07.10.26.
//

#ifndef STRUCTURASYSTEMS_PROJECTFILTERMODEL_H
#define STRUCTURASYSTEMS_PROJECTFILTERMODEL_H

#include <QSortFilterProxyModel>
#include <QString>
#include <QtQml/qqmlregistration.h>

namespace StructuraSystems::Client {
    /**
     * Filters a ProjectItemModel by name and description (case insensitive). Rows that do not match are removed from
     * the model, so views and keyboard navigation only ever see matching entries.
     */
    class ProjectFilterModel : public QSortFilterProxyModel {
        Q_OBJECT
        QML_ELEMENT
        Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterTextChanged)
        Q_PROPERTY(int count READ count NOTIFY countChanged)
    public:
        explicit ProjectFilterModel(QObject *parent = nullptr);
        ~ProjectFilterModel() override = default;

        [[nodiscard]] QString filterText() const;
        void setFilterText(const QString &filterText);

        /** Number of rows that pass the filter. */
        [[nodiscard]] int count() const;

        /** Maps a row of this model to the row of the source model, -1 if invalid. */
        Q_INVOKABLE int sourceRow(int row) const;

    signals:
        void filterTextChanged();
        void countChanged();

    protected:
        bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

    private:
        void updateCount();

        QString FilterText;
        int Count = 0;
    };
}

#endif //STRUCTURASYSTEMS_PROJECTFILTERMODEL_H
