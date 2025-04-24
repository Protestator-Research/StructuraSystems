//
// Created by Moritz Herzog on 23.04.25.
//

#ifndef STRUCTURASYSTEMS_ERRORITEMMODEL_H
#define STRUCTURASYSTEMS_ERRORITEMMODEL_H

#include <QAbstractItemModel>

namespace StructuraSystems::Client {
    class ErrorItemModel : public QAbstractItemModel{
    public:
        ~ErrorItemModel() override;

        QModelIndex index(int row, int column, const QModelIndex &parent) const override;

        QModelIndex parent(const QModelIndex &child) const override;

        int rowCount(const QModelIndex &parent) const override;

        int columnCount(const QModelIndex &parent) const override;

        QVariant data(const QModelIndex &index, int role) const override;

        void removeErrorsFromProject()

    private:


    };
}


#endif //STRUCTURASYSTEMS_ERRORITEMMODEL_H
