//
// Edges of the diagram as list model. The edges are drawn by EdgeLayerItem; the model is exposed to QML
// mainly for counts and tests. Filled by the DiagramController.
//

#ifndef STRUCTURASYSTEMS_DIAGRAMEDGEMODEL_H
#define STRUCTURASYSTEMS_DIAGRAMEDGEMODEL_H

#include <QAbstractListModel>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <cstdint>
#include <vector>

#include "DiagramTypes.h"

namespace StructuraSystems::Client {

    struct DiagramEdge {
        /// "c:<child id>" for a composition (ownership), "r:<relationship id>" for a relationship.
        QString Key;
        DiagramTypes::EdgeKind Kind = DiagramTypes::Connection;
        std::uint64_t Source = 0; ///< composition: the owner
        std::uint64_t Target = 0; ///< composition: the owned part
        bool Selected = false;
        /// Parametric view: the line starts at an output pin of the source (index), or at its right border (-1),
        /// and ends at an input pin of the target (index) or its left border (-1).
        int SourcePin = -1;
        int TargetPin = -1;

        bool operator==(const DiagramEdge &other) const = default;
    };

    class DiagramEdgeModel : public QAbstractListModel {
        Q_OBJECT
        QML_ELEMENT
        QML_UNCREATABLE("Owned by DiagramController.")
        Q_PROPERTY(int count READ count NOTIFY countChanged)

    public:
        enum Roles {
            EdgeKeyRole = Qt::UserRole + 1,
            EdgeKindRole,
            SourceIdRole,
            TargetIdRole,
            SelectedRole
        };
        Q_ENUM(Roles)

        explicit DiagramEdgeModel(QObject *parent = nullptr);

        [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
        [[nodiscard]] QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
        [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
        [[nodiscard]] int count() const { return int(Edges.size()); }

        // ---- C++ API ----
        [[nodiscard]] const std::vector<DiagramEdge> &edges() const { return Edges; }
        [[nodiscard]] const DiagramEdge *find(const QString &key) const;
        /// Replaces all edges (only if something changed).
        void setEdges(std::vector<DiagramEdge> edges);

    signals:
        void countChanged();

    private:
        std::vector<DiagramEdge> Edges;
    };
}

#endif //STRUCTURASYSTEMS_DIAGRAMEDGEMODEL_H
