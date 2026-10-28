//
// Tidy lines between the nodes of a diagram: a straight line where it passes no other node, otherwise the
// orthogonal route (one or two bends) that passes the fewest nodes; several lines between the same two nodes
// side by side. Used for drawing (EdgeLayerItem) and for hit testing (the controllers), so both agree.
//

#ifndef STRUCTURASYSTEMS_EDGEROUTING_H
#define STRUCTURASYSTEMS_EDGEROUTING_H

#include <QLineF>
#include <QPointF>
#include <QRectF>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "DiagramEdgeModel.h"
#include "DiagramGeometry.h"
#include "DiagramNodeModel.h"

namespace StructuraSystems::Client::EdgeRouting {

    /// true if the segment passes through the inside of rect (touching its border does not count).
    inline bool crosses(const QLineF &segment, const QRectF &rect) {
        const QRectF inside = rect.adjusted(3, 3, -3, -3);
        if (inside.width() <= 0 || inside.height() <= 0)
            return false;
        // Liang-Barsky clipping
        qreal t0 = 0.0;
        qreal t1 = 1.0;
        const qreal dx = segment.dx();
        const qreal dy = segment.dy();
        const qreal p[4] = {-dx, dx, -dy, dy};
        const qreal q[4] = {segment.x1() - inside.left(), inside.right() - segment.x1(), segment.y1() - inside.top(),
                            inside.bottom() - segment.y1()};
        for (int i = 0; i < 4; ++i) {
            if (std::abs(p[i]) < 1e-9) {
                if (q[i] < 0)
                    return false;
                continue;
            }
            const qreal t = q[i] / p[i];
            if (p[i] < 0)
                t0 = std::max(t0, t);
            else
                t1 = std::min(t1, t);
            if (t0 > t1)
                return false;
        }
        return true;
    }

    inline int crossings(const std::vector<QPointF> &path, const std::vector<QRectF> &obstacles) {
        int count = 0;
        for (const QRectF &obstacle: obstacles) {
            for (std::size_t i = 1; i < path.size(); ++i) {
                if (crosses(QLineF(path[i - 1], path[i]), obstacle)) {
                    ++count;
                    break;
                }
            }
        }
        return count;
    }

    inline qreal length(const std::vector<QPointF> &path) {
        qreal result = 0;
        for (std::size_t i = 1; i < path.size(); ++i)
            result += QLineF(path[i - 1], path[i]).length();
        return result;
    }

    /// Route from source to target; offset moves it sideways (several lines between the same nodes).
    inline std::vector<QPointF> route(const QRectF &source, const QRectF &target, const std::vector<QRectF> &obstacles,
                                      qreal offset = 0) {
        QLineF straight = DiagramGeometry::edgeLine(source, target);
        if (std::abs(offset) > 0 && straight.length() > 1e-6) {
            QPointF normal(-straight.dy(), straight.dx());
            normal /= straight.length();
            straight.translate(normal * offset);
        }
        std::vector<QPointF> best{straight.p1(), straight.p2()};
        const int straightCrossings = crossings(best, obstacles);
        if (straightCrossings == 0)
            return best;

        // the same shift for both directions of a pair (the offset is measured along the line)
        const bool forward = source.center().x() < target.center().x() ||
                             (source.center().x() == target.center().x() && source.center().y() < target.center().y());
        offset = forward ? offset : -offset;
        const QPointF s = source.center() + QPointF(offset, offset);
        const QPointF t = target.center() + QPointF(offset, offset);
        const auto clampX = [](const QRectF &rect, qreal x) { return std::clamp(x, rect.left() + 4, rect.right() - 4); };
        const auto clampY = [](const QRectF &rect, qreal y) { return std::clamp(y, rect.top() + 4, rect.bottom() - 4); };
        const auto sideX = [](const QRectF &rect, qreal towards) { return towards >= rect.center().x() ? rect.right() : rect.left(); };
        const auto sideY = [](const QRectF &rect, qreal towards) { return towards >= rect.center().y() ? rect.bottom() : rect.top(); };
        std::vector<std::vector<QPointF>> candidates;
        // one bend: horizontal then vertical, vertical then horizontal
        if (t.x() < source.left() || t.x() > source.right())
            candidates.push_back({{sideX(source, t.x()), clampY(source, s.y())}, {t.x(), clampY(source, s.y())},
                                  {t.x(), sideY(target, s.y())}});
        if (t.y() < source.top() || t.y() > source.bottom())
            candidates.push_back({{clampX(source, s.x()), sideY(source, t.y())}, {clampX(source, s.x()), t.y()},
                                  {sideX(target, s.x()), t.y()}});
        // two bends: through a vertical or horizontal channel between (or beside) the two nodes
        std::vector<qreal> channelsX;
        std::vector<qreal> channelsY;
        if (source.right() < target.left())
            channelsX.push_back((source.right() + target.left()) / 2);
        else if (target.right() < source.left())
            channelsX.push_back((target.right() + source.left()) / 2);
        channelsX.push_back(std::min(source.left(), target.left()) - 24);
        channelsX.push_back(std::max(source.right(), target.right()) + 24);
        if (source.bottom() < target.top())
            channelsY.push_back((source.bottom() + target.top()) / 2);
        else if (target.bottom() < source.top())
            channelsY.push_back((target.bottom() + source.top()) / 2);
        channelsY.push_back(std::min(source.top(), target.top()) - 24);
        channelsY.push_back(std::max(source.bottom(), target.bottom()) + 24);
        for (const qreal base: std::vector<qreal>(channelsX)) {
            for (const qreal shift: {0.0, -40.0, 40.0}) {
                const qreal x = base + shift + offset;
                const qreal ys = clampY(source, s.y());
                const qreal yt = clampY(target, t.y());
                candidates.push_back({{sideX(source, x), ys}, {x, ys}, {x, yt}, {sideX(target, x), yt}});
            }
        }
        for (const qreal base: std::vector<qreal>(channelsY)) {
            for (const qreal shift: {0.0, -40.0, 40.0}) {
                const qreal y = base + shift + offset;
                const qreal xs = clampX(source, s.x());
                const qreal xt = clampX(target, t.x());
                candidates.push_back({{xs, sideY(source, y)}, {xs, y}, {xt, y}, {xt, sideY(target, y)}});
            }
        }
        int bestCrossings = straightCrossings;
        qreal bestCost = std::numeric_limits<qreal>::max();
        for (auto &candidate: candidates) {
            // the route must leave and enter its nodes, never run through them
            if (crossings(candidate, {source, target}) > 0)
                continue;
            const int count = crossings(candidate, obstacles);
            const qreal cost = length(candidate) + 60.0 * qreal(candidate.size() - 2);
            if (count < bestCrossings || (count == bestCrossings && count < straightCrossings && cost < bestCost)) {
                bestCrossings = count;
                bestCost = cost;
                best = candidate;
            }
        }
        return best;
    }

    /// The nodes a line between source and target should not pass: all others but frames, boundaries and the
    /// nodes that contain source or target (a part around them).
    inline std::vector<QRectF> obstaclesFor(const DiagramNodeModel &nodes, const DiagramNode &source,
                                            const DiagramNode &target) {
        std::vector<QRectF> obstacles;
        for (const auto &node: nodes.nodes()) {
            if (node.Id == source.Id || node.Id == target.Id || node.Kind == DiagramTypes::PackageNode ||
                node.Kind == DiagramTypes::BoundaryNode || node.Kind == DiagramTypes::PortNode ||
                node.Rect.contains(source.Rect) || node.Rect.contains(target.Rect) || node.Rect.isEmpty())
                continue;
            obstacles.push_back(node.Rect);
        }
        return obstacles;
    }

    /// Sideways offset of an edge among the edges between the same two nodes (0 for a single one).
    inline qreal parallelOffset(const std::vector<DiagramEdge> &edges, const DiagramEdge &edge) {
        int count = 0;
        int index = 0;
        for (const auto &other: edges) {
            const bool same = (other.Source == edge.Source && other.Target == edge.Target) ||
                              (other.Source == edge.Target && other.Target == edge.Source);
            if (!same)
                continue;
            if (&other == &edge || other.Key == edge.Key)
                index = count;
            ++count;
        }
        if (count < 2)
            return 0;
        // the same side for both directions: measured along the line from the smaller to the larger id
        const qreal offset = (qreal(index) - qreal(count - 1) / 2.0) * 18.0;
        return edge.Source < edge.Target ? offset : -offset;
    }

    /// The route of an edge of the model (empty if one of its nodes is missing).
    inline std::vector<QPointF> routeOf(const DiagramNodeModel &nodes, const std::vector<DiagramEdge> &edges,
                                        const DiagramEdge &edge) {
        const auto *source = nodes.find(edge.Source);
        const auto *target = nodes.find(edge.Target);
        if (!source || !target)
            return {};
        return route(source->Rect, target->Rect, obstaclesFor(nodes, *source, *target), parallelOffset(edges, edge));
    }

    inline qreal distanceToRoute(const QPointF &point, const std::vector<QPointF> &path) {
        qreal best = std::numeric_limits<qreal>::max();
        for (std::size_t i = 1; i < path.size(); ++i)
            best = std::min(best, DiagramGeometry::distanceToSegment(point, QLineF(path[i - 1], path[i])));
        return best;
    }
}

#endif //STRUCTURASYSTEMS_EDGEROUTING_H
