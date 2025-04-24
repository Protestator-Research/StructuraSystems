//
// Created by Moritz Herzog on 23.04.25.
//

#include "ErrorItemModel.h"

namespace StructuraSystems::Client {

    ErrorItemModel::~ErrorItemModel() {

    }

    QModelIndex ErrorItemModel::index(int row, int column, const QModelIndex &parent) const {
        return QModelIndex();
    }

    QModelIndex ErrorItemModel::parent(const QModelIndex &child) const {
        return QModelIndex();
    }

    int ErrorItemModel::rowCount(const QModelIndex &parent) const {
        return 0;
    }

    int ErrorItemModel::columnCount(const QModelIndex &parent) const {
        return 0;
    }

    QVariant ErrorItemModel::data(const QModelIndex &index, int role) const {
        return QVariant();
    }
}