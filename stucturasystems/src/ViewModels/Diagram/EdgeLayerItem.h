//
// Draws all edges of a diagram (composition ◆, flow ▶, specialization △, connection) plus the rubber band
// while an arrow is drawn. Lies as overlay above the visible part of the canvas, so it only paints what is
// on screen; origin and zoom follow the Flickable. Does not take any mouse input.
// A second instance below the nodes with showGrid (and no models) draws the dot grid of the canvas.
//

#ifndef STRUCTURASYSTEMS_EDGELAYERITEM_H
#define STRUCTURASYSTEMS_EDGELAYERITEM_H

#include <QColor>
#include <QPointF>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

namespace StructuraSystems::Client {
    class DiagramEdgeModel;
    class DiagramNodeModel;

    class EdgeLayerItem : public QQuickPaintedItem {
        Q_OBJECT
        QML_ELEMENT
        Q_MOC_INCLUDE("DiagramNodeModel.h")
        Q_MOC_INCLUDE("DiagramEdgeModel.h")
        Q_PROPERTY(StructuraSystems::Client::DiagramNodeModel *nodes READ nodes WRITE setNodes NOTIFY nodesChanged)
        Q_PROPERTY(StructuraSystems::Client::DiagramEdgeModel *edges READ edges WRITE setEdges NOTIFY edgesChanged)
        /// Canvas coordinate shown at the top left corner of this item (Flickable contentX / zoom).
        Q_PROPERTY(qreal originX READ originX WRITE setOriginX NOTIFY viewChanged)
        Q_PROPERTY(qreal originY READ originY WRITE setOriginY NOTIFY viewChanged)
        Q_PROPERTY(qreal zoom READ zoom WRITE setZoom NOTIFY viewChanged)
        Q_PROPERTY(QColor lineColor READ lineColor WRITE setLineColor NOTIFY colorsChanged)
        Q_PROPERTY(QColor selectionColor READ selectionColor WRITE setSelectionColor NOTIFY colorsChanged)
        /// Fill of the hollow specialization triangle (the canvas background).
        Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor NOTIFY colorsChanged)
        Q_PROPERTY(bool rubberBandVisible READ rubberBandVisible WRITE setRubberBandVisible NOTIFY rubberBandChanged)
        /// Start and end of the rubber band in canvas coordinates.
        Q_PROPERTY(QPointF rubberStart READ rubberStart WRITE setRubberStart NOTIFY rubberBandChanged)
        Q_PROPERTY(QPointF rubberEnd READ rubberEnd WRITE setRubberEnd NOTIFY rubberBandChanged)
        Q_PROPERTY(bool showGrid READ showGrid WRITE setShowGrid NOTIFY gridChanged)
        /// Parametric view: lines run from output pins (right) to input pins (left) as curves.
        Q_PROPERTY(bool pinMode READ pinMode WRITE setPinMode NOTIFY pinModeChanged)
        Q_PROPERTY(qreal gridSize READ gridSize WRITE setGridSize NOTIFY gridChanged)
        Q_PROPERTY(QColor gridColor READ gridColor WRITE setGridColor NOTIFY gridChanged)

    public:
        explicit EdgeLayerItem(QQuickItem *parent = nullptr);

        void paint(QPainter *painter) override;

        [[nodiscard]] DiagramNodeModel *nodes() const { return Nodes; }
        void setNodes(DiagramNodeModel *nodes);
        [[nodiscard]] DiagramEdgeModel *edges() const { return Edges; }
        void setEdges(DiagramEdgeModel *edges);
        [[nodiscard]] qreal originX() const { return OriginX; }
        void setOriginX(qreal value);
        [[nodiscard]] qreal originY() const { return OriginY; }
        void setOriginY(qreal value);
        [[nodiscard]] qreal zoom() const { return Zoom; }
        void setZoom(qreal value);
        [[nodiscard]] QColor lineColor() const { return LineColor; }
        void setLineColor(const QColor &color);
        [[nodiscard]] QColor selectionColor() const { return SelectionColor; }
        void setSelectionColor(const QColor &color);
        [[nodiscard]] QColor backgroundColor() const { return BackgroundColor; }
        void setBackgroundColor(const QColor &color);
        [[nodiscard]] bool rubberBandVisible() const { return RubberBandVisible; }
        void setRubberBandVisible(bool visible);
        [[nodiscard]] QPointF rubberStart() const { return RubberStart; }
        void setRubberStart(const QPointF &point);
        [[nodiscard]] QPointF rubberEnd() const { return RubberEnd; }
        void setRubberEnd(const QPointF &point);
        [[nodiscard]] bool pinMode() const { return PinMode; }
        void setPinMode(bool enabled);
        [[nodiscard]] bool showGrid() const { return ShowGrid; }
        void setShowGrid(bool show);
        [[nodiscard]] qreal gridSize() const { return GridSize; }
        void setGridSize(qreal size);
        [[nodiscard]] QColor gridColor() const { return GridColor; }
        void setGridColor(const QColor &color);

    signals:
        void nodesChanged();
        void edgesChanged();
        void viewChanged();
        void colorsChanged();
        void rubberBandChanged();
        void gridChanged();
        void pinModeChanged();

    private:
        void connectModel(QObject *model);
        void paintGrid(QPainter *painter);
        void paintPinEdges(QPainter *painter);

        QPointer<DiagramNodeModel> Nodes;
        QPointer<DiagramEdgeModel> Edges;
        qreal OriginX = 0;
        qreal OriginY = 0;
        qreal Zoom = 1;
        QColor LineColor{0x1b, 0x21, 0x28};
        QColor SelectionColor{0x0e, 0x7c, 0x86};
        QColor BackgroundColor{Qt::white};
        bool RubberBandVisible = false;
        QPointF RubberStart;
        QPointF RubberEnd;
        bool ShowGrid = false;
        bool PinMode = false;
        qreal GridSize = 20;
        QColor GridColor{0x9a, 0xa7, 0xb3};
    };
}

#endif //STRUCTURASYSTEMS_EDGELAYERITEM_H
