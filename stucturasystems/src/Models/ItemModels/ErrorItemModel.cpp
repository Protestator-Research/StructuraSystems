//
// Created by Moritz Herzog on 23.04.25.
//

#include "ErrorItemModel.h"

namespace StructuraSystems::Client {

    ErrorItemModel::~ErrorItemModel() {

    }

    QModelIndex ErrorItemModel::index(int , int , const QModelIndex &) const {
        return QModelIndex();
    }

    QModelIndex ErrorItemModel::parent(const QModelIndex &) const {
        return QModelIndex();
    }

    int ErrorItemModel::rowCount(const QModelIndex &) const {
        return 0;
    }

    int ErrorItemModel::columnCount(const QModelIndex &) const {
        return 0;
    }

    QVariant ErrorItemModel::data(const QModelIndex &, int ) const {
        return QVariant();
    }
}