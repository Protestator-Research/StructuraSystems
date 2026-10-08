//
// Small geometry helpers shared by the controller (hit testing) and the edge layer (drawing).
//

#ifndef STRUCTURASYSTEMS_DIAGRAMGEOMETRY_H
#define STRUCTURASYSTEMS_DIAGRAMGEOMETRY_H

#include <QLineF>
#include <QPointF>
#include <QRectF>

#include <algorithm>
#include <cmath>
#include <vector>

namespace StructuraSystems::Client::DiagramGeometry {

    /// Point on the outline of rect on the way from its centre towards target.
    inline QPointF borderPointTowards(const QRectF &rect, const QPointF &target) {
        const QPointF center = rect.center();
        const qreal dx = target.x() - center.x();
        const qreal dy = target.y() - center.y();
        if (std::abs(dx) < 1e-6 && std::abs(dy) < 1e-6)
            return center;
        const qreal scaleX = std::abs(dx) > 1e-6 ? (rect.width() / 2.0) / std::abs(dx) : 1e9;
        const qreal scaleY = std::abs(dy) > 1e-6 ? (rect.height() / 2.0) / std::abs(dy) : 1e9;
        const qreal scale = std::min(scaleX, scaleY);
        return {center.x() + dx * scale, center.y() + dy * scale};
    }

    /// Straight edge between two node outlines (from the border of source to the border of target).
    inline QLineF edgeLine(const QRectF &source, const QRectF &target) {
        return {borderPointTowards(source, target.center()), borderPointTowards(target, source.center())};
    }

    // --- parametric view: nodes with pins ---------------------------------------------------------------
    constexpr qreal PinTop = 46.0;     ///< below keyword and title
    constexpr qreal PinRowHeight = 22.0;

    /// Point of an output pin (right border) or input pin (left border); pin -1 = middle of that border.
    inline QPointF pinPoint(const QRectF &rect, bool output, int pin) {
        const qreal y = pin < 0 ? std::min(rect.center().y(), rect.top() + PinTop / 2.0 + 4.0)
                                : rect.top() + PinTop + PinRowHeight * (pin + 0.5);
        return {output ? rect.right() : rect.left(), y};
    }

    /// Line of the parametric view between two pins, as polyline (a smooth S-curve).
    inline std::vector<QPointF> pinCurve(const QPointF &start, const QPointF &end, int segments = 16) {
        const qreal pull = std::max<qreal>(40.0, std::abs(end.x() - start.x()) / 2.0);
        const QPointF c1(start.x() + pull, start.y());
        const QPointF c2(end.x() - pull, end.y());
        std::vector<QPointF> points;
        points.reserve(std::size_t(segments) + 1);
        for (int i = 0; i <= segments; ++i) {
            const qreal t = qreal(i) / segments;
            const qreal u = 1.0 - t;
            points.push_back(start * (u * u * u) + c1 * (3 * u * u * t) + c2 * (3 * u * t * t) + end * (t * t * t));
        }
        return points;
    }

    inline qreal distanceToSegment(const QPointF &point, const QLineF &segment) {
        const QPointF d = segment.p2() - segment.p1();
        const qreal lengthSquared = d.x() * d.x() + d.y() * d.y();
        if (lengthSquared < 1e-9)
            return QLineF(point, segment.p1()).length();
        const QPointF v = point - segment.p1();
        const qreal t = std::clamp((v.x() * d.x() + v.y() * d.y()) / lengthSquared, 0.0, 1.0);
        return QLineF(point, segment.p1() + d * t).length();
    }
}

#endif //STRUCTURASYSTEMS_DIAGRAMGEOMETRY_H
