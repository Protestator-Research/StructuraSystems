//
// Editing logic of the graphical editor.
//

#include "DiagramController.h"
#include "DiagramGeometry.h"

#include <QMetaObject>

#include <Diagram/Conversion/SysMLCodeGenerator.h>
#include <Diagram/Conversion/SysMLTextImporter.h>
#include <Diagram/Semantic/RelationshipRules.h>

#include <algorithm>
#include <exception>
#include <iterator>
#include <vector>

namespace StructuraSystems::Client {

    using Diagram::ElementId;
    using Diagram::ElementKind;
    using Diagram::Multiplicity;
    using Diagram::RelationshipKind;
    using Diagram::RelationshipRules;
    using Diagram::RootId;
    using Diagram::SysMLTextImporter;

    namespace {
        constexpr qreal DefaultPartWidth = 160.0;
        constexpr qreal DefaultPartHeight = 64.0;
        constexpr qreal DefaultFrameWidth = 360.0;
        constexpr qreal DefaultFrameHeight = 240.0;
        constexpr qreal MinimumFrameWidth = 160.0;
        constexpr qreal MinimumFrameHeight = 90.0;
        /// Frames are grabbed at their border (this distance) and at their title band.
        constexpr qreal FrameBorderGrip = 6.0;
        constexpr qreal FrameTitleHeight = 30.0;
        constexpr qreal EdgeHitDistance = 6.0;
        constexpr qreal CanvasMargin = 600.0;

        bool isNodeKind(ElementKind kind) {
            return kind == ElementKind::Package || kind == ElementKind::PartDefinition || kind == ElementKind::PartUsage;
        }

        using Diagram::isConstraintOrCalc;

        /// Elements that can receive attributes, actions, constraints or calculations dropped from the palette.
        bool acceptsFeature(ElementKind owner, int tool) {
            if (owner == ElementKind::PartDefinition || owner == ElementKind::PartUsage)
                return true;
            // parameters and nested constraints / calculations
            return isConstraintOrCalc(owner) && tool != DiagramTypes::ToolAction;
        }

        /// in / out / inout / return of a parameter (empty if the attribute is not a parameter).
        std::string directionOf(const Diagram::SysMLElement &element) {
            std::string direction;
            for (const auto &prefix: element.prefixes()) {
                if (prefix == "in" || prefix == "out" || prefix == "inout" || prefix == "return")
                    direction = prefix;
            }
            return direction;
        }

        bool isPartLike(ElementKind kind) {
            return kind == ElementKind::PartDefinition || kind == ElementKind::PartUsage;
        }

        std::optional<RelationshipKind> toRelationshipKind(int kind) {
            switch (kind) {
                case DiagramTypes::Connection:
                    return RelationshipKind::Connection;
                case DiagramTypes::Flow:
                    return RelationshipKind::Flow;
                case DiagramTypes::Specialization:
                    return RelationshipKind::Specialization;
                default:
                    return std::nullopt;
            }
        }

        DiagramTypes::EdgeKind toEdgeKind(RelationshipKind kind) {
            switch (kind) {
                case RelationshipKind::Connection:
                    return DiagramTypes::Connection;
                case RelationshipKind::Flow:
                    return DiagramTypes::Flow;
                case RelationshipKind::Specialization:
                    return DiagramTypes::Specialization;
                case RelationshipKind::Binding:
                    return DiagramTypes::Binding;
            }
            return DiagramTypes::Connection;
        }

        std::optional<int> presetEdgeKind(DiagramTypes::ArrowDefault value) {
            switch (value) {
                case DiagramTypes::AlwaysComposition:
                    return DiagramTypes::Composition;
                case DiagramTypes::AlwaysConnection:
                    return DiagramTypes::Connection;
                case DiagramTypes::AlwaysFlow:
                    return DiagramTypes::Flow;
                case DiagramTypes::AlwaysSpecialization:
                    return DiagramTypes::Specialization;
                case DiagramTypes::AskOnRelease:
                    break;
            }
            return std::nullopt;
        }

        constexpr int MaximumEntryLength = 90;

        QString elide(const QString &text, int length = MaximumEntryLength) {
            return text.size() <= length ? text : text.left(length - 1) + QChar(0x2026);
        }

        QString kindText(ElementKind kind) {
            switch (kind) {
                case ElementKind::Package:
                    return QObject::tr("package");
                case ElementKind::PartDefinition:
                    return QObject::tr("part definition");
                case ElementKind::PartUsage:
                    return QObject::tr("part");
                case ElementKind::Attribute:
                    return QObject::tr("attribute");
                case ElementKind::ActionPerformance:
                    return QObject::tr("action");
                case ElementKind::ConstraintDefinition:
                    return QObject::tr("constraint definition");
                case ElementKind::CalcDefinition:
                    return QObject::tr("calculation definition");
                case ElementKind::ConstraintUsage:
                    return QObject::tr("constraint");
                case ElementKind::CalcUsage:
                    return QObject::tr("calculation");
                case ElementKind::Textual:
                    return QObject::tr("statement");
            }
            return {};
        }

        QString kindText(const Diagram::SysMLElement &element) {
            if (element.kind() == ElementKind::Textual && !element.keyword().empty())
                return QString::fromStdString(element.keyword());
            if (isConstraintOrCalc(element.kind()) && element.declaredKeyword().has_value())
                return QString::fromStdString(*element.declaredKeyword()); // function, assert constraint, ...
            return kindText(element.kind());
        }

        /// «measurable» for metadata, "/" for derived (UML notation), other prefixes as words.
        QString decorate(const Diagram::SysMLElement &element, const QString &name) {
            QString metadata;
            QString words;
            bool derived = false;
            for (const auto &prefix: element.prefixes()) {
                const QString text = QString::fromStdString(prefix);
                if (text.startsWith(QLatin1Char('#')))
                    metadata += QStringLiteral("«") + text.mid(1) + QStringLiteral("» ");
                else if (text == QLatin1String("derived"))
                    derived = true;
                else
                    words += text + QLatin1Char(' ');
            }
            return metadata + words + (derived ? QStringLiteral("/") : QString()) + name;
        }

        QString specializationText(const Diagram::SysMLElement &element) {
            QString text;
            for (const auto &reference: element.externalSpecializations())
                text += (text.isEmpty() ? QStringLiteral(" :> ") : QStringLiteral(", ")) + QString::fromStdString(reference);
            return text;
        }

        /// Statements kept as text that are not shown in the diagram (they stay in the code).
        bool isHiddenStatement(const Diagram::SysMLElement &element) {
            if (element.kind() != ElementKind::Textual)
                return false;
            const auto &keyword = element.keyword();
            return keyword == "comment" || keyword == "doc" || keyword == "import" || keyword == "alias";
        }

        QVariantMap entry(ElementId id, const QString &text, const QString &tooltip = QString()) {
            return {{QStringLiteral("entryId"), QString::number(id)},
                    {QStringLiteral("text"), elide(text)},
                    {QStringLiteral("tooltip"), tooltip.isEmpty() && text.size() > MaximumEntryLength ? text : tooltip}};
        }

        void addCompartment(QVariantList &compartments, const QString &title, const QVariantList &entries) {
            if (!entries.isEmpty())
                compartments.append(QVariantMap{{QStringLiteral("title"), title}, {QStringLiteral("entries"), entries}});
        }

        /// Packages, definitions and parts; statements kept as text only on package level (constraint def, ...).
        bool isNodeElement(const Diagram::SemanticModel &model, ElementId id) {
            const auto &element = model.element(id);
            if (isNodeKind(element.kind()))
                return true;
            if (!(isConstraintOrCalc(element.kind()) || element.kind() == ElementKind::Textual) || isHiddenStatement(element))
                return false;
            return element.owner() == RootId || model.element(element.owner()).kind() == ElementKind::Package;
        }

        QString usageText(const Diagram::SemanticModel &model, ElementId id) {
            const auto &element = model.element(id);
            QString text = QString::fromStdString(element.name());
            if (!element.typeName().empty())
                text += QStringLiteral(" : ") + QString::fromStdString(element.typeName());
            if (element.multiplicity().has_value())
                text += QString::fromStdString(element.multiplicity()->toString());
            return text;
        }

        QString valueText(const Diagram::SysMLElement &element) {
            if (element.value().empty())
                return {};
            return QLatin1Char(' ') + QString::fromStdString(element.valueOperator()) + QLatin1Char(' ') +
                   QString::fromStdString(element.value());
        }

        QString edgeKey(bool composition, std::uint64_t id) {
            return (composition ? QStringLiteral("c:") : QStringLiteral("r:")) + QString::number(id);
        }

        bool isFeatureTool(int tool) {
            return tool == DiagramTypes::ToolAttribute || tool == DiagramTypes::ToolAction ||
                   tool == DiagramTypes::ToolConstraint || tool == DiagramTypes::ToolCalc;
        }

        /// Keyword shown above the name: «constraint def», «function», «assert constraint», ...
        QString keywordOf(const Diagram::SysMLElement &element) {
            std::string keyword;
            switch (element.kind()) {
                case ElementKind::ConstraintDefinition:
                    keyword = "constraint def";
                    break;
                case ElementKind::CalcDefinition:
                    keyword = "calc def";
                    break;
                case ElementKind::ConstraintUsage:
                    keyword = "constraint";
                    break;
                case ElementKind::CalcUsage:
                    keyword = "calc";
                    break;
                default:
                    break;
            }
            if (element.declaredKeyword().has_value() && !element.declaredKeyword()->empty())
                keyword = *element.declaredKeyword();
            return QStringLiteral("«") + QString::fromStdString(keyword) + QStringLiteral("»");
        }
    }

    DiagramController::DiagramController(Diagram::SemanticModel &model, QObject *parent) :
            QObject(parent),
            Model(model),
            Nodes(new DiagramNodeModel(this)),
            Edges(new DiagramEdgeModel(this)) {
        ListenerToken = Model.addListener([this](const Diagram::ModelChange &) { scheduleSync(); });
        connect(Nodes, &DiagramNodeModel::geometryChanged, this, &DiagramController::updateCanvasSize);
        syncNow();
    }

    DiagramController::~DiagramController() {
        Model.removeListener(ListenerToken);
    }

    // --- properties --------------------------------------------------------------------------------

    void DiagramController::setArrowMode(bool enabled) {
        if (ArrowMode == enabled)
            return;
        ArrowMode = enabled;
        if (enabled)
            emit statusMessage(tr("Drag from one element to another to connect them. Esc leaves the arrow mode."));
        emit arrowModeChanged();
    }

    void DiagramController::setArrowDefault(int value) {
        const auto preset = static_cast<DiagramTypes::ArrowDefault>(
                std::clamp(value, int(DiagramTypes::AskOnRelease), int(DiagramTypes::AlwaysSpecialization)));
        if (preset == ArrowDefaultValue)
            return;
        ArrowDefaultValue = preset;
        emit arrowDefaultChanged();
    }

    // --- adding ------------------------------------------------------------------------------------

    QString DiagramController::addElement(int tool, qreal x, qreal y) {
        AutoLayout = false;
        const QPointF point(x, y);
        if (isFeatureTool(tool)) {
            const auto partId = toId(nodeIdAt(point, std::nullopt, false));
            if (!partId || !acceptsFeature(Model.element(*partId).kind(), tool)) {
                emit statusMessage(tool == DiagramTypes::ToolAction
                                   ? tr("Drop actions onto a part or part definition.")
                                   : tr("Drop attributes, constraints and calculations onto a part, a part definition, "
                                        "a constraint or a calculation."));
                return {};
            }
            const auto id = addFeature(tool, *partId);
            return id ? QString::number(*id) : QString();
        }
        if (tool == DiagramTypes::ToolStatement)
            return addStatement(point);
        if (tool != DiagramTypes::ToolPart && tool != DiagramTypes::ToolPartDefinition && tool != DiagramTypes::ToolPackage &&
            tool != DiagramTypes::ToolConstraintDefinition && tool != DiagramTypes::ToolCalcDefinition)
            return {};

        const ElementKind kind = tool == DiagramTypes::ToolPart ? ElementKind::PartUsage
                                 : tool == DiagramTypes::ToolPartDefinition ? ElementKind::PartDefinition
                                 : tool == DiagramTypes::ToolConstraintDefinition ? ElementKind::ConstraintDefinition
                                 : tool == DiagramTypes::ToolCalcDefinition ? ElementKind::CalcDefinition
                                 : ElementKind::Package;

        // Dropped into a frame: the package owns the new element.
        ElementId owner = RootId;
        if (const auto frame = frameAt(point, std::nullopt); frame && RelationshipRules::canOwn(Model, *frame, kind))
            owner = *frame;

        const std::string base = kind == ElementKind::PartUsage ? "part"
                                 : kind == ElementKind::PartDefinition ? "Part"
                                 : kind == ElementKind::ConstraintDefinition ? "Constraint"
                                 : kind == ElementKind::CalcDefinition ? "Calc"
                                 : "Package";
        const std::string name = uniqueName(base, owner);
        std::optional<ElementId> id;
        const QPointF offset = kind == ElementKind::Package ? QPointF(20, 15) : QPointF(70, 25);
        if (!runModelChange([&] {
                id = Model.addElement(kind, name, owner);
                PendingPositions[*id] = QPointF(std::max<qreal>(0, x - offset.x()), std::max<qreal>(0, y - offset.y()));
                if (isConstraintOrCalc(kind))
                    fillNewEvaluation(*id);
            }))
            return {};

        SelectedNodes = {*id};
        SelectedEdges.clear();
        applySelection();
        emit statusMessage(tr("Added %1 '%2' - double-click to edit.").arg(kindText(kind), QString::fromStdString(name)));
        return QString::number(*id);
    }

    QString DiagramController::addStatement(const QPointF &point) {
        // Into the part / constraint / calculation under the mouse, else into the package frame, else top level.
        ElementId owner = RootId;
        if (const auto node = toId(nodeIdAt(point, std::nullopt, false));
            node && RelationshipRules::canOwn(Model, *node, ElementKind::Textual) && !isFrame(*node))
            owner = *node;
        else if (const auto frame = frameAt(point, std::nullopt); frame && RelationshipRules::canOwn(Model, *frame, ElementKind::Textual))
            owner = *frame;
        const std::string name = uniqueName("Requirement", owner);
        const std::string text = "requirement def " + name + " {\n    doc /* describe the requirement */\n}";
        std::optional<ElementId> id;
        if (!runModelChange([&] {
                id = Model.addElement(ElementKind::Textual, name, owner);
                Model.setText(*id, text, "requirement def", name);
                if (owner == RootId || Model.element(owner).kind() == ElementKind::Package)
                    PendingPositions[*id] = QPointF(std::max<qreal>(0, point.x() - 70), std::max<qreal>(0, point.y() - 25));
            }))
            return {};
        emit statusMessage(tr("Added a SysML statement - write any SysML v2 text into it."));
        return QString::number(*id);
    }

    std::optional<ElementId> DiagramController::addFeatureTo(int tool, ElementId owner) {
        if (!Model.contains(owner) || !acceptsFeature(Model.element(owner).kind(), tool))
            return std::nullopt;
        return addFeature(tool, owner);
    }

    QString DiagramController::addElementAtSelection(int tool, qreal x, qreal y) {
        if (tool == DiagramTypes::ToolStatement && SelectedNodes.size() == 1) {
            const ElementId selected = *SelectedNodes.begin();
            if (Model.contains(selected) && !isFrame(selected)) {
                const QRectF rect = Nodes->find(selected) ? Nodes->find(selected)->Rect : QRectF();
                return addStatement(rect.center());
            }
        }
        if (!isFeatureTool(tool))
            return addElement(tool, x, y);

        std::optional<ElementId> selectedPart;
        for (const ElementId id: SelectedNodes) {
            if (!Model.contains(id) || !acceptsFeature(Model.element(id).kind(), tool))
                continue;
            if (selectedPart) {
                selectedPart.reset();
                break;
            }
            selectedPart = id;
        }
        if (!selectedPart) {
            emit statusMessage(tr("Select exactly one part first, or drag the entry onto a part."));
            return {};
        }
        const auto id = addFeature(tool, *selectedPart);
        return id ? QString::number(*id) : QString();
    }

    std::optional<ElementId> DiagramController::addFeature(int tool, ElementId owner) {
        const bool evaluationOwner = isConstraintOrCalc(Model.element(owner).kind());
        ElementKind kind = ElementKind::Attribute;
        std::string base = "attribute";
        if (tool == DiagramTypes::ToolAction) {
            kind = ElementKind::ActionPerformance;
            base = "action";
        } else if (tool == DiagramTypes::ToolConstraint) {
            kind = ElementKind::ConstraintUsage;
            base = "constraint";
        } else if (tool == DiagramTypes::ToolCalc) {
            kind = ElementKind::CalcUsage;
            base = "calc";
        } else if (evaluationOwner) {
            base = "x"; // a parameter
        }
        const std::string name = uniqueName(base, owner);
        std::optional<ElementId> id;
        if (!runModelChange([&] {
                id = Model.addElement(kind, name, owner);
                if (kind == ElementKind::Attribute && evaluationOwner) {
                    Model.setPrefixes(*id, {"in"});
                    Model.setDeclaredKeyword(*id, std::string()); // in x : Real;
                    Model.setTypeName(*id, "Real");
                } else if (isConstraintOrCalc(kind)) {
                    fillNewEvaluation(*id);
                }
            }))
            return std::nullopt;
        const QString what = kind == ElementKind::Attribute && evaluationOwner ? tr("parameter") : kindText(kind);
        emit statusMessage(tr("Added %1 '%2' - double-click it to edit.").arg(what, QString::fromStdString(name)));
        return id;
    }

    void DiagramController::fillNewEvaluation(ElementId id) {
        // Starting point that is valid SysML: one parameter and an expression using it.
        const ElementKind kind = Model.element(id).kind();
        if (kind == ElementKind::ConstraintUsage) {
            Model.setDeclaredKeyword(id, std::string("assert constraint"));
            Model.setExpression(id, "true");
            return;
        }
        const ElementId parameter = Model.addElement(ElementKind::Attribute, "x", id);
        Model.setPrefixes(parameter, {"in"});
        Model.setDeclaredKeyword(parameter, std::string());
        Model.setTypeName(parameter, "Real");
        if (Diagram::isConstraintKind(kind)) {
            Model.setExpression(id, "x >= 0.0");
            return;
        }
        const ElementId result = Model.addElement(ElementKind::Attribute, "result", id);
        Model.setPrefixes(result, {"return"});
        Model.setDeclaredKeyword(result, std::string());
        Model.setTypeName(result, "Real");
        Model.setValue(result, "=", "x");
    }

    // --- lookup ------------------------------------------------------------------------------------

    std::optional<ElementId> DiagramController::toId(const QString &id) const {
        bool ok = false;
        const auto value = id.toULongLong(&ok);
        if (!ok || value == RootId || !Model.contains(value))
            return std::nullopt;
        return ElementId(value);
    }

    bool DiagramController::isFrame(ElementId id) const {
        return Model.contains(id) && Model.element(id).kind() == ElementKind::Package;
    }

    QString DiagramController::nodeAt(qreal x, qreal y, bool includeFrameArea) const {
        return nodeIdAt(QPointF(x, y), std::nullopt, includeFrameArea);
    }

    QString DiagramController::nodeIdAt(const QPointF &point, std::optional<ElementId> ignore, bool includeFrameArea) const {
        // Parts are drawn above frames; among them the deepest (drawn last) wins.
        const DiagramNode *best = nullptr;
        for (const auto &node: Nodes->nodes()) {
            if (node.Kind == DiagramTypes::PackageNode || (ignore && node.Id == *ignore) || !node.Rect.contains(point))
                continue;
            if (!best || node.Depth >= best->Depth)
                best = &node;
        }
        if (best)
            return QString::number(best->Id);

        // Frames only at their border and title band, so the content inside stays reachable.
        for (const auto &node: Nodes->nodes()) {
            if (node.Kind != DiagramTypes::PackageNode || (ignore && node.Id == *ignore))
                continue;
            const QRectF outer = node.Rect.adjusted(-FrameBorderGrip, -FrameBorderGrip, FrameBorderGrip, FrameBorderGrip);
            const QRectF inner = node.Rect.adjusted(FrameBorderGrip, FrameTitleHeight, -FrameBorderGrip, -FrameBorderGrip);
            if (outer.contains(point) && !inner.contains(point) && (!best || node.Depth >= best->Depth))
                best = &node;
        }
        if (best)
            return QString::number(best->Id);

        if (includeFrameArea) {
            if (const auto frame = frameAt(point, ignore))
                return QString::number(*frame);
        }
        return {};
    }

    std::optional<ElementId> DiagramController::frameAt(const QPointF &point, std::optional<ElementId> ignore) const {
        std::optional<ElementId> best;
        qreal bestArea = 0;
        for (const auto &node: Nodes->nodes()) {
            if (node.Kind != DiagramTypes::PackageNode || (ignore && node.Id == *ignore) || !node.Rect.contains(point))
                continue;
            const qreal area = node.Rect.width() * node.Rect.height();
            if (!best || area < bestArea) {
                best = node.Id;
                bestArea = area;
            }
        }
        return best;
    }

    QString DiagramController::edgeAt(qreal x, qreal y) const {
        const QPointF point(x, y);
        const auto &edges = Edges->edges();
        for (auto it = edges.rbegin(); it != edges.rend(); ++it) {
            const auto *source = Nodes->find(it->Source);
            const auto *target = Nodes->find(it->Target);
            if (!source || !target)
                continue;
            const QLineF line = DiagramGeometry::edgeLine(source->Rect, target->Rect);
            if (DiagramGeometry::distanceToSegment(point, line) <= EdgeHitDistance)
                return it->Key;
        }
        return {};
    }

    QRectF DiagramController::nodeRect(const QString &id) const {
        const auto elementId = toId(id);
        const auto *node = elementId ? Nodes->find(*elementId) : nullptr;
        return node ? node->Rect : QRectF();
    }

    // --- geometry ----------------------------------------------------------------------------------

    void DiagramController::moveNode(const QString &id, qreal x, qreal y) {
        const auto elementId = toId(id);
        if (!elementId)
            return;
        const int row = Nodes->indexOf(*elementId);
        if (row < 0)
            return;
        const QRectF old = Nodes->nodes()[std::size_t(row)].Rect;
        const QPointF position(std::max<qreal>(0, x), std::max<qreal>(0, y));
        const QPointF delta = position - old.topLeft();
        if (delta.isNull())
            return;
        AutoLayout = false;
        Nodes->setRect(row, QRectF(position, old.size()));
        if (isFrame(*elementId))
            moveFrameContent(*elementId, delta);
    }

    void DiagramController::moveFrameContent(ElementId frame, const QPointF &delta) {
        for (const ElementId child: Model.children(frame)) {
            const int row = Nodes->indexOf(child);
            if (row < 0)
                continue;
            Nodes->setRect(row, Nodes->nodes()[std::size_t(row)].Rect.translated(delta));
            if (isFrame(child))
                moveFrameContent(child, delta);
        }
    }

    void DiagramController::finishMove(const QString &id) {
        if (const auto elementId = toId(id))
            updateContainment(*elementId);
    }

    void DiagramController::updateContainment(ElementId id) {
        if (!Model.contains(id))
            return;
        const ElementId owner = Model.element(id).owner();
        if (owner != RootId && isPartLike(Model.element(owner).kind()))
            return; // owned by a part (composition): the placement does not change the ownership

        const auto *node = Nodes->find(id);
        if (!node)
            return;
        auto frame = frameAt(node->Rect.center(), id);
        if (frame && Model.isAncestor(id, *frame))
            frame.reset();
        const ElementId newOwner = frame ? *frame : RootId;
        if (newOwner == owner || !RelationshipRules::canReparent(Model, id, newOwner))
            return;
        if (runModelChange([&] { Model.reparent(id, newOwner); })) {
            emit statusMessage(frame ? tr("Moved into package '%1'.").arg(QString::fromStdString(Model.element(newOwner).name()))
                                     : tr("Moved out of the package."));
        }
    }

    void DiagramController::setNodeSize(const QString &id, qreal width, qreal height) {
        const auto elementId = toId(id);
        if (!elementId || isFrame(*elementId) || width <= 0 || height <= 0)
            return;
        const int row = Nodes->indexOf(*elementId);
        if (row < 0)
            return;
        const QRectF old = Nodes->nodes()[std::size_t(row)].Rect;
        Measured.insert(*elementId);
        if (old.size() == QSizeF(width, height))
            return;
        Nodes->setRect(row, QRectF(old.topLeft(), QSizeF(width, height)));
        if (AutoLayout && !LayoutQueued) {
            // Real sizes arrived after an automatic layout: arrange again with them (once per batch).
            LayoutQueued = true;
            QMetaObject::invokeMethod(this, [this] {
                LayoutQueued = false;
                if (AutoLayout)
                    layoutAll();
            }, Qt::QueuedConnection);
        }
    }

    void DiagramController::resizeFrame(const QString &id, qreal width, qreal height) {
        const auto elementId = toId(id);
        if (!elementId || !isFrame(*elementId))
            return;
        const int row = Nodes->indexOf(*elementId);
        if (row < 0)
            return;
        const QRectF old = Nodes->nodes()[std::size_t(row)].Rect;
        AutoLayout = false;
        Nodes->setRect(row, QRectF(old.topLeft(), QSizeF(std::max(width, MinimumFrameWidth),
                                                         std::max(height, MinimumFrameHeight))));
    }

    void DiagramController::updateCanvasSize() {
        qreal right = 0;
        qreal bottom = 0;
        for (const auto &node: Nodes->nodes()) {
            right = std::max(right, node.Rect.right());
            bottom = std::max(bottom, node.Rect.bottom());
        }
        const QSizeF size(std::max<qreal>(2400, right + CanvasMargin), std::max<qreal>(1600, bottom + CanvasMargin));
        if (size == CanvasSize)
            return;
        CanvasSize = size;
        emit canvasSizeChanged();
    }

    // --- selection and deleting --------------------------------------------------------------------

    void DiagramController::select(const QString &id, bool additive) {
        const auto elementId = toId(id);
        if (!elementId)
            return;
        if (!additive) {
            SelectedNodes = {*elementId};
            SelectedEdges.clear();
        } else if (!SelectedNodes.erase(*elementId)) {
            SelectedNodes.insert(*elementId);
        }
        applySelection();
    }

    void DiagramController::selectEdge(const QString &key, bool additive) {
        if (!Edges->find(key))
            return;
        if (!additive) {
            SelectedNodes.clear();
            SelectedEdges = {key};
        } else if (!SelectedEdges.erase(key)) {
            SelectedEdges.insert(key);
        }
        applySelection();
    }

    void DiagramController::clearSelection() {
        if (SelectedNodes.empty() && SelectedEdges.empty())
            return;
        SelectedNodes.clear();
        SelectedEdges.clear();
        applySelection();
    }

    void DiagramController::applySelection() {
        for (auto it = SelectedNodes.begin(); it != SelectedNodes.end();)
            it = Nodes->indexOf(*it) < 0 ? SelectedNodes.erase(it) : std::next(it);
        for (auto it = SelectedEdges.begin(); it != SelectedEdges.end();)
            it = Edges->find(*it) == nullptr ? SelectedEdges.erase(it) : std::next(it);

        for (int row = 0; row < Nodes->count(); ++row)
            Nodes->setSelected(row, SelectedNodes.count(Nodes->nodes()[std::size_t(row)].Id) > 0);

        auto edges = Edges->edges();
        for (auto &edge: edges)
            edge.Selected = SelectedEdges.count(edge.Key) > 0;
        Edges->setEdges(std::move(edges));
        emit selectionChanged();
    }

    void DiagramController::deleteSelection() {
        if (!hasSelection())
            return;
        const std::set<QString> edges = SelectedEdges;
        const std::set<ElementId> nodes = SelectedNodes;
        for (const auto &key: edges)
            deleteEdge(key);
        for (const ElementId id: nodes) {
            if (Model.contains(id)) // may already be gone together with its owner
                runModelChange([&] { Model.removeElement(id); });
        }
        clearSelection();
        emit statusMessage(tr("Deleted. Owned elements were deleted with their owner."));
    }

    void DiagramController::deleteElement(const QString &id) {
        if (const auto elementId = toId(id))
            runModelChange([&] { Model.removeElement(*elementId); });
    }

    void DiagramController::deleteEdge(const QString &key) {
        const auto *edge = Edges->find(key);
        if (!edge)
            return;
        if (edge->Kind == DiagramTypes::Composition) {
            // Removing ownership: the part moves to the package (frame) it lies in, or to the top level.
            const ElementId child = edge->Target;
            if (!Model.contains(child))
                return;
            ElementId newOwner = RootId;
            if (const auto *node = Nodes->find(child)) {
                if (const auto frame = frameAt(node->Rect.center(), child))
                    newOwner = *frame;
            }
            runModelChange([&] { Model.reparent(child, newOwner); });
            return;
        }
        bool ok = false;
        const auto relationship = key.mid(2).toULongLong(&ok);
        if (ok && Model.containsRelationship(relationship))
            runModelChange([&] { Model.removeRelationship(relationship); });
    }

    // --- edges -------------------------------------------------------------------------------------

    QVariantList DiagramController::allowedEdgeKinds(const QString &source, const QString &target) const {
        QVariantList kinds;
        const auto sourceId = toId(source);
        const auto targetId = toId(target);
        if (!sourceId || !targetId || *sourceId == *targetId)
            return kinds;

        if (isPartLike(Model.element(*sourceId).kind()) && Model.element(*targetId).kind() == ElementKind::PartUsage &&
            Model.element(*targetId).owner() != *sourceId && RelationshipRules::canReparent(Model, *targetId, *sourceId))
            kinds.append(int(DiagramTypes::Composition));

        for (const int kind: {int(DiagramTypes::Connection), int(DiagramTypes::Flow), int(DiagramTypes::Specialization)}) {
            if (RelationshipRules::canRelate(Model, *toRelationshipKind(kind), *sourceId, *targetId))
                kinds.append(kind);
        }
        return kinds;
    }

    bool DiagramController::createEdge(int kind, const QString &source, const QString &target) {
        const auto sourceId = toId(source);
        const auto targetId = toId(target);
        if (!sourceId || !targetId || kind < DiagramTypes::Composition || kind > DiagramTypes::Specialization)
            return false;
        const bool ok = runModelChange([&] {
            if (kind == DiagramTypes::Composition)
                Model.reparent(*targetId, *sourceId);
            else
                Model.addRelationship(*toRelationshipKind(kind), *sourceId, *targetId);
        });
        if (ok)
            emit statusMessage(tr("%1 created.").arg(edgeKindName(kind)));
        return ok;
    }

    QVariantMap DiagramController::arrowReleased(const QString &source, qreal x, qreal y) {
        const auto sourceId = toId(source);
        if (!sourceId)
            return {};
        const QString target = nodeIdAt(QPointF(x, y), sourceId, false);
        if (target.isEmpty()) {
            emit statusMessage(tr("Release the arrow over another element."));
            return {};
        }
        const QVariantList kinds = allowedEdgeKinds(source, target);

        if (const auto preset = presetEdgeKind(ArrowDefaultValue)) {
            if (kinds.contains(*preset)) {
                createEdge(*preset, source, target);
            } else if (const auto relationship = toRelationshipKind(*preset)) {
                const auto targetId = toId(target);
                emit statusMessage(QString::fromStdString(
                        RelationshipRules::canRelate(Model, *relationship, *sourceId, *targetId).Reason));
            } else {
                emit statusMessage(tr("Composition is not possible here (a part can only be owned by a part)."));
            }
            return {};
        }

        if (kinds.isEmpty()) {
            emit statusMessage(tr("No relationship is possible between these elements."));
            return {};
        }
        return {{QStringLiteral("target"), target}, {QStringLiteral("kinds"), kinds}};
    }

    QString DiagramController::edgeKindName(int kind) const {
        switch (kind) {
            case DiagramTypes::Composition:
                return tr("Composition");
            case DiagramTypes::Connection:
                return tr("Connect");
            case DiagramTypes::Flow:
                return tr("Flow");
            case DiagramTypes::Specialization:
                return tr("Specialization");
            case DiagramTypes::Binding:
                return tr("Binding");
            default:
                return {};
        }
    }

    // --- editing -----------------------------------------------------------------------------------

    QString DiagramController::kindLabel(const QString &id) const {
        const auto elementId = toId(id);
        return elementId ? kindText(Model.element(*elementId)) : QString();
    }

    QVariantMap DiagramController::elementDetails(const QString &id) const {
        const auto elementId = toId(id);
        if (!elementId)
            return {};
        const auto &element = Model.element(*elementId);
        const ElementKind kind = element.kind();
        QString multiplicity;
        if (element.multiplicity().has_value()) {
            multiplicity = QString::fromStdString(element.multiplicity()->toString());
            multiplicity = multiplicity.mid(1, multiplicity.size() - 2); // without [ ]
        }
        QStringList prefixes;
        for (const auto &prefix: element.prefixes())
            prefixes.append(QString::fromStdString(prefix));
        QString value;
        if (!element.value().empty()) {
            value = QString::fromStdString(element.value());
            if (element.valueOperator() != "=")
                value = QString::fromStdString(element.valueOperator()) + QLatin1Char(' ') + value;
        }
        const bool evaluation = isConstraintOrCalc(kind);
        const bool usage = kind == ElementKind::PartUsage || kind == ElementKind::Attribute ||
                           kind == ElementKind::ActionPerformance || kind == ElementKind::ConstraintUsage ||
                           kind == ElementKind::CalcUsage;
        return {
            {QStringLiteral("hasExpression"), evaluation},
            {QStringLiteral("expression"), QString::fromStdString(element.expression())},
            {QStringLiteral("isConstraintUsage"), kind == ElementKind::ConstraintUsage},
            {QStringLiteral("assertion"), element.declaredKeyword().has_value() &&
                                          *element.declaredKeyword() == "assert constraint"},
            {QStringLiteral("isConstraint"), Diagram::isConstraintKind(kind)},
            {QStringLiteral("kindLabel"), kindText(element)},
            {QStringLiteral("name"), QString::fromStdString(element.name())},
            {QStringLiteral("typeName"), QString::fromStdString(element.typeName())},
            {QStringLiteral("multiplicity"), multiplicity},
            {QStringLiteral("typed"), usage},
            {QStringLiteral("hasMultiplicity"), kind == ElementKind::PartUsage || kind == ElementKind::Attribute},
            {QStringLiteral("hasValue"), kind == ElementKind::PartUsage || kind == ElementKind::Attribute},
            {QStringLiteral("value"), value},
            {QStringLiteral("prefixes"), prefixes.join(QLatin1Char(' '))},
            {QStringLiteral("isAction"), kind == ElementKind::ActionPerformance},
            {QStringLiteral("isTextual"), kind == ElementKind::Textual},
            {QStringLiteral("text"), QString::fromStdString(element.text())},
            {QStringLiteral("referenceAction"), element.performStyle() == Diagram::PerformStyle::ReferenceAction},
            {QStringLiteral("typePlaceholder"), kind == ElementKind::Attribute ? QStringLiteral("ISQ::MassValue")
                                                : kind == ElementKind::ConstraintUsage ? QStringLiteral("NonNegativeReal")
                                                : kind == ElementKind::CalcUsage ? QStringLiteral("Power")
                                                : QStringLiteral("Engine")}
        };
    }

    QString DiagramController::updateElement(const QString &id, const QVariantMap &values) {
        const auto elementId = toId(id);
        if (!elementId)
            return tr("The element does not exist anymore.");
        const ElementKind kind = Model.element(*elementId).kind();

        if (kind == ElementKind::Textual) {
            QString text = values.value(QStringLiteral("text")).toString();
            while (text.endsWith(QLatin1Char('\n')) || text.endsWith(QLatin1Char(' ')))
                text.chop(1);
            if (text.trimmed().isEmpty())
                return tr("The text must not be empty.");
            if (text.count(QLatin1Char('{')) != text.count(QLatin1Char('}')))
                return tr("The braces { } are not balanced.");
            const auto [keyword, name] = SysMLTextImporter::describeStatement(text.toStdString());
            QString error;
            try {
                Model.setText(*elementId, text.toStdString(), keyword, name);
            } catch (const std::exception &exception) {
                error = QString::fromStdString(exception.what());
            }
            syncNow();
            return error;
        }

        const std::string name = values.value(QStringLiteral("name")).toString().trimmed().toStdString();
        if (name.empty())
            return tr("The name must not be empty.");
        const auto [multiplicityValid, multiplicity] = parseMultiplicity(values.value(QStringLiteral("multiplicity")).toString());
        if (!multiplicityValid)
            return tr("Multiplicity must look like 6, 0..1 or 1..*");

        const std::string typeName = values.value(QStringLiteral("typeName")).toString().trimmed().toStdString();
        const bool referenceAction = values.value(QStringLiteral("referenceAction")).toBool();
        std::vector<std::string> prefixes;
        for (const auto &prefix: values.value(QStringLiteral("prefixes")).toString().split(QLatin1Char(' '), Qt::SkipEmptyParts))
            prefixes.push_back(prefix.toStdString());
        // "decode(x)" (operator "="), ":= decode(x)" or "default = 3"
        QString valueText = values.value(QStringLiteral("value")).toString().trimmed();
        QString valueOperator = QStringLiteral("=");
        for (const QString &candidate: {QStringLiteral("default :="), QStringLiteral("default ="), QStringLiteral(":="),
                                       QStringLiteral("default"), QStringLiteral("=")}) {
            if (valueText.startsWith(candidate + QLatin1Char(' ')) || valueText == candidate) {
                valueOperator = candidate;
                valueText = valueText.mid(candidate.size()).trimmed();
                break;
            }
        }

        QString expression;
        if (isConstraintOrCalc(kind)) {
            expression = values.value(QStringLiteral("expression")).toString();
            while (expression.endsWith(QLatin1Char('\n')) || expression.endsWith(QLatin1Char(' ')))
                expression.chop(1);
            if (expression.count(QLatin1Char('{')) != expression.count(QLatin1Char('}')) ||
                expression.count(QLatin1Char('(')) != expression.count(QLatin1Char(')')) ||
                expression.count(QLatin1Char('[')) != expression.count(QLatin1Char(']')))
                return tr("The brackets in the expression are not balanced.");
            const QString trimmed = expression.trimmed();
            if (trimmed.endsWith(QLatin1Char(';')))
                return tr("The expression is written without ';' at its end.");
        }

        QString error;
        try {
            if (name != Model.element(*elementId).name())
                Model.rename(*elementId, name);
            if (kind == ElementKind::PartUsage || kind == ElementKind::Attribute || kind == ElementKind::ActionPerformance ||
                kind == ElementKind::ConstraintUsage || kind == ElementKind::CalcUsage)
                Model.setTypeName(*elementId, typeName);
            if (isConstraintOrCalc(kind) && values.contains(QStringLiteral("expression")))
                Model.setExpression(*elementId, expression.toStdString());
            if (kind == ElementKind::ConstraintUsage && values.contains(QStringLiteral("assertion"))) {
                const bool assertion = values.value(QStringLiteral("assertion")).toBool();
                Model.setDeclaredKeyword(*elementId, assertion ? std::optional<std::string>("assert constraint")
                                                               : std::nullopt);
            }
            if (kind == ElementKind::PartUsage || kind == ElementKind::Attribute) {
                Model.setMultiplicity(*elementId, multiplicity);
                Model.setValue(*elementId, valueOperator.toStdString(), valueText.toStdString());
            }
            if (kind == ElementKind::ActionPerformance)
                Model.setPerformStyle(*elementId, referenceAction ? Diagram::PerformStyle::ReferenceAction
                                                                  : Diagram::PerformStyle::DeclareAction);
            if (values.contains(QStringLiteral("prefixes")))
                Model.setPrefixes(*elementId, prefixes);
        } catch (const std::exception &exception) {
            error = QString::fromStdString(exception.what());
        }
        syncNow();
        return error;
    }

    std::pair<bool, std::optional<Multiplicity>> DiagramController::parseMultiplicity(const QString &input) {
        QString text = input.trimmed();
        if (text.startsWith(QLatin1Char('[')) && text.endsWith(QLatin1Char(']')))
            text = text.mid(1, text.size() - 2).trimmed();
        if (text.isEmpty())
            return {true, std::nullopt};
        if (text == QLatin1String("*"))
            return {true, Multiplicity::range(0, std::nullopt)};

        const auto parseNumber = [](const QString &value, bool &ok) -> std::size_t {
            return static_cast<std::size_t>(value.trimmed().toULongLong(&ok));
        };

        const auto separator = text.indexOf(QLatin1String(".."));
        bool ok = false;
        if (separator < 0) {
            const std::size_t count = parseNumber(text, ok);
            if (!ok)
                return {false, std::nullopt};
            return {true, Multiplicity::exactly(count)};
        }

        const std::size_t lower = parseNumber(text.left(separator), ok);
        if (!ok)
            return {false, std::nullopt};
        const QString upperText = text.mid(separator + 2).trimmed();
        if (upperText == QLatin1String("*"))
            return {true, Multiplicity::range(lower, std::nullopt)};
        const std::size_t upper = parseNumber(upperText, ok);
        if (!ok || upper < lower)
            return {false, std::nullopt};
        return {true, Multiplicity::range(lower, upper)};
    }

    // --- model -> node and edge models -------------------------------------------------------------

    bool DiagramController::runModelChange(const std::function<void()> &change) {
        bool ok = true;
        try {
            change();
        } catch (const std::exception &exception) {
            ok = false;
            emit statusMessage(QString::fromStdString(exception.what()));
        }
        syncNow();
        return ok;
    }

    void DiagramController::scheduleSync() {
        if (SyncQueued)
            return;
        SyncQueued = true;
        QMetaObject::invokeMethod(this, [this] {
            if (SyncQueued)
                syncNow();
        }, Qt::QueuedConnection);
    }

    void DiagramController::syncNow() {
        SyncQueued = false;

        std::vector<ElementId> nodeIds;
        std::function<void(ElementId)> collect = [&](ElementId owner) {
            for (const ElementId child: Model.children(owner)) {
                if (!isNodeElement(Model, child))
                    continue;
                nodeIds.push_back(child);
                collect(child);
            }
        };
        collect(RootId);
        const std::set<ElementId> wanted(nodeIds.begin(), nodeIds.end());

        for (int row = Nodes->count() - 1; row >= 0; --row) {
            const auto id = Nodes->nodes()[std::size_t(row)].Id;
            if (wanted.count(id) == 0) {
                Measured.erase(id);
                Nodes->removeAt(row);
            }
        }

        for (const ElementId id: nodeIds) {
            const auto &element = Model.element(id);
            DiagramNode node;
            node.Id = id;
            node.Depth = int(Model.ownershipPath(id).size()) - 1;
            fillNode(node, element);
            const int row = Nodes->indexOf(id);
            if (row >= 0) {
                Nodes->updateContent(row, node);
                continue;
            }
            const bool frame = node.Kind == DiagramTypes::PackageNode;
            const QSizeF size = frame ? QSizeF(DefaultFrameWidth, DefaultFrameHeight) : QSizeF(DefaultPartWidth, DefaultPartHeight);
            const auto pending = PendingPositions.find(id);
            const QPointF position = pending != PendingPositions.end() ? pending->second : defaultPositionFor(id);
            if (pending != PendingPositions.end())
                PendingPositions.erase(pending);
            node.Rect = QRectF(position, size);
            Nodes->append(node);
        }

        // Edges: ownership of a part by a part is drawn as composition, the rest are relationships.
        std::vector<DiagramEdge> edges;
        for (const ElementId id: nodeIds) {
            const ElementId owner = Model.element(id).owner();
            if (owner == RootId || !isPartLike(Model.element(owner).kind()))
                continue;
            DiagramEdge edge;
            edge.Kind = DiagramTypes::Composition;
            edge.Source = owner;
            edge.Target = id;
            edge.Key = edgeKey(true, id);
            edge.Selected = SelectedEdges.count(edge.Key) > 0;
            edges.push_back(edge);
        }
        for (const auto &relationship: Model.relationships()) {
            if (wanted.count(relationship.Source) == 0 || wanted.count(relationship.Target) == 0)
                continue;
            DiagramEdge edge;
            edge.Kind = toEdgeKind(relationship.Kind);
            edge.Source = relationship.Source;
            edge.Target = relationship.Target;
            edge.Key = edgeKey(false, relationship.Id);
            edge.Selected = SelectedEdges.count(edge.Key) > 0;
            edges.push_back(edge);
        }
        Edges->setEdges(std::move(edges));

        // Drop selections of elements that are gone.
        const bool selectionGone =
                std::any_of(SelectedNodes.begin(), SelectedNodes.end(), [&](ElementId id) { return wanted.count(id) == 0; }) ||
                std::any_of(SelectedEdges.begin(), SelectedEdges.end(), [&](const QString &key) { return !Edges->find(key); });
        if (selectionGone)
            applySelection();

        if (LastElementCount != int(Model.elementCount())) {
            LastElementCount = int(Model.elementCount());
            emit elementCountChanged();
        }
    }

    void DiagramController::fillNode(DiagramNode &node, const Diagram::SysMLElement &element) const {
        const ElementId id = element.id();
        const QString name = QString::fromStdString(element.name());
        switch (element.kind()) {
            case ElementKind::Package:
                node.Kind = DiagramTypes::PackageNode;
                node.Title = decorate(element, QStringLiteral("package ") + name);
                return;
            case ElementKind::Textual: {
                // any other statement on package level: a node with what can be read from its text
                const auto summary = SysMLTextImporter::summarize(element);
                node.Kind = DiagramTypes::TextualNode;
                node.Keyword = QStringLiteral("«") + QString::fromStdString(element.keyword()) + QStringLiteral("»");
                node.Title = name.isEmpty() ? QString::fromStdString(element.keyword()) : name;
                const QString fullText = QString::fromStdString(element.text());
                QVariantList parameters;
                for (const auto &parameter: summary.Parameters)
                    parameters.append(entry(id, QString::fromStdString(parameter), fullText));
                addCompartment(node.Compartments, QStringLiteral("parameters"), parameters);
                if (summary.Kind == Diagram::StatementSummary::Category::Function) {
                    QString result = QString::fromStdString(summary.Result);
                    if (!summary.Expression.empty())
                        result += QStringLiteral(" = ") + QString::fromStdString(summary.Expression);
                    if (!result.isEmpty())
                        addCompartment(node.Compartments, QStringLiteral("result"), {entry(id, result, fullText)});
                } else if (summary.Kind == Diagram::StatementSummary::Category::Constraint) {
                    if (!summary.Expression.empty())
                        addCompartment(node.Compartments, QStringLiteral("constraint"),
                                       {entry(id, QString::fromStdString(summary.Expression), fullText)});
                } else {
                    QVariantList lines;
                    const auto textLines = fullText.split(QLatin1Char('\n'));
                    for (int i = 1; i < textLines.size() && lines.size() < 6; ++i) {
                        const QString line = textLines.at(i).trimmed();
                        if (!line.isEmpty() && line != QLatin1String("}"))
                            lines.append(entry(id, line, fullText));
                    }
                    addCompartment(node.Compartments, QStringLiteral("text"), lines);
                }
                return;
            }
            case ElementKind::ConstraintDefinition:
            case ElementKind::CalcDefinition:
            case ElementKind::ConstraintUsage:
            case ElementKind::CalcUsage: {
                // SysML v2 general view: «constraint def» / «calc def» / «constraint» / «calc» with the compartments
                // parameters, result (calc) or constraint (the expression in braces), attributes, constraints, calcs.
                const ElementKind kind = element.kind();
                node.Kind = kind == ElementKind::ConstraintDefinition ? DiagramTypes::ConstraintDefinitionNode
                            : kind == ElementKind::CalcDefinition ? DiagramTypes::CalcDefinitionNode
                            : kind == ElementKind::ConstraintUsage ? DiagramTypes::ConstraintNode
                            : DiagramTypes::CalcNode;
                node.Keyword = keywordOf(element);
                node.Title = decorate(element, Diagram::isDefinitionKind(kind) ? name : usageText(Model, id)) +
                             specializationText(element);
                QVariantList parameters;
                QVariantList result;
                QVariantList attributes;
                fillFeatureEntries(element, parameters, result, attributes, node.Compartments, false);
                if (!element.expression().empty()) {
                    const QString expression = QString::fromStdString(SysMLTextImporter::oneLine(element.expression()));
                    const QString full = QString::fromStdString(element.expression());
                    if (Diagram::isConstraintKind(kind))
                        addCompartment(node.Compartments, QStringLiteral("constraint"),
                                       {entry(id, QStringLiteral("{ ") + expression + QStringLiteral(" }"), full)});
                    else
                        result.append(entry(id, expression, full));
                }
                QVariantList ordered;
                addCompartment(ordered, QStringLiteral("parameters"), parameters);
                addCompartment(ordered, QStringLiteral("result"), result);
                addCompartment(ordered, QStringLiteral("attributes"), attributes);
                node.Compartments = ordered + node.Compartments;
                return;
            }
            case ElementKind::PartDefinition:
                node.Kind = DiagramTypes::PartDefinitionNode;
                node.Keyword = QStringLiteral("«part def»");
                node.Title = decorate(element, name) + specializationText(element);
                break;
            default:
                node.Kind = DiagramTypes::PartNode;
                node.Keyword = QStringLiteral("«part»");
                node.Title = decorate(element, usageText(Model, id)) + specializationText(element) + valueText(element);
                break;
        }

        QVariantList parameters;
        QVariantList result;
        QVariantList attributes;
        QVariantList rest;
        fillFeatureEntries(element, parameters, result, attributes, rest, true);
        // a part has no parameters: directed attributes are shown with their direction
        QVariantList all = parameters + result + attributes;
        addCompartment(node.Compartments, QStringLiteral("attributes"), all);
        node.Compartments += rest;
    }

    void DiagramController::fillFeatureEntries(const Diagram::SysMLElement &element, QVariantList &parameters,
                                               QVariantList &result, QVariantList &attributes,
                                               QVariantList &compartments, bool withActions) const {
        const Diagram::SysMLCodeGenerator generator;
        QVariantList actions;
        QVariantList constraints;
        QVariantList calcs;
        QVariantList other;
        for (const ElementId child: element.ownedElements()) {
            const auto &childElement = Model.element(child);
            switch (childElement.kind()) {
                case ElementKind::Attribute: {
                    const std::string direction = directionOf(childElement);
                    if (direction == "return") {
                        // "return" is the compartment, not a word of the entry
                        QString text = usageText(Model, child) + valueText(childElement);
                        QString metadata;
                        for (const auto &prefix: childElement.prefixes()) {
                            if (prefix != "return")
                                metadata += QString::fromStdString(prefix) + QLatin1Char(' ');
                        }
                        result.append(entry(child, metadata + text));
                    } else if (!direction.empty()) {
                        parameters.append(entry(child, decorate(childElement, usageText(Model, child)) + valueText(childElement)));
                    } else {
                        attributes.append(entry(child, decorate(childElement, usageText(Model, child)) + valueText(childElement)));
                    }
                    break;
                }
                case ElementKind::ActionPerformance:
                    actions.append(entry(child, decorate(childElement, QStringLiteral("perform ") + usageText(Model, child))));
                    break;
                case ElementKind::ConstraintDefinition:
                case ElementKind::ConstraintUsage:
                    constraints.append(entry(child, QString::fromStdString(SysMLTextImporter::summarize(Model, child).Display),
                                             QString::fromStdString(generator.generate(Model, child))));
                    break;
                case ElementKind::CalcDefinition:
                case ElementKind::CalcUsage:
                    calcs.append(entry(child, QString::fromStdString(SysMLTextImporter::summarize(Model, child).Display),
                                       QString::fromStdString(generator.generate(Model, child))));
                    break;
                case ElementKind::Textual: {
                    if (isHiddenStatement(childElement))
                        break;
                    const auto summary = SysMLTextImporter::summarize(childElement);
                    const QString display = QString::fromStdString(summary.Display);
                    const QString tooltip = QString::fromStdString(childElement.text());
                    if (summary.Kind == Diagram::StatementSummary::Category::Constraint)
                        constraints.append(entry(child, display, tooltip));
                    else if (summary.Kind == Diagram::StatementSummary::Category::Function)
                        calcs.append(entry(child, display, tooltip));
                    else
                        other.append(entry(child, display, tooltip));
                    break;
                }
                default:
                    break;
            }
        }
        if (withActions)
            addCompartment(compartments, QStringLiteral("actions"), actions);
        addCompartment(compartments, QStringLiteral("constraints"), constraints);
        addCompartment(compartments, QStringLiteral("calcs"), calcs);
        addCompartment(compartments, QStringLiteral("other"), other);
    }

    QPointF DiagramController::defaultPositionFor(ElementId id) const {
        const ElementId owner = Model.element(id).owner();
        if (const auto *ownerNode = Nodes->find(owner)) {
            const auto &siblings = Model.children(owner);
            const auto index = static_cast<qreal>(std::find(siblings.begin(), siblings.end(), id) - siblings.begin());
            if (ownerNode->Kind == DiagramTypes::PackageNode)
                return ownerNode->Rect.topLeft() + QPointF(20 + index * 30, 50 + index * 30);
            return ownerNode->Rect.topLeft() + QPointF(std::max<qreal>(0, index * 180 - 60), ownerNode->Rect.height() + 60);
        }
        const int index = Nodes->count();
        return {40.0 + (index % 4) * 220.0, 40.0 + (index / 4) * 200.0};
    }

    // --- automatic layout ---------------------------------------------------------------------------

    namespace {
        constexpr qreal LayoutMargin = 40.0;
        constexpr qreal LayoutGap = 50.0;          ///< between nodes in a row
        constexpr qreal LayoutRowGap = 60.0;       ///< between rows
        constexpr qreal CompositionGap = 70.0;     ///< owner part -> owned parts below
        constexpr qreal FramePadding = 24.0;
        constexpr qreal FrameTitleSpace = 44.0;
        constexpr qreal MaximumRowWidth = 1400.0;
        constexpr qreal MaximumFrameRowWidth = 900.0;

        /// Places sizes in rows (left to right, wrapping at maxWidth). Returns the positions and the total size.
        QSizeF flow(const std::vector<QSizeF> &sizes, qreal maxWidth, std::vector<QPointF> &positions) {
            positions.clear();
            qreal x = 0;
            qreal y = 0;
            qreal rowHeight = 0;
            qreal width = 0;
            for (const auto &size: sizes) {
                if (x > 0 && x + size.width() > maxWidth) {
                    x = 0;
                    y += rowHeight + LayoutRowGap;
                    rowHeight = 0;
                }
                positions.emplace_back(x, y);
                x += size.width() + LayoutGap;
                rowHeight = std::max(rowHeight, size.height());
                width = std::max(width, x - LayoutGap);
            }
            return {width, sizes.empty() ? 0.0 : y + rowHeight};
        }
    }

    std::vector<ElementId> DiagramController::nodeChildren(ElementId owner) const {
        std::vector<ElementId> result;
        for (const ElementId child: Model.children(owner)) {
            if (isNodeElement(Model, child))
                result.push_back(child);
        }
        return result;
    }

    QSizeF DiagramController::estimatedSize(const DiagramNode &node) const {
        if (Measured.count(node.Id) > 0)
            return node.Rect.size();
        // Same structure as NodeDelegate: keyword, title, compartments with caption and entries.
        qreal longest = std::max(node.Keyword.size() * 7.0, node.Title.size() * 8.5);
        qreal height = 4 + 18 + 24;
        for (const auto &compartment: node.Compartments) {
            const auto entries = compartment.toMap().value(QStringLiteral("entries")).toList();
            for (const auto &item: entries)
                longest = std::max(longest, item.toMap().value(QStringLiteral("text")).toString().size() * 7.6);
            height += 1 + 16 + entries.size() * 19.0 + 4;
        }
        return {std::max<qreal>(140, longest + 16), height + 8};
    }

    QSizeF DiagramController::measure(ElementId id, std::map<ElementId, QSizeF> &extents,
                                      std::map<ElementId, std::vector<QPointF>> &framePositions) {
        const auto *node = Nodes->find(id);
        if (!node)
            return {};
        const auto children = nodeChildren(id);
        std::vector<QSizeF> childSizes;
        for (const ElementId child: children)
            childSizes.push_back(measure(child, extents, framePositions));

        QSizeF extent;
        if (node->Kind == DiagramTypes::PackageNode) {
            std::vector<QPointF> positions;
            const QSizeF content = flow(childSizes, MaximumFrameRowWidth, positions);
            framePositions[id] = positions;
            extent = QSizeF(std::max(DefaultFrameWidth * 0.75, content.width() + 2 * FramePadding),
                            std::max(DefaultFrameHeight * 0.6, content.height() + FrameTitleSpace + FramePadding));
        } else {
            const QSizeF own = estimatedSize(*node);
            if (children.empty()) {
                extent = own;
            } else {
                qreal width = 0;
                qreal height = 0;
                for (const auto &size: childSizes) {
                    width += size.width();
                    height = std::max(height, size.height());
                }
                width += LayoutGap * qreal(childSizes.size() - 1);
                extent = QSizeF(std::max(own.width(), width), own.height() + CompositionGap + height);
            }
        }
        extents[id] = extent;
        return extent;
    }

    void DiagramController::place(ElementId id, const QPointF &topLeft, const std::map<ElementId, QSizeF> &extents,
                                  const std::map<ElementId, std::vector<QPointF>> &framePositions) {
        const int row = Nodes->indexOf(id);
        if (row < 0)
            return;
        const auto &node = Nodes->nodes()[std::size_t(row)];
        const QSizeF extent = extents.at(id);
        const auto children = nodeChildren(id);

        if (node.Kind == DiagramTypes::PackageNode) {
            Nodes->setRect(row, QRectF(topLeft, extent));
            const auto &positions = framePositions.at(id);
            for (std::size_t i = 0; i < children.size() && i < positions.size(); ++i)
                place(children[i], topLeft + QPointF(FramePadding, FrameTitleSpace) + positions[i], extents, framePositions);
            return;
        }

        const QSizeF own = estimatedSize(node);
        Nodes->setRect(row, QRectF(QPointF(topLeft.x() + (extent.width() - own.width()) / 2, topLeft.y()), own));
        if (children.empty())
            return;
        qreal childrenWidth = 0;
        for (const ElementId child: children)
            childrenWidth += extents.at(child).width();
        childrenWidth += LayoutGap * qreal(children.size() - 1);
        qreal x = topLeft.x() + (extent.width() - childrenWidth) / 2;
        const qreal y = topLeft.y() + own.height() + CompositionGap;
        for (const ElementId child: children) {
            place(child, QPointF(x, y), extents, framePositions);
            x += extents.at(child).width() + LayoutGap;
        }
    }

    void DiagramController::layoutAll() {
        syncNow();
        std::map<ElementId, QSizeF> extents;
        std::map<ElementId, std::vector<QPointF>> framePositions;
        const auto roots = nodeChildren(RootId);
        std::vector<QSizeF> sizes;
        for (const ElementId id: roots)
            sizes.push_back(measure(id, extents, framePositions));
        std::vector<QPointF> positions;
        flow(sizes, MaximumRowWidth, positions);
        for (std::size_t i = 0; i < roots.size(); ++i)
            place(roots[i], QPointF(LayoutMargin, LayoutMargin) + positions[i], extents, framePositions);
        AutoLayout = true;
    }

    QRectF DiagramController::contentBounds() const {
        QRectF bounds;
        for (const auto &node: Nodes->nodes())
            bounds = bounds.isNull() ? node.Rect : bounds.united(node.Rect);
        return bounds;
    }

    std::string DiagramController::uniqueName(const std::string &base, ElementId owner) const {
        const auto &siblings = Model.children(owner);
        for (int index = 1;; ++index) {
            const std::string candidate = base + std::to_string(index);
            const bool used = std::any_of(siblings.begin(), siblings.end(),
                                          [&](ElementId sibling) { return Model.element(sibling).name() == candidate; });
            if (!used)
                return candidate;
        }
    }
}
