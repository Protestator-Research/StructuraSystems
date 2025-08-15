#pragma once

#include <QJsonObject>
#include <optional>

namespace StructuraSystems::Server
{
    template<typename T>
    struct FromJsonFactory
    {
        virtual std::optional<T> fromJson(const QJsonObject& json) const = 0;
        virtual ~FromJsonFactory() = default;
    };
}
