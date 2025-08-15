#pragma once

#include <QJsonObject>

namespace StructuraSystems::Server
{
    struct Updatable
    {
        virtual bool update(const QJsonObject& json) = 0;
        virtual void updateFields(const QJsonObject& json) = 0;
        virtual ~Updatable() = default;
    };

}
