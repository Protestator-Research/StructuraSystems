//
// Nodes of the diagram as list model for QML (one delegate per package, part definition and part).
// Plain data filled by the DiagramController; the semantic model is the only source of the content,
// the geometry (position and size on the canvas) lives only here.
//

#ifndef STRUCTURASYSTEMS_DIAGRAMNODEMODEL_H
#define STRUCTURASYSTEMS_DIAGRAMNODEMODEL_H

#include <QAbstractListModel>
#include <QRectF>
#include <QString>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <cstdint>
#include <vector>

#include "DiagramTypes.h"

namespace StructuraSystems::Client {

    struct DiagramNode {
        std::uint64_t Id = 0;
        DiagramTypes::NodeKind Kind = DiagramTypes::PartNode;
        QString Keyword;   ///< «part» / «part def», empty for packages
        QString Title;     ///< e.g. "engine : Engine[1]" or "package Vehicles"
        /// [{title: "attributes", entries: [{entryId, text, tooltip}]}, ...] in display order
        QVariantList Compartments;
        /// Parametric view: parameters on the left (inputs) and right (result) border, [{name, text, open}]
        QVariantList InPins;
        QVariantList OutPins;
        QRectF Rect;       ///< canvas coordinates
        int Depth = 0;     ///< ownership depth, used for the stacking order
        bool Selected = false;

        [[nodiscard]] bool sameContent(const DiagramNode &other) const {
            return Kind == other.Kind && Keyword == other.Keyword && Title == other.Title &&
                   Compartments == other.Compartments && Depth == other.Depth && InPins == other.InPins &&
                   OutPins == other.OutPins;
        }
    };

    class DiagramNodeModel : public QAbstractListModel {
        Q_OBJECT
        QML_ELEMENT
        QML_UNCREATABLE("Owned by DiagramController.")
        Q_PROPERTY(int count READ count NOTIFY countChanged)

    public:
        enum Roles {
            ElementIdRole = Qt::UserRole + 1,
            NodeKindRole,
            KeywordRole,
            TitleRole,
            CompartmentsRole,
            NodeXRole,
            NodeYRole,
            NodeWidthRole,
            NodeHeightRole,
            DepthRole,
            SelectedRole,
            InPinsRole,
            OutPinsRole
        };
        Q_ENUM(Roles)

        explicit DiagramNodeModel(QObject *parent = nullptr);

        [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
        [[nodiscard]] QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
        [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
        [[nodiscard]] int count() const { return int(Nodes.size()); }

        // ---- C++ API used by DiagramController and EdgeLayerItem ----
        [[nodiscard]] const std::vector<DiagramNode> &nodes() const { return Nodes; }
        [[nodiscard]] int indexOf(std::uint64_t id) const;
        /// nullptr if there is no node with this id.
        [[nodiscard]] const DiagramNode *find(std::uint64_t id) const;

        void append(const DiagramNode &node);
        void removeAt(int row);
        /// Takes over the content (not geometry and selection) of node.
        void updateContent(int row, const DiagramNode &node);
        void setRect(int row, const QRectF &rect);
        void setSelected(int row, bool selected);

        /// Number of entries in the compartment with the given title (0 if there is none).
        [[nodiscard]] static int entryCount(const DiagramNode &node, const QString &title);

    signals:
        void countChanged();
        /// Any geometry changed (used by the edge layer and for the canvas size).
        void geometryChanged();

    private:
        std::vector<DiagramNode> Nodes;
    };
}

#endif //STRUCTURASYSTEMS_DIAGRAMNODEMODEL_H
