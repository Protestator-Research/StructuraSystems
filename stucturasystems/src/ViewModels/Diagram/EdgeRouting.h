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
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <queue>
#include <tuple>
#include <unordered_map>
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

    /// An orthogonal route from the border of source to the border of target that passes none of the obstacles
    /// (each kept at a distance): the shortest with the fewest bends on the grid of the obstacle borders. Empty if
    /// there is none. The result of the same input is kept (the edge layer asks for every route on each repaint).
    inline std::vector<QPointF> gridRoute(const QRectF &source, const QRectF &target, const std::vector<QRectF> &obstacles) {
        constexpr qreal Margin = 12.0;
        // cache
        std::size_t key = 0;
        const auto mix = [&](qreal value) {
            key ^= std::hash<long long>()(std::llround(value * 4.0)) + 0x9e3779b97f4a7c15ULL + (key << 6) + (key >> 2);
        };
        for (const QRectF &r: {source, target}) {
            mix(r.x()); mix(r.y()); mix(r.width()); mix(r.height());
        }
        for (const QRectF &r: obstacles) {
            mix(r.x()); mix(r.y()); mix(r.width()); mix(r.height());
        }
        static std::unordered_map<std::size_t, std::vector<QPointF>> cache;
        if (const auto found = cache.find(key); found != cache.end())
            return found->second;
        if (cache.size() > 4000)
            cache.clear();

        std::vector<QRectF> blocked;
        for (const QRectF &r: obstacles)
            blocked.push_back(r.adjusted(-Margin, -Margin, Margin, Margin));
        const QRectF s = source.adjusted(-Margin, -Margin, Margin, Margin);
        const QRectF t = target.adjusted(-Margin, -Margin, Margin, Margin);
        // the lines of the grid: the borders of everything (kept at a distance) and the middles of the two ends
        std::vector<qreal> xs{s.left(), s.right(), t.left(), t.right(), source.center().x(), target.center().x()};
        std::vector<qreal> ys{s.top(), s.bottom(), t.top(), t.bottom(), source.center().y(), target.center().y()};
        for (const QRectF &r: blocked) {
            xs.push_back(r.left());
            xs.push_back(r.right());
            ys.push_back(r.top());
            ys.push_back(r.bottom());
        }
        const auto normalize = [](std::vector<qreal> &values) {
            std::sort(values.begin(), values.end());
            std::vector<qreal> result;
            for (const qreal value: values) {
                if (result.empty() || value - result.back() > 0.5)
                    result.push_back(value);
            }
            // between two neighbouring lines a line in the middle (a channel between two nodes)
            std::vector<qreal> withMiddles;
            for (std::size_t i = 0; i < result.size(); ++i) {
                withMiddles.push_back(result[i]);
                if (i + 1 < result.size())
                    withMiddles.push_back((result[i] + result[i + 1]) / 2);
            }
            values = withMiddles;
        };
        normalize(xs);
        normalize(ys);
        const int nx = int(xs.size());
        const int ny = int(ys.size());
        if (nx * ny > 250000)
            return {};
        // the ends themselves (their borders are no lines of the grid)
        const auto insideEnds = [&](const QPointF &p) {
            const auto strictly = [&](const QRectF &r) {
                return p.x() > r.left() + 0.1 && p.x() < r.right() - 0.1 && p.y() > r.top() + 0.1 && p.y() < r.bottom() - 0.1;
            };
            return strictly(source) || strictly(target);
        };
        // what the obstacles cover, marked by the ranges of grid lines in them: points, and the steps to the right
        // (horizontal, from i to i + 1) and downwards (vertical, from j to j + 1)
        std::vector<char> free(std::size_t(nx * ny), 1);
        std::vector<char> rightFree(std::size_t(nx * ny), 1);
        std::vector<char> downFree(std::size_t(nx * ny), 1);
        const auto firstAbove = [](const std::vector<qreal> &lines, qreal value) {
            return int(std::upper_bound(lines.begin(), lines.end(), value + 0.1) - lines.begin());
        };
        const auto firstFrom = [](const std::vector<qreal> &lines, qreal value) {
            return int(std::lower_bound(lines.begin(), lines.end(), value - 0.1) - lines.begin());
        };
        for (const QRectF &r: blocked) {
            const int i0 = firstAbove(xs, r.left());       // strictly inside
            const int i1 = firstFrom(xs, r.right());       // first on / after the right border
            const int j0 = firstAbove(ys, r.top());
            const int j1 = firstFrom(ys, r.bottom());
            const int il = firstFrom(xs, r.left());        // on / after the left border
            const int jt = firstFrom(ys, r.top());
            for (int i = il; i < nx && i <= i1; ++i) {
                for (int j = jt; j < ny && j <= j1; ++j) {
                    const std::size_t at = std::size_t(i * ny + j);
                    const bool innerX = i >= i0 && i < i1;
                    const bool innerY = j >= j0 && j < j1;
                    if (innerX && innerY)
                        free[at] = 0;
                    if (innerY && i < i1)
                        rightFree[at] = 0; // from xs[i] (>= left) to xs[i + 1] (<= right) at a height inside
                    if (innerX && j < j1)
                        downFree[at] = 0;
                }
            }
        }
        for (int i = 0; i < nx; ++i)
            for (int j = 0; j < ny; ++j)
                if (insideEnds(QPointF(xs[std::size_t(i)], ys[std::size_t(j)])))
                    free[std::size_t(i * ny + j)] = 0;
        // starts / goals: the points of the grid on the four sides of the ends, at the middle of each side
        const auto exits = [&](const QRectF &r, const QRectF &around) {
            std::vector<std::pair<QPointF, QPointF>> result; // border point, grid point outside
            result.push_back({QPointF(r.center().x(), r.top()), QPointF(r.center().x(), around.top())});
            result.push_back({QPointF(r.center().x(), r.bottom()), QPointF(r.center().x(), around.bottom())});
            result.push_back({QPointF(r.left(), r.center().y()), QPointF(around.left(), r.center().y())});
            result.push_back({QPointF(r.right(), r.center().y()), QPointF(around.right(), r.center().y())});
            return result;
        };
        const auto indexOf = [&](const QPointF &p) -> int {
            const auto ix = std::lower_bound(xs.begin(), xs.end(), p.x() - 0.25);
            const auto iy = std::lower_bound(ys.begin(), ys.end(), p.y() - 0.25);
            if (ix == xs.end() || iy == ys.end() || std::abs(*ix - p.x()) > 0.5 || std::abs(*iy - p.y()) > 0.5)
                return -1;
            return int(ix - xs.begin()) * ny + int(iy - ys.begin());
        };
        const auto starts = exits(source, s);
        const auto goals = exits(target, t);
        // Dijkstra over (grid point, direction); a bend costs as much as 40 px of line
        constexpr qreal Bend = 40.0;
        const int states = nx * ny * 4;
        std::vector<qreal> cost(std::size_t(states), std::numeric_limits<qreal>::max());
        std::vector<int> previous(std::size_t(states), -1);
        using Item = std::pair<qreal, int>;
        std::priority_queue<Item, std::vector<Item>, std::greater<>> open;
        for (const auto &[border, outside]: starts) {
            const int index = indexOf(outside);
            if (index < 0 || !free[std::size_t(index)])
                continue;
            // the direction of the first step (out of the border)
            const int direction = outside.y() < border.y() - 0.1 ? 0 : outside.y() > border.y() + 0.1 ? 1
                                  : outside.x() < border.x() ? 2 : 3;
            const int state = index * 4 + direction;
            const qreal c = QLineF(border, outside).length();
            if (c < cost[std::size_t(state)]) {
                cost[std::size_t(state)] = c;
                open.push({c, state});
            }
        }
        std::map<int, std::pair<QPointF, int>> goalOf; // grid index -> border point, direction into the target
        for (const auto &[border, outside]: goals) {
            const int index = indexOf(outside);
            if (index < 0 || !free[std::size_t(index)])
                continue;
            const int direction = border.y() < outside.y() - 0.1 ? 0 : border.y() > outside.y() + 0.1 ? 1
                                  : border.x() < outside.x() ? 2 : 3;
            goalOf[index] = {border, direction};
        }
        if (goalOf.empty() || open.empty()) {
            cache[key] = {};
            return {};
        }
        const int dx[4] = {0, 0, -1, 1};
        const int dy[4] = {-1, 1, 0, 0};
        int reached = -1;
        qreal reachedCost = std::numeric_limits<qreal>::max();
        while (!open.empty()) {
            const auto [c, state] = open.top();
            open.pop();
            if (c > cost[std::size_t(state)] || c >= reachedCost)
                continue;
            const int index = state / 4;
            const int direction = state % 4;
            if (const auto goal = goalOf.find(index); goal != goalOf.end()) {
                const QPointF at(xs[std::size_t(index / ny)], ys[std::size_t(index % ny)]);
                const qreal total = c + QLineF(at, goal->second.first).length() + (direction == goal->second.second ? 0 : Bend);
                if (total < reachedCost) {
                    reachedCost = total;
                    reached = state;
                }
            }
            const int i = index / ny;
            const int j = index % ny;
            for (int d = 0; d < 4; ++d) {
                if ((direction == 0 && d == 1) || (direction == 1 && d == 0) || (direction == 2 && d == 3) ||
                    (direction == 3 && d == 2))
                    continue; // no turning back
                const int ni = i + dx[d];
                const int nj = j + dy[d];
                if (ni < 0 || nj < 0 || ni >= nx || nj >= ny)
                    continue;
                const int next = ni * ny + nj;
                if (!free[std::size_t(next)])
                    continue;
                // the step between two neighbouring lines runs through an obstacle if its middle does
                const int from = std::min(index, next);
                if ((d >= 2 && !rightFree[std::size_t(from)]) || (d < 2 && !downFree[std::size_t(from)]))
                    continue;
                const QPointF middle((xs[std::size_t(i)] + xs[std::size_t(ni)]) / 2, (ys[std::size_t(j)] + ys[std::size_t(nj)]) / 2);
                if (insideEnds(middle))
                    continue;
                const qreal step = std::abs(xs[std::size_t(ni)] - xs[std::size_t(i)]) + std::abs(ys[std::size_t(nj)] - ys[std::size_t(j)]);
                const qreal nc = c + step + (d == direction ? 0 : Bend);
                const int nextState = next * 4 + d;
                if (nc < cost[std::size_t(nextState)]) {
                    cost[std::size_t(nextState)] = nc;
                    previous[std::size_t(nextState)] = state;
                    open.push({nc, nextState});
                }
            }
        }
        if (reached < 0) {
            cache[key] = {};
            return {};
        }
        std::vector<QPointF> points;
        points.push_back(goalOf.at(reached / 4).first);
        for (int state = reached; state >= 0; state = previous[std::size_t(state)])
            points.push_back(QPointF(xs[std::size_t(state / 4 / ny)], ys[std::size_t(state / 4 % ny)]));
        // the border point of the source the first grid point lies in front of
        const QPointF first = points.back();
        QPointF border = DiagramGeometry::borderPointTowards(source, first);
        for (const auto &[b, outside]: starts) {
            if (QLineF(outside, first).length() < 0.5)
                border = b;
        }
        points.push_back(border);
        std::reverse(points.begin(), points.end());
        // only the corners
        std::vector<QPointF> corners;
        for (const QPointF &p: points) {
            if (!corners.empty() && QLineF(corners.back(), p).length() < 0.5)
                continue;
            if (corners.size() >= 2) {
                const QPointF &a = corners[corners.size() - 2];
                const QPointF &b = corners.back();
                const bool straight = (std::abs(a.x() - b.x()) < 0.5 && std::abs(b.x() - p.x()) < 0.5) ||
                                      (std::abs(a.y() - b.y()) < 0.5 && std::abs(b.y() - p.y()) < 0.5);
                if (straight) {
                    corners.back() = p;
                    continue;
                }
            }
            corners.push_back(p);
        }
        cache[key] = corners;
        return corners;
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
        if (bestCrossings > 0) {
            // no simple route around them: the way through the gaps between the nodes
            auto around = gridRoute(source, target, obstacles);
            if (around.size() >= 2 && crossings(around, obstacles) == 0)
                return around;
        }
        return best;
    }

    /// The nodes a line between source and target should not pass: all others (notes, parts, definitions, package
    /// frames) but boundaries, ports and the nodes that contain source or target (a part or package around them).
    inline std::vector<QRectF> obstaclesFor(const DiagramNodeModel &nodes, const DiagramNode &source,
                                            const DiagramNode &target) {
        std::vector<QRectF> obstacles;
        for (const auto &node: nodes.nodes()) {
            // package frames too (a line does not cut through a package it does not lead into or out of)
            if (node.Id == source.Id || node.Id == target.Id || node.Kind == DiagramTypes::BoundaryNode ||
                node.Kind == DiagramTypes::PortNode || node.Rect.contains(source.Rect) || node.Rect.contains(target.Rect) ||
                node.Rect.isEmpty())
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

    /// Where the lines of an n-ary connection / dependency meet: the middle of the nodes of its ends.
    inline QPointF junctionPoint(const DiagramNodeModel &nodes, const std::vector<DiagramEdge> &edges, const QString &group) {
        QPointF sum;
        int count = 0;
        std::vector<QRectF> rects;
        for (const auto &other: edges) {
            if (other.Group != group)
                continue;
            if (const auto *node = nodes.find(other.Source)) {
                sum += node->Rect.center();
                rects.push_back(node->Rect);
                ++count;
            }
        }
        if (count == 0)
            return {};
        QPointF point = sum / count;
        // not hidden inside one of the ends (ends in a row): below them
        if (std::any_of(rects.begin(), rects.end(), [&](const QRectF &rect) { return rect.adjusted(-8, -8, 8, 8).contains(point); })) {
            qreal bottom = point.y();
            for (const auto &rect: rects)
                bottom = std::max(bottom, rect.bottom());
            point.setY(bottom + 36);
        }
        return point;
    }

    /// A port on the border of its part: the outward direction there and the part (null if it is on none).
    inline std::pair<QPointF, const DiagramNode *> portOutward(const DiagramNodeModel &nodes, const DiagramNode &port) {
        if (port.Kind != DiagramTypes::PortNode)
            return {};
        const QPointF c = port.Rect.center();
        const DiagramNode *anchor = nullptr;
        qreal best = std::numeric_limits<qreal>::max();
        for (const auto &node: nodes.nodes()) {
            if (node.Id == port.Id || node.Kind == DiagramTypes::PortNode || node.Kind == DiagramTypes::PackageNode ||
                !node.Rect.adjusted(-2, -2, 2, 2).contains(c))
                continue;
            const QRectF r = node.Rect;
            const qreal distance = std::min({std::abs(c.x() - r.left()), std::abs(r.right() - c.x()),
                                             std::abs(c.y() - r.top()), std::abs(r.bottom() - c.y())});
            if (distance <= 2.0 && r.width() * r.height() < best) {
                best = r.width() * r.height();
                anchor = &node;
            }
        }
        if (!anchor)
            return {};
        const QRectF r = anchor->Rect;
        const qreal left = std::abs(c.x() - r.left());
        const qreal right = std::abs(r.right() - c.x());
        const qreal top = std::abs(c.y() - r.top());
        const qreal nearest = std::min({left, right, top, std::abs(r.bottom() - c.y())});
        const QPointF direction = nearest == left ? QPointF(-1, 0) : nearest == right ? QPointF(1, 0)
                                  : nearest == top ? QPointF(0, -1) : QPointF(0, 1);
        return {direction, anchor};
    }

    /// The height of the tab of a package (NodeDelegate.qml: frame.tabHeight).
    constexpr qreal PackageTabHeight = 24.0;

    /// A straight line from the border of source to end (on the border of target, leaving it towards outward); if
    /// it passes another node, the way around them, the last piece straight out of the border at end.
    inline std::vector<QPointF> straightOrAround(const DiagramNodeModel &nodes, const DiagramNode &source,
                                                 const DiagramNode &target, const QPointF &end, const QPointF &outward) {
        const QPointF start = DiagramGeometry::borderPointTowards(source.Rect, end);
        std::vector<QPointF> straight{start, end};
        auto obstacles = obstaclesFor(nodes, source, target);
        if (crossings(straight, obstacles) == 0)
            return straight;
        const QPointF stub = end + outward * 30.0;
        obstacles.push_back(target.Rect);
        auto around = gridRoute(source.Rect, QRectF(stub - QPointF(0.5, 0.5), QSizeF(1, 1)), obstacles);
        if (around.size() < 2)
            return straight;
        around.back() = stub;
        around.push_back(end);
        if (crossings(around, obstacles) > 0)
            return straight;
        return around;
    }

    /// The route of an edge of the model (empty if one of its nodes is missing).
    inline std::vector<QPointF> routeOf(const DiagramNodeModel &nodes, const std::vector<DiagramEdge> &edges,
                                        const DiagramEdge &edge) {
        const auto *sourceNode = nodes.find(edge.Source);
        const auto *targetNode = nodes.find(edge.Target);
        if (!sourceNode || !targetNode)
            return {};
        // a package is a folder (table 3): its tab stands above the body, lines end on the body
        const auto body = [](const DiagramNode &node) {
            DiagramNode result = node;
            if (node.Kind == DiagramTypes::PackageNode && node.Rect.height() > PackageTabHeight * 2)
                result.Rect.setTop(node.Rect.top() + PackageTabHeight);
            return result;
        };
        const DiagramNode sourceBody = body(*sourceNode);
        const DiagramNode targetBody = body(*targetNode);
        const auto *source = &sourceBody;
        const auto *target = &targetBody;
        if (edge.Junction != 0) {
            // a line of an n-ary relationship: between its end and the dot (towards the supplier for a supplier)
            const QPointF junction = junctionPoint(nodes, edges, edge.Group);
            const QPointF border = DiagramGeometry::borderPointTowards(source->Rect, junction);
            if (edge.Junction == 3)
                return {junction, border};
            return {border, junction};
        }
        // a note to its element (table 2, "Annotation-Documentation"): a straight line that ends on the border of the
        // element between its ports, not on a port
        if (edge.Kind == DiagramTypes::Annotation && edge.TargetEntryRect.isEmpty() && source->Kind != DiagramTypes::PortNode &&
            target->Kind != DiagramTypes::PortNode) {
            const QRectF r = target->Rect;
            QPointF end = DiagramGeometry::edgeLine(source->Rect, r).p2();
            const qreal left = std::abs(end.x() - r.left());
            const qreal right = std::abs(r.right() - end.x());
            const qreal top = std::abs(end.y() - r.top());
            const qreal bottom = std::abs(r.bottom() - end.y());
            const qreal nearest = std::min({left, right, top, bottom});
            const bool vertical = nearest == left || nearest == right; // the end runs along a vertical side
            const qreal side = nearest == left ? r.left() : nearest == right ? r.right() : nearest == top ? r.top() : r.bottom();
            std::vector<std::pair<qreal, qreal>> taken; // along the side: where ports (and a gap around them) are
            for (const auto &node: nodes.nodes()) {
                if (node.Kind != DiagramTypes::PortNode)
                    continue;
                const QPointF c = node.Rect.center();
                if (std::abs((vertical ? c.x() : c.y()) - side) > 3.0 ||
                    !r.adjusted(-3, -3, 3, 3).contains(c))
                    continue;
                const qreal half = (vertical ? node.Rect.height() : node.Rect.width()) / 2 + 6;
                const qreal at = vertical ? c.y() : c.x();
                taken.emplace_back(at - half, at + half);
            }
            const qreal low = (vertical ? r.top() : r.left()) + 8;
            const qreal high = (vertical ? r.bottom() : r.right()) - 8;
            const auto free = [&](qreal at) {
                return at >= low && at <= high && std::none_of(taken.begin(), taken.end(), [&](const auto &span) {
                    return at > span.first && at < span.second;
                });
            };
            qreal along = vertical ? end.y() : end.x();
            if (!free(along)) {
                // the free place nearest to where the straight line meets the border
                qreal best = along;
                qreal bestDistance = std::numeric_limits<qreal>::max();
                std::vector<qreal> candidates{low, high, (low + high) / 2};
                for (const auto &span: taken) {
                    candidates.push_back(span.first);
                    candidates.push_back(span.second);
                }
                for (const qreal candidate: candidates) {
                    if (free(candidate) && std::abs(candidate - along) < bestDistance) {
                        bestDistance = std::abs(candidate - along);
                        best = candidate;
                    }
                }
                along = best;
            }
            end = vertical ? QPointF(side, along) : QPointF(along, side);
            const QPointF outward = nearest == left ? QPointF(-1, 0) : nearest == right ? QPointF(1, 0)
                                    : nearest == top ? QPointF(0, -1) : QPointF(0, 1);
            return straightOrAround(nodes, *source, *target, end, outward);
        }
        // to an entry of a compartment (annotation of "part1::attribute1", table 2): the line ends at that entry
        if (!edge.TargetEntryRect.isEmpty()) {
            const QRectF entry = edge.TargetEntryRect.translated(target->Rect.topLeft());
            // from the side of the node the source is on, at the height of the entry
            const bool left = source->Rect.center().x() < target->Rect.center().x();
            const QPointF end(left ? target->Rect.left() : target->Rect.right(), entry.center().y());
            return straightOrAround(nodes, *source, *target, end, QPointF(left ? -1 : 1, 0));
        }
        // from a port: first straight out of the border of its part, then on to the other end (graphical BNF:
        // connections attach to the port square from outside; inside for a port of the part around the other end)
        auto [outSource, anchorSource] = portOutward(nodes, *source);
        auto [outTarget, anchorTarget] = portOutward(nodes, *target);
        if (anchorSource && anchorSource->Rect.contains(target->Rect.center()))
            outSource = -outSource;
        if (anchorTarget && anchorTarget->Rect.contains(source->Rect.center()))
            outTarget = -outTarget;
        if (!edge.SourceOut.isNull())
            outSource = edge.SourceOut; // as the view placed it (a pin on the frame of the context)
        if (!edge.TargetOut.isNull())
            outTarget = edge.TargetOut;
        if (!outSource.isNull() || !outTarget.isNull()) {
            const qreal stub = 18.0;
            const qreal offset = parallelOffset(edges, edge);
            const QPointF sideSource(-outSource.y(), outSource.x());
            // both ends shifted to the same side (lines side by side, not crossing)
            const QPointF sideTarget = !outSource.isNull() ? sideSource : QPointF(-outTarget.y(), outTarget.x());
            const QPointF startPoint = outSource.isNull() ? QPointF() : source->Rect.center() + outSource * (source->Rect.width() / 2);
            const QPointF endPoint = outTarget.isNull() ? QPointF() : target->Rect.center() + outTarget * (target->Rect.width() / 2);
            const QPointF s = outSource.isNull() ? QPointF() : startPoint + outSource * (stub + std::abs(offset)) + sideSource * offset;
            const QPointF t = outTarget.isNull() ? QPointF() : endPoint + outTarget * (stub + std::abs(offset)) + sideTarget * offset;
            const QRectF from = outSource.isNull() ? source->Rect : QRectF(s - QPointF(0.5, 0.5), QSizeF(1, 1));
            const QRectF to = outTarget.isNull() ? target->Rect : QRectF(t - QPointF(0.5, 0.5), QSizeF(1, 1));
            std::vector<QRectF> obstacles;
            for (const auto &node: nodes.nodes()) {
                if (node.Id == source->Id || node.Id == target->Id ||
                    node.Kind == DiagramTypes::BoundaryNode || node.Kind == DiagramTypes::PortNode || node.Rect.isEmpty() ||
                    node.Rect.contains(from) || node.Rect.contains(to))
                    continue;
                obstacles.push_back(node.Rect);
            }
            auto middle = route(from, to, obstacles, 0);
            std::vector<QPointF> path;
            if (!outSource.isNull()) {
                path.push_back(startPoint + sideSource * offset);
                path.push_back(s);
            }
            for (std::size_t i = 0; i < middle.size(); ++i) {
                // the tiny stub rectangles: their border points are the stub ends themselves
                if ((i == 0 && !outSource.isNull()) || (i + 1 == middle.size() && !outTarget.isNull()))
                    continue;
                path.push_back(middle[i]);
            }
            if (!outTarget.isNull()) {
                path.push_back(t);
                path.push_back(endPoint + sideTarget * offset);
            }
            if (path.size() >= 2)
                return path;
        }
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
