//
// Draws all edges of a diagram.
//

#include "EdgeLayerItem.h"
#include "DiagramEdgeModel.h"
#include "DiagramGeometry.h"
#include "DiagramNodeModel.h"
#include "EdgeRouting.h"

#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>
#include <QPolygonF>
#include <QVector>

#include <algorithm>
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

    void EdgeLayerItem::setRouting(bool enabled) {
        if (Routing == enabled)
            return;
        Routing = enabled;
        emit routingChanged();
        update();
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
            std::vector<QRectF> placedLabels;
            for (const auto &edge: Edges->edges()) {
                const auto *source = Nodes->find(edge.Source);
                const auto *target = Nodes->find(edge.Target);
                if (!source || !target)
                    continue;

                const QColor loopColor = edge.Selected ? SelectionColor : LineColor;
                if (edge.Source == edge.Target) {
                    // transition of a state to itself: a loop at the upper right corner
                    painter->setPen(QPen(loopColor, edge.Selected ? 2.2 : 1.4));
                    const QRectF r = source->Rect;
                    const QRectF loop(r.right() - 26, r.top() - 18, 36, 36);
                    painter->drawArc(loop, 180 * 16, -270 * 16);
                    const QPointF tip(r.right(), r.top() + 10);
                    painter->setBrush(QBrush(loopColor));
                    painter->drawPolygon(arrowHead(tip, tip + QPointF(10, 0)));
                    painter->setBrush(Qt::NoBrush);
                    if (!edge.Label.isEmpty())
                        painter->drawText(QPointF(r.right() + 14, r.top() - 6), edge.Label);
                    continue;
                }

                // use cases are ellipses: the line ends at their outline
                // and an actor at its stick figure (above its name)
                const auto shape = [](const DiagramNode &node) {
                    return node.Kind == DiagramTypes::ActorNode
                           ? QRectF(node.Rect.center().x() - 20, node.Rect.top(), 40, 62) : node.Rect;
                };
                const auto outline = [&](const DiagramNode &node, const QPointF &towards) {
                    return node.Kind == DiagramTypes::UseCaseNode ? DiagramGeometry::ellipsePointTowards(node.Rect, towards)
                                                                  : DiagramGeometry::borderPointTowards(shape(node), towards);
                };
                // tidy lines (structure and behavior views): around the nodes in between, side by side between
                // the same two nodes; otherwise straight (use cases: from outline to outline)
                std::vector<QPointF> path;
                const bool roundShapes = source->Kind == DiagramTypes::UseCaseNode || source->Kind == DiagramTypes::ActorNode ||
                                         target->Kind == DiagramTypes::UseCaseNode || target->Kind == DiagramTypes::ActorNode;
                if (Routing && !roundShapes) {
                    path = EdgeRouting::routeOf(*Nodes, Edges->edges(), edge);
                } else {
                    QLineF line(outline(*source, shape(*target).center()), outline(*target, shape(*source).center()));
                    const qreal offset = EdgeRouting::parallelOffset(Edges->edges(), edge);
                    if (std::abs(offset) > 0 && line.length() > 1e-6) {
                        QPointF normal(-(line.dy()), line.dx());
                        normal /= line.length();
                        line.translate(normal * offset);
                    }
                    path = {line.p1(), line.p2()};
                }
                if (path.size() < 2)
                    continue;
                const bool parallel = std::abs(EdgeRouting::parallelOffset(Edges->edges(), edge)) > 0;
                const QPointF start = path.front();
                const QPointF end = path.back();
                const QPointF afterStart = path[1];
                const QPointF beforeEnd = path[path.size() - 2];
                const QColor color = edge.Selected ? SelectionColor : LineColor;
                painter->setPen(QPen(color, edge.Selected ? 2.2 : 1.4));

                QPointF lineStart = start;
                QPointF lineEnd = end;
                QPolygonF marker;
                bool filled = true;
                bool openArrow = false; // «satisfy», «verify», «include», dependency: dashed with an open arrow head
                bool openHead = false;  // message, «allocate»: a solid line with an open arrow head
                switch (edge.Kind) {
                    case DiagramTypes::Composition:
                        marker = diamond(start, afterStart); // the diamond sits at the owner
                        if (!marker.isEmpty())
                            lineStart = marker[2];
                        break;
                    case DiagramTypes::Flow:
                    case DiagramTypes::Succession:
                    case DiagramTypes::Transition:
                        marker = arrowHead(end, beforeEnd);
                        break;
                    case DiagramTypes::Specialization:
                        marker = arrowHead(end, beforeEnd);
                        filled = false;
                        if (!marker.isEmpty())
                            lineEnd = (marker[1] + marker[2]) / 2.0;
                        break;
                    case DiagramTypes::Satisfy:
                    case DiagramTypes::Verify:
                    case DiagramTypes::Include:
                    case DiagramTypes::Frame:
                    case DiagramTypes::Dependency:
                        openArrow = true;
                        break;
                    case DiagramTypes::Message:
                    case DiagramTypes::Allocation:
                        openHead = true;
                        break;
                    case DiagramTypes::Temporal:
                        break;
                    case DiagramTypes::Connection:
                    case DiagramTypes::Binding:
                    case DiagramTypes::ValueFlow:
                    case DiagramTypes::Reference:
                    case DiagramTypes::Interface:
                    case DiagramTypes::Association:
                        break;
                }

                std::vector<QPointF> drawn = path;
                drawn.front() = lineStart;
                drawn.back() = lineEnd;
                if (openArrow) {
                    QPen dashed(color, edge.Selected ? 2.2 : 1.4, Qt::DashLine);
                    painter->setPen(dashed);
                    painter->drawPolyline(drawn.data(), int(drawn.size()));
                    painter->setPen(QPen(color, edge.Selected ? 2.2 : 1.4));
                    const QPolygonF head = arrowHead(end, beforeEnd);
                    if (!head.isEmpty()) {
                        painter->drawLine(head[0], head[1]);
                        painter->drawLine(head[0], head[2]);
                    }
                } else {
                    painter->drawPolyline(drawn.data(), int(drawn.size()));
                    const QPolygonF head = openHead ? arrowHead(end, beforeEnd) : QPolygonF();
                    if (!head.isEmpty()) {
                        painter->drawLine(head[0], head[1]);
                        painter->drawLine(head[0], head[2]);
                    }
                }
                // the longest segment carries the label (and the ball and socket of an interface)
                std::size_t longest = 1;
                for (std::size_t i = 1; i < drawn.size(); ++i) {
                    if (QLineF(drawn[i - 1], drawn[i]).length() > QLineF(drawn[longest - 1], drawn[longest]).length())
                        longest = i;
                }
                const QPointF segmentStart = drawn[longest - 1];
                const QPointF segmentEnd = drawn[longest];
                if (edge.Kind == DiagramTypes::Temporal) {
                    // temporal decomposition: a filled half disc on the owner, its flat side towards the slice
                    const QLineF line(start, afterStart);
                    const qreal angle = line.angle();
                    const QRectF disc(start.x() - 7, start.y() - 7, 14, 14);
                    QPainterPath halfDisc;
                    halfDisc.moveTo(start);
                    halfDisc.arcTo(disc, angle + 90, 180);
                    halfDisc.closeSubpath();
                    painter->setBrush(QBrush(color));
                    painter->drawPath(halfDisc.translated(QPointF(std::cos(qDegreesToRadians(angle)),
                                                                  -std::sin(qDegreesToRadians(angle))) * 7));
                    painter->setBrush(Qt::NoBrush);
                }
                if (edge.Kind == DiagramTypes::Interface) {
                    // ball and socket in the middle
                    const QPointF middle = (segmentStart + segmentEnd) / 2.0;
                    const QLineF line(segmentStart, segmentEnd);
                    const qreal angle = line.angle();
                    painter->setBrush(QBrush(BackgroundColor));
                    painter->drawEllipse(middle, 4.5, 4.5);
                    painter->setBrush(Qt::NoBrush);
                    const QRectF socket(middle.x() - 8, middle.y() - 8, 16, 16);
                    painter->drawArc(socket, int((angle + 90) * 16), 180 * 16);
                }
                if (!edge.Label.isEmpty()) {
                    QFont font = painter->font();
                    font.setPixelSize(11);
                    painter->setFont(font);
                    const QPointF middle = (segmentStart + segmentEnd) / 2.0;
                    const QRectF box = painter->fontMetrics().boundingRect(edge.Label).adjusted(-4, -2, 4, 2);
                    // beside a steep line, above a flat one; on the side the line was moved to
                    const bool steep = std::abs(segmentEnd.y() - segmentStart.y()) > std::abs(segmentEnd.x() - segmentStart.x());
                    const qreal side = (parallel && edge.Source > edge.Target) ? -1.0 : 1.0;
                    QRectF placed = steep ? QRectF(side > 0 ? middle.x() + 8 : middle.x() - 8 - box.width(),
                                                   middle.y() - box.height() / 2, box.width(), box.height())
                                          : QRectF(middle.x() - box.width() / 2,
                                                   side > 0 ? middle.y() - box.height() - 8 : middle.y() + 8,
                                                   box.width(), box.height());
                    // not on top of another label: moved along the line until it is free
                    for (int attempt = 1; attempt < 8; ++attempt) {
                        const bool taken = std::any_of(placedLabels.begin(), placedLabels.end(),
                                                       [&](const QRectF &other) { return other.intersects(placed); });
                        if (!taken)
                            break;
                        const qreal step = (attempt % 2 ? 1.0 : -1.0) * ((attempt + 1) / 2) * (steep ? box.height() + 2 : box.width() / 2 + 6);
                        placed.translate(steep ? QPointF(0, step) : QPointF(step, 0));
                    }
                    placedLabels.push_back(placed);
                    painter->setPen(Qt::NoPen);
                    painter->setBrush(QBrush(BackgroundColor));
                    painter->drawRect(placed);
                    painter->setBrush(Qt::NoBrush);
                    painter->setPen(QPen(color));
                    painter->drawText(placed, Qt::AlignCenter, edge.Label);
                    painter->setPen(QPen(color, edge.Selected ? 2.2 : 1.4));
                }
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
