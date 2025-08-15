#pragma once

#include <QMap>
#include <QJsonArray>
#include "../Json/FromJsonFactory.hpp"


namespace StructuraSystems::Server
{
    template<typename T>
    class IdMap : public QMap<qint64, T>
    {
    public:
        IdMap() = default;
        explicit IdMap(const FromJsonFactory<T>& factory, const QJsonArray& array) : QMap<qint64, T>()
        {
            for (const auto& jsonValue : array) {
                if (jsonValue.isObject()) {
                    const auto maybeT = factory.fromJson(jsonValue.toObject());
                    if (maybeT) {
                        QMap<qint64, T>::insert(maybeT->id, *maybeT);
                    }
                }
            }
        }
    };
}

