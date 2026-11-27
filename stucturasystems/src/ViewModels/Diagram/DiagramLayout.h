//
// Positions of the diagram nodes as stored in the layout line of the generated block (GeneratedBlockGuard):
// a node is identified by the names of the elements on its way from the top level, unnamed elements by their
// place among their siblings ("vehicle::engine", "ChargeBattery::#2"), so the positions survive reloading.
//

#ifndef STRUCTURASYSTEMS_DIAGRAMLAYOUT_H
#define STRUCTURASYSTEMS_DIAGRAMLAYOUT_H

#include <QJsonArray>
#include <QJsonObject>
#include <QPointF>
#include <QRectF>
#include <QString>

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <optional>

#include <Diagram/Semantic/SemanticModel.h>

namespace StructuraSystems::Client::DiagramLayout {

    /// "vehicle::engine"; unnamed elements as "#<place among the siblings>".
    inline QString keyOf(const Diagram::SemanticModel &model, Diagram::ElementId id) {
        QString key;
        for (const Diagram::ElementId step: model.ownershipPath(id)) {
            const auto &element = model.element(step);
            QString segment = QString::fromStdString(element.name());
            if (segment.isEmpty()) {
                const auto &siblings = model.children(element.owner());
                segment = QLatin1Char('#') +
                          QString::number(std::find(siblings.begin(), siblings.end(), step) - siblings.begin());
            }
            key += (key.isEmpty() ? QString() : QStringLiteral("::")) + segment;
        }
        return key;
    }

    /// Every element of the model by its key.
    inline std::map<QString, Diagram::ElementId> elementsByKey(const Diagram::SemanticModel &model) {
        std::map<QString, Diagram::ElementId> result;
        std::function<void(Diagram::ElementId)> visit = [&](Diagram::ElementId owner) {
            for (const Diagram::ElementId child: model.children(owner)) {
                if (model.element(child).kind() == Diagram::ElementKind::Textual)
                    continue;
                result.emplace(keyOf(model, child), child);
                visit(child);
            }
        };
        visit(Diagram::RootId);
        return result;
    }

    /// [x, y] or [x, y, width, height] (whole numbers).
    inline QJsonArray toJson(const QPointF &position) {
        return QJsonArray{qRound(position.x()), qRound(position.y())};
    }

    inline QJsonArray toJson(const QRectF &rect) {
        return QJsonArray{qRound(rect.x()), qRound(rect.y()), qRound(rect.width()), qRound(rect.height())};
    }

    inline std::optional<QPointF> positionOf(const QJsonValue &value) {
        const QJsonArray array = value.toArray();
        if (array.size() < 2 || !array.at(0).isDouble() || !array.at(1).isDouble())
            return std::nullopt;
        return QPointF(std::max(0.0, array.at(0).toDouble()), std::max(0.0, array.at(1).toDouble()));
    }

    /// The size of [x, y, width, height], std::nullopt for [x, y].
    inline std::optional<QSizeF> sizeOf(const QJsonValue &value) {
        const QJsonArray array = value.toArray();
        if (array.size() < 4 || !array.at(2).isDouble() || !array.at(3).isDouble() || array.at(2).toDouble() <= 0 ||
            array.at(3).toDouble() <= 0)
            return std::nullopt;
        return QSizeF(array.at(2).toDouble(), array.at(3).toDouble());
    }
}

#endif //STRUCTURASYSTEMS_DIAGRAMLAYOUT_H
