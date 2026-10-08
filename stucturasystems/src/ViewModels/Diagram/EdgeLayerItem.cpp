//
// Draws all edges of a diagram.
//

#include "EdgeLayerItem.h"
#include "DiagramEdgeModel.h"
#include "DiagramGeometry.h"
#include "DiagramNodeModel.h"

#include <QPainter>
#include <QPolygonF>
#include <QVector>

#include <cmath>

namespace StructuraSystems::Client {

    namespace {
        constexpr qreal MarkerLength = 14.0;
        constexpr qreal MarkerHalfWidth = 6.0;

        /// Triangle whose tip lies at 'tip', pointing away from 'from'.
        QPolygonF arrowHead(const QPointF &tip, const QPointF &from) {
            const QLineF line(tip, from);
            if (line.length() < 1e-6)
                return {};
            const QPointF direction = (from - tip) / line.length();
            const QPointF normal(-direction.y(), direction.x());
            const QPointF base = tip + direction * MarkerLength;
            return QPolygonF({tip, base + normal * MarkerHalfWidth, base - normal * MarkerHalfWidth});
        }

        QPolygonF diamond(const QPointF &tip, const QPointF &from) {
            const QLineF line(tip, from);
            if (line.length() < 1e-6)
                return {};
            const QPointF direction = (from - tip) / line.length();
            const QPointF normal(-direction.y(), direction.x());
            const QPointF middle = tip + direction * (MarkerLength / 2.0);
            return QPolygonF({tip, middle + normal * (MarkerHalfWidth - 1), tip + direction * MarkerLength,
                              middle - normal * (MarkerHalfWidth - 1)});
        }
    }

    EdgeLayerItem::EdgeLayerItem(QQuickItem *parent) :
            QQuickPaintedItem(parent) {
        setAntialiasing(true);
        setAcceptedMouseButtons(Qt::NoButton);
        setAcceptHoverEvents(false);
    }

    void EdgeLayerItem::connectModel(QObject *model) {
        if (!model)
            return;
        const auto repaint = [this] { update(); };
        auto *itemModel = qobject_cast<QAbstractItemModel *>(model);
        connect(itemModel, &QAbstractItemModel::dataChanged, this, repaint);
        connect(itemModel, &QAbstractItemModel::modelReset, this, repaint);
        connect(itemModel, &QAbstractItemModel::rowsInserted, this, repaint);
        connect(itemModel, &QAbstractItemModel::rowsRemoved, this, repaint);
    }

    void EdgeLayerItem::setNodes(DiagramNodeModel *nodes) {
        if (Nodes == nodes)
            return;
        if (Nodes)
            disconnect(Nodes, nullptr, this, nullptr);
        Nodes = nodes;
        connectModel(nodes);
        emit nodesChanged();
        update();
    }

    void EdgeLayerItem::setEdges(DiagramEdgeModel *edges) {
        if (Edges == edges)
            return;
        if (Edges)
            disconnect(Edges, nullptr, this, nullptr);
        Edges = edges;
        connectModel(edges);
        emit edgesChanged();
        update();
    }

    void EdgeLayerItem::setOriginX(qreal value) {
        if (qFuzzyCompare(OriginX, value))
            return;
        OriginX = value;
        emit viewChanged();
        update();
    }

    void EdgeLayerItem::setOriginY(qreal value) {
        if (qFuzzyCompare(OriginY, value))
            return;
        OriginY = value;
        emit viewChanged();
        update();
    }

    void EdgeLayerItem::setZoom(qreal value) {
        if (value <= 0 || qFuzzyCompare(Zoom, value))
            return;
        Zoom = value;
        emit viewChanged();
        update();
    }

    void EdgeLayerItem::setLineColor(const QColor &color) {
        if (LineColor == color)
            return;
        LineColor = color;
        emit colorsChanged();
        update();
    }

    void EdgeLayerItem::setSelectionColor(const QColor &color) {
        if (SelectionColor == color)
            return;
        SelectionColor = color;
        emit colorsChanged();
        update();
    }

    void EdgeLayerItem::setBackgroundColor(const QColor &color) {
        if (BackgroundColor == color)
            return;
        BackgroundColor = color;
        emit colorsChanged();
        update();
    }

    void EdgeLayerItem::setRubberBandVisible(bool visible) {
        if (RubberBandVisible == visible)
            return;
        RubberBandVisible = visible;
        emit rubberBandChanged();
        update();
    }

    void EdgeLayerItem::setRubberStart(const QPointF &point) {
        if (RubberStart == point)
            return;
        RubberStart = point;
        emit rubberBandChanged();
        if (RubberBandVisible)
            update();
    }

    void EdgeLayerItem::setRubberEnd(const QPointF &point) {
        if (RubberEnd == point)
            return;
        RubberEnd = point;
        emit rubberBandChanged();
        if (RubberBandVisible)
            update();
    }

    void EdgeLayerItem::setPinMode(bool enabled) {
        if (PinMode == enabled)
            return;
        PinMode = enabled;
        emit pinModeChanged();
        update();
    }

    void EdgeLayerItem::paintPinEdges(QPainter *painter) {
        for (const auto &edge: Edges->edges()) {
            const auto *source = Nodes->find(edge.Source);
            const auto *target = Nodes->find(edge.Target);
            if (!source || !target)
                continue;
            const QPointF start = DiagramGeometry::pinPoint(source->Rect, true, edge.SourcePin);
            const QPointF end = DiagramGeometry::pinPoint(target->Rect, false, edge.TargetPin);
            const auto points = DiagramGeometry::pinCurve(start, end);
            const QColor color = edge.Selected ? SelectionColor : LineColor;
            QPen pen(color, edge.Selected ? 2.2 : 1.4);
            if (edge.Kind == DiagramTypes::Reference) {
                pen.setStyle(Qt::DashLine);
                pen.setWidthF(edge.Selected ? 2.0 : 1.1);
            }
            painter->setPen(pen);
            painter->drawPolyline(points.data(), int(points.size()));
            painter->setPen(QPen(color, pen.widthF()));
            if (edge.Kind == DiagramTypes::ValueFlow || edge.Kind == DiagramTypes::Reference) {
                const QPolygonF marker = arrowHead(end, points[points.size() - 3]);
                if (!marker.isEmpty()) {
                    painter->setBrush(QBrush(color));
                    painter->drawPolygon(marker);
                    painter->setBrush(Qt::NoBrush);
                }
            } else if (edge.Kind == DiagramTypes::Binding) {
                // "=" on a small disc in the middle of the line
                const QPointF middle = points[points.size() / 2];
                painter->setBrush(QBrush(BackgroundColor));
                painter->drawEllipse(middle, 7.0, 7.0);
                painter->setBrush(Qt::NoBrush);
                painter->drawLine(middle + QPointF(-3.5, -2), middle + QPointF(3.5, -2));
                painter->drawLine(middle + QPointF(-3.5, 2), middle + QPointF(3.5, 2));
            }
        }
    }

    void EdgeLayerItem::setShowGrid(bool show) {
        if (ShowGrid == show)
            return;
        ShowGrid = show;
        emit gridChanged();
        update();
    }

    void EdgeLayerItem::setGridSize(qreal size) {
        if (size < 4 || qFuzzyCompare(GridSize, size))
            return;
        GridSize = size;
        emit gridChanged();
        update();
    }

    void EdgeLayerItem::setGridColor(const QColor &color) {
        if (GridColor == color)
            return;
        GridColor = color;
        emit gridChanged();
        update();
    }

    void EdgeLayerItem::paintGrid(QPainter *painter) {
        // Visible canvas area; dots only where they can be seen.
        const QRectF visible(OriginX, OriginY, width() / Zoom, height() / Zoom);
        const qreal left = std::floor(visible.left() / GridSize) * GridSize;
        const qreal top = std::floor(visible.top() / GridSize) * GridSize;
        QVector<QPointF> points;
        for (qreal x = left; x <= visible.right(); x += GridSize)
            for (qreal y = top; y <= visible.bottom(); y += GridSize)
                points.append(QPointF(x, y));
        painter->setPen(QPen(GridColor, 1.5 / Zoom, Qt::SolidLine, Qt::RoundCap));
        painter->drawPoints(points.data(), int(points.size()));
    }

    void EdgeLayerItem::paint(QPainter *painter) {
        painter->setRenderHint(QPainter::Antialiasing);
        painter->translate(-OriginX * Zoom, -OriginY * Zoom);
        painter->scale(Zoom, Zoom);

        if (ShowGrid)
            paintGrid(painter);

        if (Nodes && Edges && PinMode)
            paintPinEdges(painter);
        if (Nodes && Edges && !PinMode) {
            for (const auto &edge: Edges->edges()) {
                const auto *source = Nodes->find(edge.Source);
                const auto *target = Nodes->find(edge.Target);
                if (!source || !target)
                    continue;

                const QLineF line = DiagramGeometry::edgeLine(source->Rect, target->Rect);
                const QPointF start = line.p1();
                const QPointF end = line.p2();
                const QColor color = edge.Selected ? SelectionColor : LineColor;
                painter->setPen(QPen(color, edge.Selected ? 2.2 : 1.4));

                QPointF lineStart = start;
                QPointF lineEnd = end;
                QPolygonF marker;
                bool filled = true;
                switch (edge.Kind) {
                    case DiagramTypes::Composition:
                        marker = diamond(start, end); // the diamond sits at the owner
                        if (!marker.isEmpty())
                            lineStart = marker[2];
                        break;
                    case DiagramTypes::Flow:
                        marker = arrowHead(end, start);
                        break;
                    case DiagramTypes::Specialization:
                        marker = arrowHead(end, start);
                        filled = false;
                        if (!marker.isEmpty())
                            lineEnd = (marker[1] + marker[2]) / 2.0;
                        break;
                    case DiagramTypes::Connection:
                    case DiagramTypes::Binding:
                    case DiagramTypes::ValueFlow:
                    case DiagramTypes::Reference:
                        break;
                }

                painter->drawLine(lineStart, lineEnd);
                if (!marker.isEmpty()) {
                    painter->setBrush(filled ? QBrush(color) : QBrush(BackgroundColor));
                    painter->drawPolygon(marker);
                    painter->setBrush(Qt::NoBrush);
                }
            }
        }

        if (RubberBandVisible) {
            painter->setPen(QPen(SelectionColor, 1.5, Qt::DashLine));
            painter->drawLine(RubberStart, RubberEnd);
        }
    }
}
