//
// Parametric view of one part / part definition (SysML v2 interconnection view of constraints and calculations):
// its attributes as values, calls "f(a, b)" and other expressions that compute values, constraint and calc
// usages with their parameters as pins, and the lines between them (arguments, parameter values, "bind").
//
// Nothing of this view is stored on its own: every node and line is read from the semantic model (values,
// expressions, parameters, bindings) and every edit is written back into it, so the generated SysML v2 text
// contains exactly what is drawn:
//
//   line value a -> argument i of the call in b   b = f(.., a, ..)
//   line value a -> parameter p of a usage c      c { in p = a; }
//   line value a -> value b                       b = a   or   bind b = a;
//   line calc usage c -> value b                  b = c
//   calc def dropped onto the canvas / a value    derived attribute r = f(p1, p2)  /  b = f(p1, p2)
//   constraint def dropped onto the canvas        assert constraint c : Def;
//
// Coordinates are canvas coordinates; ids are passed to QML as strings.
//

#ifndef STRUCTURASYSTEMS_PARAMETRICCONTROLLER_H
#define STRUCTURASYSTEMS_PARAMETRICCONTROLLER_H

#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <Diagram/Semantic/SemanticModel.h>

#include "DiagramEdgeModel.h"
#include "DiagramNodeModel.h"
#include "DiagramTypes.h"

namespace StructuraSystems::Client {
    class DiagramController;

    class ParametricController : public QObject {
        Q_OBJECT
        QML_ELEMENT
        QML_UNCREATABLE("Owned by DiagramEditorModel.")
        Q_MOC_INCLUDE("DiagramController.h")
        Q_PROPERTY(StructuraSystems::Client::DiagramNodeModel *nodes READ nodes CONSTANT)
        Q_PROPERTY(StructuraSystems::Client::DiagramEdgeModel *edges READ edges CONSTANT)
        /// Element id of the part / part definition shown.
        Q_PROPERTY(QString contextId READ contextId WRITE setContextId NOTIFY contextChanged)
        /// Parts and part definitions that can be shown: [{id, title}]
        Q_PROPERTY(QVariantList contexts READ contexts NOTIFY contextsChanged)
        /// Constraint and calc definitions of the model for the palette: [{id, name, keyword, calc}]
        Q_PROPERTY(QVariantList definitions READ definitions NOTIFY contextsChanged)
        Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
        Q_PROPERTY(qreal canvasWidth READ canvasWidth NOTIFY canvasSizeChanged)
        Q_PROPERTY(qreal canvasHeight READ canvasHeight NOTIFY canvasSizeChanged)

    public:
        ParametricController(Diagram::SemanticModel &model, DiagramController *structure, QObject *parent = nullptr);
        ~ParametricController() override;

        [[nodiscard]] DiagramNodeModel *nodes() const { return Nodes; }
        [[nodiscard]] DiagramEdgeModel *edges() const { return Edges; }
        [[nodiscard]] QString contextId() const;
        void setContextId(const QString &id);
        [[nodiscard]] QVariantList contexts() const { return Contexts; }
        [[nodiscard]] QVariantList definitions() const { return Definitions; }
        [[nodiscard]] bool hasSelection() const { return !SelectedNodes.empty() || !SelectedEdges.empty(); }
        [[nodiscard]] qreal canvasWidth() const { return CanvasSize.width(); }
        [[nodiscard]] qreal canvasHeight() const { return CanvasSize.height(); }

        // ---- editing (QML) ----
        /// New attribute of the context at (x, y).
        Q_INVOKABLE QString addValue(qreal x, qreal y);
        /// New constraint (DiagramTypes::ToolConstraint) or calculation (ToolCalc) of the context.
        Q_INVOKABLE QString addUsage(int tool, qreal x, qreal y);
        /// A constraint / calc definition from the palette dropped at (x, y) (see the file comment).
        Q_INVOKABLE QString dropDefinition(const QString &definitionId, qreal x, qreal y);
        /// What is at (x, y): {node, element, pin, output} (node empty if nothing; pin -1 = the node itself).
        Q_INVOKABLE QVariantMap hitTest(qreal x, qreal y) const;
        /// Possible meanings of a line from (sourceNode, pin, output) to (targetNode, pin, output):
        /// {options: [{option, label}], error}. Usually one option; value -> value offers "value" and "bind".
        Q_INVOKABLE QVariantMap connectOptions(const QString &sourceNode, int sourcePin, bool sourceOutput,
                                               const QString &targetNode, int targetPin, bool targetOutput) const;
        /// Draws the line with the chosen option; returns an error text (empty on success).
        Q_INVOKABLE QString connectPins(const QString &sourceNode, int sourcePin, bool sourceOutput,
                                    const QString &targetNode, int targetPin, bool targetOutput, const QString &option);
        Q_INVOKABLE QString edgeAt(qreal x, qreal y) const;
        Q_INVOKABLE QPointF pinPosition(const QString &node, int pin, bool output) const;
        Q_INVOKABLE QRectF nodeRect(const QString &node) const;
        /// Element to edit in the element dialog for a node (the attribute of a call / expression).
        Q_INVOKABLE QString elementOf(const QString &node) const;
        Q_INVOKABLE void select(const QString &node, bool additive);
        Q_INVOKABLE void selectEdge(const QString &key, bool additive);
        Q_INVOKABLE void clearSelection();
        Q_INVOKABLE void deleteSelection();
        Q_INVOKABLE void deleteEdge(const QString &key);
        Q_INVOKABLE void moveNode(const QString &node, qreal x, qreal y);
        Q_INVOKABLE void layoutAll();
        [[nodiscard]] Q_INVOKABLE QRectF contentBounds() const;

        /// Applies pending model changes immediately (also used by the tests).
        void syncNow();

        /// Node id of the call / expression computing the value of an attribute.
        static std::uint64_t computationNodeId(Diagram::ElementId attribute) { return attribute | ComputationFlag; }

    signals:
        void statusMessage(const QString &message);
        void contextChanged();
        void contextsChanged();
        void selectionChanged();
        void canvasSizeChanged();

    private:
        static constexpr std::uint64_t ComputationFlag = std::uint64_t(1) << 48;

        /// Where a line can start or end.
        struct Endpoint {
            std::uint64_t Node = 0;
            int Pin = -1;
            bool Output = false;
        };

        /// Parameter pins of a call / usage: name and the element holding its value (if any).
        struct Pin {
            std::string Name;
            std::optional<Diagram::ElementId> Parameter; ///< own parameter element of a usage
            std::string Value;                           ///< argument / parameter value as written
        };

        struct NodeInfo {
            DiagramTypes::NodeKind Kind = DiagramTypes::ValueNode;
            Diagram::ElementId Element = 0; ///< attribute (also for calls / expressions) or usage
            std::vector<Pin> Inputs;
            std::string ResultName;         ///< output pin of calls and calc usages
        };

        [[nodiscard]] std::optional<Diagram::ElementId> context() const;
        void chooseContext();
        void updateLists();
        void build(std::vector<DiagramNode> &nodes, std::vector<DiagramEdge> &edges);
        void scheduleSync();
        void applySelection();
        void updateCanvasSize();
        [[nodiscard]] QSizeF sizeOf(const DiagramNode &node) const;
        bool runModelChange(const std::function<void()> &change);

        [[nodiscard]] std::optional<std::uint64_t> toNode(const QString &id) const;
        [[nodiscard]] bool isValueNode(std::uint64_t node) const;
        [[nodiscard]] std::optional<Diagram::ElementId> resolve(const std::string &reference,
                                                                const std::function<bool(Diagram::ElementId)> &accept) const;
        [[nodiscard]] std::optional<Diagram::ElementId> definitionOf(const std::string &reference, bool calc) const;
        [[nodiscard]] std::string referenceTo(Diagram::ElementId element) const;
        [[nodiscard]] std::string uniqueName(const std::string &base, Diagram::ElementId owner) const;
        [[nodiscard]] std::vector<Diagram::ElementId> parametersOf(Diagram::ElementId owner, bool inputs) const;
        [[nodiscard]] std::vector<Pin> usagePins(Diagram::ElementId usage) const;
        [[nodiscard]] std::optional<Endpoint> endpointOf(Diagram::ElementId element) const;
        QString connectValueToCall(Diagram::ElementId value, Diagram::ElementId attribute, int pin);
        QString connectValueToParameter(Diagram::ElementId value, Diagram::ElementId usage, int pin);
        void clearArgument(Diagram::ElementId attribute, int pin);

        Diagram::SemanticModel &Model;
        QPointer<DiagramController> Structure;
        DiagramNodeModel *Nodes;
        DiagramEdgeModel *Edges;
        Diagram::SemanticModel::ListenerToken ListenerToken = 0;
        std::optional<Diagram::ElementId> ContextElement;
        QVariantList Contexts;
        QVariantList Definitions;
        std::map<std::uint64_t, NodeInfo> Info;
        std::map<std::uint64_t, QPointF> Positions; ///< kept while the view is open
        std::set<std::uint64_t> SelectedNodes;
        std::set<QString> SelectedEdges;
        QSizeF CanvasSize{1600, 1000};
        bool SyncQueued = false;
        bool AutoLayout = true;
    };
}

#endif //STRUCTURASYSTEMS_PARAMETRICCONTROLLER_H
