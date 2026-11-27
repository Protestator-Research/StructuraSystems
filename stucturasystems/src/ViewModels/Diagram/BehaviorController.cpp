//
// Behavior view: actions, control nodes and states of one action or state, with successions and transitions.
//

#include "BehaviorController.h"
#include "DiagramLayout.h"
#include "EdgeRouting.h"
#include "DiagramGeometry.h"

#include <QMetaObject>

#include <Diagram/Conversion/SysMLCodeGenerator.h>
#include <Diagram/Conversion/SysMLTextImporter.h>
#include <Diagram/Semantic/RelationshipRules.h>

#include <algorithm>
#include <deque>
#include <exception>

namespace StructuraSystems::Client {

    using Diagram::ElementId;
    using Diagram::ElementKind;
    using Diagram::RelationshipKind;
    using Diagram::RootId;

    namespace {
        constexpr qreal Margin = 60.0;          ///< room for the frame title above the nodes
        constexpr qreal RowGap = 60.0;          ///< between the rows of nodes
        constexpr qreal ColumnGap = 60.0;
        constexpr qreal HeaderHeight = 40.0;
        constexpr qreal EntryHeight = 18.0;
        constexpr qreal CaptionHeight = 16.0;   ///< "state actions", "assert constraints"
        constexpr qreal Inset = 16.0;           ///< nested states inside their state
        constexpr qreal FramePadding = 24.0;
        constexpr qreal EdgeHitDistance = 6.0;
        constexpr qreal PinSize = 12.0;

        bool isPartLike(ElementKind kind) {
            return kind == ElementKind::PartUsage || kind == ElementKind::PartDefinition;
        }
        constexpr qreal LanePadding = 24.0;     ///< inside a swimlane, left and right of its actions
        constexpr qreal LaneHeader = 46.0;      ///< «performer» name and "perform actions" on top of a lane
        constexpr qreal PinSpacing = 70.0;      ///< between the pins on one side of an action

        /// in / out / inout of a parameter (empty if it is none): "in item fuelIn : Fuel;"
        std::string directionOf(const Diagram::SysMLElement &element) {
            std::string direction;
            for (const auto &prefix: element.prefixes()) {
                if (prefix == "in" || prefix == "out" || prefix == "inout")
                    direction = prefix;
            }
            return direction;
        }

        /// The parameters of an action shown as pins: its directed features (items, attributes, parts, ...).
        std::vector<ElementId> parametersOf(const Diagram::SemanticModel &model, ElementId action) {
            std::vector<ElementId> result;
            for (const ElementId child: model.children(action)) {
                const auto &element = model.element(child);
                if (directionOf(element).empty() || element.name().empty())
                    continue;
                switch (element.kind()) {
                    case ElementKind::ItemUsage:
                    case ElementKind::Attribute:
                    case ElementKind::PartUsage:
                    case ElementKind::OccurrenceUsage:
                    case ElementKind::PortUsage:
                        result.push_back(child);
                        break;
                    default:
                        break;
                }
            }
            return result;
        }

        QString q(const std::string &text) {
            return QString::fromStdString(text);
        }

        std::string keywordOf(const Diagram::SysMLElement &element) {
            return element.declaredKeyword().value_or(Diagram::defaultKeyword(element.kind()));
        }

        /// entry / do / exit action of a state (shown inside the state, not as node): "entry action a;",
        /// "entry assign t := 0;", "do senseTemperature;"
        bool isStateAction(const Diagram::SysMLElement &element) {
            const auto keyword = element.declaredKeyword().value_or("");
            return (element.kind() == ElementKind::ActionUsage || element.kind() == ElementKind::ActionPerformance) &&
                   (keyword == "entry action" || keyword == "do action" || keyword == "exit action" || keyword == "entry" ||
                    keyword == "do" || keyword == "exit");
        }

        /// "send new Show(p) to screen", "if x > 0", "loop until c", "else" (empty for a plain action).
        QString formText(const Diagram::SysMLElement &element) {
            if (element.kind() != ElementKind::ActionUsage)
                return {};
            QString text = element.isElseBranch() ? QStringLiteral("else") : QString();
            if (element.actionForm() != Diagram::ActionForm::Plain) {
                text += (text.isEmpty() ? QString() : QStringLiteral(" ")) + q(Diagram::toKeyword(element.actionForm()));
                if (!element.actionClause().empty())
                    text += QLatin1Char(' ') + q(element.actionClause());
            }
            if (!text.isEmpty() && !element.untilCondition().empty())
                text += QStringLiteral(" until ") + q(element.untilCondition());
            return text;
        }

        /// What the view can show the inside of: actions, states, cases, and "perform action x { ... }" with a body.
        bool isBehaviorContext(const Diagram::SemanticModel &model, ElementId id) {
            const auto &element = model.element(id);
            if (Diagram::isBehaviorKind(element.kind()))
                return true;
            if (element.kind() != ElementKind::ActionPerformance || element.performStyle() != Diagram::PerformStyle::DeclareAction)
                return false;
            const auto &children = model.children(id);
            return std::any_of(children.begin(), children.end(), [&](ElementId child) {
                return Diagram::isSuccessionEnd(model.element(child).kind());
            });
        }

        bool isState(ElementKind kind) {
            return kind == ElementKind::StateDefinition || kind == ElementKind::StateUsage;
        }

        QString usageTitle(const Diagram::SysMLElement &element) {
            QString title = q(element.name());
            if (!element.typeName().empty())
                title += QStringLiteral(" : ") + q(element.typeName());
            return title;
        }

        /// One step of the body of an if / loop in its compartment: "assign y := 2*x", "action charge : Charge",
        /// "then decide" (empty for what is no step).
        QString bodyText(const Diagram::SemanticModel &model, ElementId id) {
            const auto &element = model.element(id);
            QString text;
            if (element.kind() == ElementKind::ActionUsage) {
                text = formText(element);
                if (!text.isEmpty() && !element.name().empty())
                    text = q(element.name()) + QStringLiteral(": ") + text;
                if (text.isEmpty())
                    text = QStringLiteral("action ") + usageTitle(element);
            } else if (element.kind() == ElementKind::ActionPerformance) {
                text = QStringLiteral("perform ") + usageTitle(element);
            } else if (element.kind() == ElementKind::ControlNode) {
                const std::string keyword = keywordOf(element);
                if (Diagram::isImplicitControlKeyword(keyword))
                    return {};
                text = (q(keyword) + QLatin1Char(' ') + q(element.name())).trimmed();
            } else {
                return {};
            }
            return (element.declaredWithThen() ? QStringLiteral("then ") : QString()) + text.trimmed();
        }
    }

    BehaviorController::BehaviorController(Diagram::SemanticModel &model, QObject *parent) :
            QObject(parent),
            Model(model),
            Nodes(new DiagramNodeModel(this)),
            Edges(new DiagramEdgeModel(this)) {
        ListenerToken = Model.addListener([this](const Diagram::ModelChange &) { scheduleSync(); });
        connect(Nodes, &DiagramNodeModel::geometryChanged, this, &BehaviorController::updateCanvasSize);
        syncNow();
    }

    BehaviorController::~BehaviorController() {
        Model.removeListener(ListenerToken);
    }

    // --- context -------------------------------------------------------------------------------------

    std::optional<ElementId> BehaviorController::context() const {
        if (ContextElement && Model.contains(*ContextElement) && isBehaviorContext(Model, *ContextElement))
            return ContextElement;
        return std::nullopt;
    }

    QString BehaviorController::contextId() const {
        const auto element = context();
        return element ? QString::number(*element) : QString();
    }

    bool BehaviorController::stateMachine() const {
        const auto element = context();
        return element && isState(Model.element(*element).kind());
    }

    void BehaviorController::setContextId(const QString &id) {
        bool ok = false;
        const auto value = id.toULongLong(&ok);
        if (!ok || !Model.contains(value) || !isBehaviorContext(Model, value) || ContextElement == value)
            return;
        saveContextLayout();
        ContextElement = value;
        Positions.clear();
        SelectedNodes.clear();
        SelectedEdges.clear();
        AutoLayout = true;
        applyContextLayout();
        emit contextChanged();
        syncNow();
    }

    void BehaviorController::updateContexts() {
        QVariantList contexts;
        std::function<void(ElementId)> visit = [&](ElementId owner) {
            for (const ElementId child: Model.children(owner)) {
                const auto &element = Model.element(child);
                // actions that only send, accept, assign or terminate have no flow of their own (unless they own one)
                const bool flowless = element.kind() == ElementKind::ActionUsage &&
                                      element.actionForm() != Diagram::ActionForm::Plain &&
                                      !Diagram::isControlStructure(element.actionForm()) && element.ownedElements().empty();
                if (isBehaviorContext(Model, child) && !isStateAction(element) && !flowless) {
                    // "action def ChargeBattery", "loop ChargeBattery::charging", "if ChargeBattery::charging::(if c)"
                    QString path;
                    for (const ElementId step: Model.ownershipPath(child)) {
                        const auto &segment = Model.element(step);
                        const QString form = formText(segment);
                        path += (path.isEmpty() ? QString() : QStringLiteral("::")) +
                                (!segment.name().empty() ? q(segment.name())
                                 : QStringLiteral("(") + (form.isEmpty() ? q(keywordOf(segment)) : form) + QStringLiteral(")"));
                    }
                    const QString form = formText(element);
                    const QString title = (form.isEmpty() ? q(keywordOf(element)) : form.section(QLatin1Char(' '), 0, 0)) +
                                          QLatin1Char(' ') + path;
                    contexts.append(QVariantMap{{QStringLiteral("id"), QString::number(child)},
                                                {QStringLiteral("title"), title}});
                }
                visit(child);
            }
        };
        visit(RootId);
        if (contexts != Contexts) {
            Contexts = contexts;
            emit contextsChanged();
        }
    }

    void BehaviorController::chooseContext() {
        if (context())
            return;
        std::optional<ElementId> first;
        std::optional<ElementId> withContent;
        for (const auto &entry: Contexts) {
            const ElementId id = entry.toMap().value(QStringLiteral("id")).toString().toULongLong();
            if (!first)
                first = id;
            const auto &children = Model.children(id);
            if (!withContent && std::any_of(children.begin(), children.end(), [&](ElementId child) {
                    const auto &element = Model.element(child);
                    return Diagram::isSuccessionEnd(element.kind()) && !isStateAction(element);
                }))
                withContent = id;
        }
        const auto chosen = withContent ? withContent : first;
        if (chosen != ContextElement) {
            ContextElement = chosen;
            Positions.clear();
            AutoLayout = true;
            applyContextLayout();
            emit contextChanged();
        }
    }

    bool BehaviorController::isNode(ElementId id) const {
        const auto start = context();
        if (!start || !Model.contains(id))
            return false;
        const auto &element = Model.element(id);
        if (!Diagram::isSuccessionEnd(element.kind()) || isStateAction(element))
            return false;
        // the nodes of the context, and the states nested in its states (drawn inside them)
        if (element.owner() == *start)
            return true;
        const ElementId owner = element.owner();
        return owner != RootId && Model.element(owner).kind() == ElementKind::StateUsage && isNode(owner);
    }

    std::optional<ElementId> BehaviorController::parentNode(ElementId id) const {
        const auto start = context();
        if (!start || !Model.contains(id))
            return std::nullopt;
        const ElementId owner = Model.element(id).owner();
        if (owner == *start || owner == RootId)
            return std::nullopt;
        return owner;
    }

    // --- graph ---------------------------------------------------------------------------------------

    void BehaviorController::scheduleSync() {
        if (SyncQueued)
            return;
        SyncQueued = true;
        QMetaObject::invokeMethod(this, [this] {
            if (SyncQueued)
                syncNow();
        }, Qt::QueuedConnection);
    }

    QSizeF BehaviorController::sizeOf(const DiagramNode &node) const {
        switch (node.Kind) {
            case DiagramTypes::DecisionNode:
                return {36, 36};
            case DiagramTypes::ForkNode:
                return {100, 8};
            case DiagramTypes::StartNode:
                return {22, 22};
            case DiagramTypes::DoneNode:
            case DiagramTypes::TerminateNode:
                return {26, 26};
            case DiagramTypes::PortNode:
                return {PinSize, PinSize};
            default:
                break;
        }
        qreal width = std::max(node.Title.size() * 7.8, node.Keyword.size() * 6.6) + 32;
        // room for its pins side by side (in on top, out at the bottom)
        if (Model.contains(node.Id) && node.Kind == DiagramTypes::ActionNode) {
            int in = 0;
            int out = 0;
            for (const ElementId parameter: parametersOf(Model, node.Id))
                ++(directionOf(Model.element(parameter)) == "in" ? in : out);
            width = std::max(width, qreal(std::max(in, out)) * PinSpacing / 0.6 + 20);
        }
        int entries = 0;
        for (const auto &compartment: node.Compartments) {
            for (const auto &entry: compartment.toMap().value(QStringLiteral("entries")).toList()) {
                width = std::max(width, entry.toMap().value(QStringLiteral("text")).toString().size() * 7.2 + 24);
                ++entries;
            }
        }
        width = std::clamp<qreal>(width, 120, 520);
        const qreal height = HeaderHeight + (node.Keyword.isEmpty() ? 0 : 6) +
                             (entries > 0 ? entries * EntryHeight + qreal(node.Compartments.size()) * CaptionHeight + 8 : 0);
        return {std::round(width), height};
    }

    void BehaviorController::syncNow() {
        SyncQueued = false;
        updateContexts();
        chooseContext();

        std::vector<DiagramNode> nodes;
        std::vector<DiagramEdge> edges;
        std::vector<DiagramNode> pins;
        PinOwners.clear();
        if (const auto start = context()) {
            const auto entryOf = [](ElementId id, const QString &text) {
                return QVariantMap{{QStringLiteral("entryId"), QString::number(id)}, {QStringLiteral("text"), text},
                                   {QStringLiteral("tooltip"), QString()}};
            };
            std::function<void(ElementId, int)> collect = [&](ElementId owner, int depth) {
                for (const ElementId child: Model.children(owner)) {
                    if (!isNode(child))
                        continue;
                    const auto &element = Model.element(child);
                    DiagramNode node;
                    node.Id = child;
                    node.Depth = depth;
                    switch (element.kind()) {
                        case ElementKind::ControlNode: {
                            const std::string keyword = keywordOf(element);
                            node.Kind = keyword == "decide" || keyword == "merge" ? DiagramTypes::DecisionNode
                                        : keyword == "fork" || keyword == "join" ? DiagramTypes::ForkNode
                                        : keyword == "done" ? DiagramTypes::DoneNode
                                        : DiagramTypes::StartNode;
                            node.Keyword = q(keyword);
                            node.Title = Diagram::isImplicitControlKeyword(keyword) ? QString() : q(element.name());
                            break;
                        }
                        case ElementKind::StateUsage: {
                            node.Kind = DiagramTypes::StateNode;
                            node.Title = usageTitle(element);
                            // «state», «exhibit state»; «parallel» for concurrent nested states
                            node.Keyword = QStringLiteral("«") + q(element.declaredKeyword().value_or("state")) + QStringLiteral("»");
                            if (element.isParallel())
                                node.Keyword += QStringLiteral(" «parallel»");
                            QVariantList actions;
                            QVariantList constraints;
                            for (const ElementId owned: element.ownedElements()) {
                                const auto &ownedElement = Model.element(owned);
                                if (isStateAction(ownedElement)) {
                                    // "entry performSelfTest", "exit send engineStartStatus to driver"
                                    const std::string keyword = *ownedElement.declaredKeyword();
                                    const QString form = formText(ownedElement);
                                    actions.append(entryOf(owned, q(keyword.substr(0, keyword.find(' '))) + QLatin1Char(' ') +
                                                                  (form.isEmpty() ? usageTitle(ownedElement) : form)));
                                } else if (ownedElement.kind() == ElementKind::ConstraintUsage &&
                                           ownedElement.declaredKeyword().value_or("") == "assert constraint") {
                                    // "{electricalPower <= 500 [W]}"
                                    QString text = usageTitle(ownedElement);
                                    if (!ownedElement.expression().empty())
                                        text = (text.isEmpty() ? QString() : text + QLatin1Char(' ')) + QStringLiteral("{") +
                                               q(Diagram::SysMLTextImporter::oneLine(ownedElement.expression())) + QStringLiteral("}");
                                    constraints.append(entryOf(owned, text));
                                }
                            }
                            if (!actions.isEmpty())
                                node.Compartments.append(QVariantMap{{QStringLiteral("title"), QStringLiteral("state actions")},
                                                                     {QStringLiteral("entries"), actions}});
                            if (!constraints.isEmpty())
                                node.Compartments.append(QVariantMap{{QStringLiteral("title"), QStringLiteral("assert constraints")},
                                                                     {QStringLiteral("entries"), constraints}});
                            break;
                        }
                        default:
                            node.Kind = DiagramTypes::ActionNode;
                            node.Title = usageTitle(element);
                            // «action», «perform», «perform action», «accept», «send», ...
                            if (element.kind() == ElementKind::ActionPerformance)
                                node.Keyword = QStringLiteral("«perform»");
                            else
                                node.Keyword = QStringLiteral("«") + q(element.declaredKeyword().value_or("action")) + QStringLiteral("»");
                            if (element.kind() == ElementKind::ActionUsage && Diagram::isControlStructure(element.actionForm())) {
                                // graphical BNF: «if» with the compartments 'if condition', 'then body', 'else body';
                                // «loop» (while, loop ... until, for) with 'while condition' / 'for iterator', 'loop body',
                                // 'until condition'
                                const auto form = element.actionForm();
                                node.Keyword = form == Diagram::ActionForm::If ? QStringLiteral("«if»") : QStringLiteral("«loop»");
                                node.Title = usageTitle(element);
                                const auto compartment = [&](const QString &title, const QVariantList &entries) {
                                    if (!entries.isEmpty())
                                        node.Compartments.append(QVariantMap{{QStringLiteral("title"), title},
                                                                             {QStringLiteral("entries"), entries}});
                                };
                                const QString clause = q(element.actionClause());
                                if (form == Diagram::ActionForm::If)
                                    compartment(QStringLiteral("if condition"), {entryOf(child, clause)});
                                else if (form == Diagram::ActionForm::While)
                                    compartment(QStringLiteral("while condition"), {entryOf(child, clause)});
                                else if (form == Diagram::ActionForm::For)
                                    compartment(QStringLiteral("for iterator"), {entryOf(child, clause)});
                                QVariantList body;
                                QVariantList elseBody;
                                for (const ElementId owned: element.ownedElements()) {
                                    const auto &ownedElement = Model.element(owned);
                                    if (ownedElement.kind() == ElementKind::ActionUsage && ownedElement.isElseBranch()) {
                                        for (const ElementId inner: ownedElement.ownedElements())
                                            if (const QString text = bodyText(Model, inner); !text.isEmpty())
                                                elseBody.append(entryOf(inner, text));
                                        continue;
                                    }
                                    if (const QString text = bodyText(Model, owned); !text.isEmpty())
                                        body.append(entryOf(owned, text));
                                }
                                compartment(form == Diagram::ActionForm::If ? QStringLiteral("then body") : QStringLiteral("loop body"), body);
                                compartment(QStringLiteral("else body"), elseBody);
                                if (!element.untilCondition().empty())
                                    compartment(QStringLiteral("until condition"), {entryOf(child, q(element.untilCondition()))});
                            } else if (element.kind() == ElementKind::ActionUsage &&
                                       element.actionForm() == Diagram::ActionForm::Terminate) {
                                // terminate-node: a circle with a cross
                                node.Kind = DiagramTypes::TerminateNode;
                                node.Keyword = QStringLiteral("terminate");
                                node.Title = element.name().empty() ? q(element.actionClause()) : usageTitle(element);
                            } else if (const QString form = formText(element); !form.isEmpty()) {
                                // «send action» new Show(p) to screen, «accept action» scene : Scene via viewPort, «assign» x := 1
                                const QString keyword = form.section(QLatin1Char(' '), 0, 0);
                                const QString rest = form.section(QLatin1Char(' '), 1);
                                node.Keyword = QStringLiteral("«") + keyword +
                                               (keyword == QLatin1String("send") || keyword == QLatin1String("accept")
                                                ? QStringLiteral(" action") : QString()) + QStringLiteral("»");
                                node.Title = element.name().empty() ? rest : usageTitle(element) +
                                                                             (rest.isEmpty() ? QString() : QStringLiteral(": ") + rest);
                            }
                            break;
                    }
                    nodes.push_back(node);
                    if (element.kind() == ElementKind::StateUsage)
                        collect(child, depth + 1);
                }
            };
            collect(*start, 0);
            // swimlanes: the parts that perform the actions of the context ("perform action0.action1;" in part1)
            LaneOf.clear();
            LaneTitles.clear();
            {
                std::set<ElementId> topLevel;
                for (const auto &node: nodes) {
                    if (Model.element(node.Id).owner() == *start)
                        topLevel.insert(node.Id);
                }
                // "a.b.c" seen from where it is written: the first name in the scopes outwards, then down
                const auto resolvePath = [&](ElementId scope, const std::string &path) -> std::optional<ElementId> {
                    const auto segments = Diagram::SysMLTextImporter::splitReference(path);
                    if (segments.empty())
                        return std::nullopt;
                    std::optional<ElementId> current;
                    for (ElementId candidate = scope;; candidate = Model.element(candidate).owner()) {
                        for (const ElementId child: Model.children(candidate)) {
                            const auto &element = Model.element(child);
                            if (element.name() == segments.front() && element.kind() != ElementKind::ActionPerformance) {
                                current = child;
                                break;
                            }
                        }
                        if (current || candidate == RootId)
                            break;
                    }
                    for (std::size_t i = 1; current && i < segments.size(); ++i) {
                        std::optional<ElementId> next;
                        for (const ElementId child: Model.children(*current)) {
                            if (Model.element(child).name() == segments[i])
                                next = child;
                        }
                        current = next;
                    }
                    return current;
                };
                std::map<ElementId, int> laneOfPerformer;
                std::function<void(ElementId)> visit = [&](ElementId owner) {
                    for (const ElementId child: Model.children(owner)) {
                        const auto &perform = Model.element(child);
                        if (perform.kind() == ElementKind::ActionPerformance &&
                            perform.performStyle() == Diagram::PerformStyle::ReferenceAction &&
                            perform.declaredKeyword().value_or("").empty() && perform.owner() != RootId &&
                            isPartLike(Model.element(perform.owner()).kind())) {
                            const auto action = resolvePath(perform.owner(), perform.name());
                            if (action && topLevel.count(*action) && !LaneOf.count(*action)) {
                                const ElementId performer = perform.owner();
                                auto lane = laneOfPerformer.find(performer);
                                if (lane == laneOfPerformer.end()) {
                                    // «performer» part0.part1: the path of nested parts
                                    QStringList names;
                                    for (ElementId current = performer; current != RootId &&
                                                                        isPartLike(Model.element(current).kind());
                                         current = Model.element(current).owner())
                                        names.prepend(q(Model.element(current).name()));
                                    LaneTitles.push_back(names.join(QLatin1Char('.')));
                                    lane = laneOfPerformer.emplace(performer, int(LaneTitles.size()) - 1).first;
                                }
                                LaneOf[*action] = lane->second;
                            }
                        }
                        visit(child);
                    }
                };
                visit(RootId);
            }
            // parameters as pins: on the border of their action, those of the context on the frame (graphical BNF:
            // param-t / param-b; the action-flow compartment of an action with its parameters around it)
            const auto addPins = [&](ElementId owner, int depth) {
                for (const ElementId parameter: parametersOf(Model, owner)) {
                    const auto &element = Model.element(parameter);
                    DiagramNode pin;
                    pin.Id = parameter;
                    pin.Kind = DiagramTypes::PortNode;
                    pin.Depth = depth;
                    pin.Keyword = q(directionOf(element));
                    pin.Title = usageTitle(element);
                    PinOwners[parameter] = owner;
                    pins.push_back(pin);
                }
            };
            for (const auto &node: nodes) {
                if (node.Kind == DiagramTypes::ActionNode)
                    addPins(node.Id, node.Depth + 1);
            }
            addPins(*start, 0);
            // input / output flows between the parameters of the actions shown (slide 57): an arrow between the actions
            const auto nodeOf = [&](ElementId end) -> std::optional<ElementId> {
                for (ElementId current = end; current != RootId && Model.contains(current); current = Model.element(current).owner()) {
                    if (isNode(current))
                        return current;
                }
                return std::nullopt;
            };
            // the way a line leaves a pin: away from its action, into the frame for those of the context
            const auto pinOutward = [&](ElementId pin) -> QPointF {
                const QString side = pinSideOf(pin);
                const bool frame = PinOwners.count(pin) && PinOwners.at(pin) == *start;
                if (side == QLatin1String("top"))
                    return {0, -1};
                if (side == QLatin1String("bottom"))
                    return frame ? QPointF(0, -1) : QPointF(0, 1);
                if (side == QLatin1String("left"))
                    return frame ? QPointF(1, 0) : QPointF(-1, 0);
                if (side == QLatin1String("right"))
                    return frame ? QPointF(-1, 0) : QPointF(1, 0);
                return {};
            };
            const auto endOf = [&](ElementId end, const std::string &feature) -> std::optional<ElementId> {
                if (feature.empty() && isPin(end))
                    return end; // the parameter: its pin
                return nodeOf(end);
            };
            for (const auto &relationship: Model.relationships()) {
                if (relationship.Kind != RelationshipKind::Flow && relationship.Kind != RelationshipKind::Binding)
                    continue;
                const auto source = endOf(relationship.Source, relationship.SourceFeature);
                const auto target = endOf(relationship.Target, relationship.TargetFeature);
                if (!source || !target || *source == *target)
                    continue;
                const bool pinToPin = isPin(*source) && isPin(*target);
                if (relationship.Kind == RelationshipKind::Binding) {
                    // "bind x = pump.cmd": a line with "=" between the pins (binding-connection)
                    if (!pinToPin)
                        continue;
                    DiagramEdge edge;
                    edge.Kind = DiagramTypes::Binding;
                    edge.Source = *source;
                    edge.Target = *target;
                    edge.Key = QStringLiteral("r:%1").arg(relationship.Id);
                    edge.Selected = SelectedEdges.count(edge.Key) > 0;
                    edge.Label = QStringLiteral("=");
                    edge.SourceOut = pinOutward(*source);
                    edge.TargetOut = pinOutward(*target);
                    edges.push_back(edge);
                    continue;
                }
                DiagramEdge edge;
                edge.Kind = DiagramTypes::Flow;
                edge.Source = *source;
                edge.Target = *target;
                // between pins: a filled head at the target; "succession flow": dashed
                edge.FilledHead = isPin(*source) || isPin(*target);
                edge.Dashed = relationship.SuccessionFlow;
                edge.SourceOut = pinOutward(*source);
                edge.TargetOut = pinOutward(*target);
                edge.Key = QStringLiteral("r:%1").arg(relationship.Id);
                edge.Selected = SelectedEdges.count(edge.Key) > 0;
                QString label = q(relationship.Name);
                if (!relationship.TypeName.empty())
                    label += QStringLiteral(" of ") + q(relationship.TypeName); // flow-label: ... ('of' payload)?
                edge.Label = label.trimmed();
                edges.push_back(edge);
            }
            for (const auto &relationship: Model.relationships()) {
                if ((relationship.Kind != RelationshipKind::Succession && relationship.Kind != RelationshipKind::Transition) ||
                    !isNode(relationship.Source) || !isNode(relationship.Target))
                    continue;
                DiagramEdge edge;
                edge.Kind = relationship.Kind == RelationshipKind::Transition ? DiagramTypes::Transition : DiagramTypes::Succession;
                edge.Source = relationship.Source;
                edge.Target = relationship.Target;
                edge.Key = QStringLiteral("r:%1").arg(relationship.Id);
                edge.Selected = SelectedEdges.count(edge.Key) > 0;
                QString label = q(relationship.Trigger);
                // "entry; if true then off;" (written so for the parser of the app): the initial transition, no guard
                const bool initial = Model.element(relationship.Source).kind() == ElementKind::ControlNode &&
                                     keywordOf(Model.element(relationship.Source)) == "entry" && relationship.Guard == "true";
                if (!relationship.Guard.empty() && !initial)
                    label += QStringLiteral(" [") + q(relationship.Guard) + QStringLiteral("]");
                // in a state machine the start leads into the first state with a solid arrow (table 17)
                if (Model.element(relationship.Source).kind() == ElementKind::ControlNode &&
                    keywordOf(Model.element(relationship.Source)) == "entry" &&
                    Model.element(relationship.Target).kind() == ElementKind::StateUsage)
                    edge.Kind = DiagramTypes::Transition;
                if (!relationship.Effect.empty())
                    label += QStringLiteral(" / ") + q(relationship.Effect);
                if (!relationship.Name.empty())
                    label = q(relationship.Name) + (label.isEmpty() ? QString() : QStringLiteral(": ") + label.trimmed());
                edge.Label = label.trimmed();
                edges.push_back(edge);
            }
        }

        std::set<ElementId> wanted;
        for (const auto &node: nodes)
            wanted.insert(node.Id);
        for (const auto &[pin, owner]: PinOwners)
            wanted.insert(pin);
        for (int row = Nodes->count() - 1; row >= 0; --row) {
            if (wanted.count(Nodes->nodes()[std::size_t(row)].Id) == 0)
                Nodes->removeAt(row);
        }
        for (auto it = SelectedNodes.begin(); it != SelectedNodes.end();)
            it = wanted.count(*it) ? std::next(it) : SelectedNodes.erase(it);

        // positions: rows from the start downwards (breadth first along the arrows), nested states inside their state
        const bool missing = std::any_of(nodes.begin(), nodes.end(), [&](const DiagramNode &node) {
            return Positions.count(node.Id) == 0;
        });
        if (missing && AutoLayout && context()) {
            std::map<ElementId, QPointF> relative;
            layoutLevel(*context(), nodes, edges, relative);
            // room on the left for the in-parameters of the context on its frame
            const bool framePins = std::any_of(PinOwners.begin(), PinOwners.end(), [&](const auto &pin) {
                return pin.second == *context() && directionOf(Model.element(pin.first)) == "in";
            });
            const QPointF origin(Margin + (framePins ? 70.0 : 0.0), Margin);
            // absolute: parents first (the nodes are collected parents before their nested states)
            for (const auto &node: nodes) {
                const auto parent = parentNode(node.Id);
                if (!parent) {
                    Positions[node.Id] = relative[node.Id] + origin;
                    continue;
                }
                const auto own = std::find_if(nodes.begin(), nodes.end(), [&](const DiagramNode &other) { return other.Id == *parent; });
                const qreal header = own != nodes.end() ? sizeOf(*own).height() : HeaderHeight;
                Positions[node.Id] = Positions[*parent] + QPointF(Inset, header + 10) + relative[node.Id];
            }
        } else if (missing) {
            const QRectF bounds = contentBounds();
            qreal x = bounds.isNull() ? Margin : bounds.right() + ColumnGap;
            std::map<ElementId, int> nested;
            for (const auto &node: nodes) {
                if (Positions.count(node.Id))
                    continue;
                if (const auto parent = parentNode(node.Id); parent && Positions.count(*parent)) {
                    const int index = nested[*parent]++;
                    Positions[node.Id] = Positions[*parent] + QPointF(Inset + index * 30, HeaderHeight + 16 + index * 30);
                    continue;
                }
                Positions[node.Id] = QPointF(x, Margin);
                x += sizeOf(node).width() + ColumnGap;
            }
        }

        for (auto &pin: pins) {
            pin.Selected = SelectedNodes.count(pin.Id) > 0;
            const int row = Nodes->indexOf(pin.Id);
            if (row >= 0) {
                Nodes->updateContent(row, pin);
                Nodes->setSelected(row, pin.Selected);
            } else {
                pin.Rect = QRectF(0, 0, PinSize, PinSize); // placed on the border by applyRects
                Nodes->append(pin);
            }
        }
        for (auto &node: nodes) {
            node.Selected = SelectedNodes.count(node.Id) > 0;
            const QRectF rect(Positions[node.Id], sizeOf(node));
            const int row = Nodes->indexOf(node.Id);
            if (row >= 0) {
                Nodes->updateContent(row, node);
                Nodes->setSelected(row, node.Selected);
            } else {
                node.Rect = rect;
                Nodes->append(node);
            }
        }
        applyRects();
        for (auto it = SelectedEdges.begin(); it != SelectedEdges.end();) {
            const bool exists = std::any_of(edges.begin(), edges.end(), [&](const DiagramEdge &edge) { return edge.Key == *it; });
            it = exists ? std::next(it) : SelectedEdges.erase(it);
        }
        Edges->setEdges(std::move(edges));
        updateCanvasSize();
        emit selectionChanged();
    }

    QSizeF BehaviorController::layoutLevel(ElementId owner, const std::vector<DiagramNode> &nodes,
                                           const std::vector<DiagramEdge> &edges, std::map<ElementId, QPointF> &relative) {
        std::vector<const DiagramNode *> members;
        for (const auto &node: nodes) {
            const auto parent = parentNode(node.Id);
            if ((parent ? *parent : *context()) == owner)
                members.push_back(&node);
        }
        // sizes: a state with nested states around their area
        std::map<ElementId, QSizeF> sizes;
        for (const auto *member: members) {
            const QSizeF own = sizeOf(*member);
            const bool composite = std::any_of(nodes.begin(), nodes.end(), [&](const DiagramNode &other) {
                return parentNode(other.Id) == member->Id;
            });
            if (!composite) {
                sizes[member->Id] = own;
                continue;
            }
            const QSizeF inner = layoutLevel(member->Id, nodes, edges, relative);
            sizes[member->Id] = QSizeF(std::max(own.width(), inner.width() + 2 * Inset), own.height() + 10 + inner.height() + Inset);
        }
        // levels along the arrows between the members; the states of a parallel state side by side
        std::map<ElementId, int> level;
        const bool parallel = owner != *context() && Model.element(owner).isParallel();
        if (parallel) {
            for (const auto *member: members)
                level[member->Id] = 0;
        } else {
            std::set<ElementId> inLevel;
            for (const auto *member: members)
                inLevel.insert(member->Id);
            std::map<ElementId, int> incoming;
            for (const auto &edge: edges) {
                if (inLevel.count(edge.Source) && inLevel.count(edge.Target))
                    ++incoming[edge.Target];
            }
            std::deque<ElementId> open;
            const auto startFrom = [&](ElementId id) {
                if (level.count(id))
                    return;
                level[id] = 0;
                open.push_back(id);
                while (!open.empty()) {
                    const ElementId current = open.front();
                    open.pop_front();
                    for (const auto &edge: edges) {
                        if (edge.Source == current && inLevel.count(edge.Target) && level.count(edge.Target) == 0) {
                            level[edge.Target] = level[current] + 1;
                            open.push_back(edge.Target);
                        }
                    }
                }
            };
            for (const auto *member: members) {
                if (member->Kind == DiagramTypes::StartNode)
                    startFrom(member->Id);
            }
            for (const auto *member: members) {
                if (incoming[member->Id] == 0)
                    startFrom(member->Id);
            }
            for (const auto *member: members)
                startFrom(member->Id);
        }
        std::map<int, std::vector<const DiagramNode *>> rows;
        for (const auto *member: members)
            rows[level[member->Id]].push_back(member);
        qreal widest = 0;
        for (const auto &[row, inRow]: rows) {
            qreal width = -ColumnGap;
            for (const auto *node: inRow)
                width += sizes[node->Id].width() + ColumnGap;
            widest = std::max(widest, width);
        }
        if (owner == *context() && !LaneTitles.empty()) {
            // swimlanes: a column per performer (after those without one), the rows along the arrows across all
            const int laneCount = int(LaneTitles.size());
            const auto laneOf = [&](ElementId id) {
                const auto found = LaneOf.find(id);
                return found == LaneOf.end() ? -1 : found->second;
            };
            std::vector<qreal> laneWidth(std::size_t(laneCount + 1), 0.0); // index 0: no lane
            for (const auto &[row, inRow]: rows) {
                std::vector<qreal> width(std::size_t(laneCount + 1), -ColumnGap);
                for (const auto *node: inRow)
                    width[std::size_t(laneOf(node->Id) + 1)] += sizes[node->Id].width() + ColumnGap;
                for (std::size_t lane = 0; lane < width.size(); ++lane)
                    laneWidth[lane] = std::max(laneWidth[lane], width[lane]);
            }
            std::vector<qreal> laneLeft(laneWidth.size(), 0.0);
            qreal left = 0;
            for (std::size_t lane = 0; lane < laneWidth.size(); ++lane) {
                if (lane > 0)
                    laneWidth[lane] = std::max(laneWidth[lane], LaneTitles[lane - 1].size() * 7.5 + 20);
                laneLeft[lane] = left + (lane > 0 ? LanePadding : 0);
                if (laneWidth[lane] > 0 || lane > 0)
                    left = laneLeft[lane] + std::max<qreal>(laneWidth[lane], 0) + (lane > 0 ? LanePadding : ColumnGap);
            }
            qreal y = LaneHeader;
            for (const auto &[row, inRow]: rows) {
                qreal height = 0;
                std::vector<qreal> x(laneLeft);
                for (const auto *node: inRow) {
                    const auto lane = std::size_t(laneOf(node->Id) + 1);
                    const QSizeF size = sizes[node->Id];
                    relative[node->Id] = QPointF(x[lane], y + (size.height() < 60 ? (60 - size.height()) / 2 : 0));
                    x[lane] += size.width() + ColumnGap;
                    height = std::max(height, size.height());
                }
                y += std::max<qreal>(height, 60) + RowGap;
            }
            return {left, std::max<qreal>(y - RowGap, 0)};
        }
        qreal y = 0;
        for (const auto &[row, inRow]: rows) {
            qreal width = -ColumnGap;
            qreal height = 0;
            for (const auto *node: inRow) {
                width += sizes[node->Id].width() + ColumnGap;
                height = std::max(height, sizes[node->Id].height());
            }
            height = std::max<qreal>(height, 60);
            qreal x = (widest - width) / 2;
            for (const auto *node: inRow) {
                const QSizeF size = sizes[node->Id];
                // small nodes (control nodes, simple actions) centered in their row
                relative[node->Id] = QPointF(x, y + (size.height() < 60 ? (60 - size.height()) / 2 : 0));
                x += size.width() + ColumnGap;
            }
            y += height + RowGap;
        }
        return {std::max<qreal>(widest, 0), std::max<qreal>(y - RowGap, 0)};
    }

    void BehaviorController::applyRects() {
        // nested states first: their state grows around them
        std::vector<const DiagramNode *> order;
        for (const auto &node: Nodes->nodes()) {
            if (!isPin(node.Id))
                order.push_back(&node);
        }
        std::stable_sort(order.begin(), order.end(), [](const DiagramNode *a, const DiagramNode *b) { return a->Depth > b->Depth; });
        std::map<ElementId, QRectF> rects;
        for (const auto *node: order) {
            const QPointF position = Positions.count(node->Id) ? Positions[node->Id] : node->Rect.topLeft();
            QSizeF size = sizeOf(*node);
            QRectF inner;
            for (const auto &[id, rect]: rects) {
                if (parentNode(id) == node->Id)
                    inner = inner.isNull() ? rect : inner.united(rect);
            }
            if (!inner.isNull()) {
                size.setWidth(std::max(size.width(), inner.right() + Inset - position.x()));
                size.setHeight(std::max(size.height(), inner.bottom() + Inset - position.y()));
            }
            rects[node->Id] = QRectF(position, size);
        }
        QRectF bounds;
        for (const auto &[id, rect]: rects) {
            const int row = Nodes->indexOf(id);
            if (row >= 0)
                Nodes->setRect(row, rect);
            bounds = bounds.isNull() ? rect : bounds.united(rect);
        }
        // swimlanes around the actions of each performer, all of the same height and side by side
        QVariantList lanes;
        if (!LaneTitles.empty() && !bounds.isNull()) {
            std::vector<QRectF> areas(LaneTitles.size());
            for (const auto &[id, lane]: LaneOf) {
                const auto found = rects.find(id);
                if (found == rects.end() || lane < 0 || lane >= int(areas.size()))
                    continue;
                areas[std::size_t(lane)] = areas[std::size_t(lane)].isNull() ? found->second : areas[std::size_t(lane)].united(found->second);
            }
            const qreal top = bounds.top() - LaneHeader - 6;
            const qreal bottom = bounds.bottom() + 16;
            qreal previousRight = -1e9;
            for (std::size_t lane = 0; lane < areas.size(); ++lane) {
                if (areas[lane].isNull())
                    continue;
                qreal left = areas[lane].left() - LanePadding;
                if (left < previousRight + 0.5 && left > previousRight - LanePadding * 2)
                    left = previousRight; // attached to the lane before it
                const qreal right = std::max(areas[lane].right() + LanePadding, left + LaneTitles[lane].size() * 7.5 + 20);
                previousRight = right;
                const QRectF area(QPointF(left, top), QPointF(right, bottom));
                bounds = bounds.united(area);
                lanes.append(QVariantMap{{QStringLiteral("title"), LaneTitles[lane]}, {QStringLiteral("x"), area.x()},
                                         {QStringLiteral("y"), area.y()}, {QStringLiteral("width"), area.width()},
                                         {QStringLiteral("height"), area.height()}});
            }
        }
        const bool lanesChanged = lanes != Lanes;
        Lanes = lanes;
        // the frame of the context: "action transportPassenger", "state vehicleStates"
        QRectF frame;
        QString title;
        if (const auto start = context()) {
            const auto &element = Model.element(*start);
            title = q(keywordOf(element)) + QLatin1Char(' ') + q(element.name());
            if (!element.typeName().empty())
                title += QStringLiteral(" : ") + q(element.typeName());
            if (element.isParallel())
                title += QStringLiteral(" «parallel»");
            frame = bounds.isNull() ? QRectF(Margin - FramePadding, Margin - FramePadding - 20, 320, 200)
                                    : bounds.adjusted(-FramePadding, -FramePadding - 20, FramePadding, FramePadding);
            frame.setWidth(std::max(frame.width(), title.size() * 8.0 + 60));
            // room for the parameters of the context on the frame and their names inside it
            bool in = false;
            bool out = false;
            for (const auto &[pin, owner]: PinOwners) {
                if (owner != *start)
                    continue;
                const std::string direction = directionOf(Model.element(pin));
                in = in || direction == "in";
                out = out || direction == "out";
            }
            frame.adjust(in ? -70 : 0, 0, out ? 90 : 0, 0);
        }
        placePins(frame);
        if (frame != FrameRect || title != FrameTitle || lanesChanged) {
            FrameRect = frame;
            FrameTitle = title;
            emit frameChanged();
        }
    }

    void BehaviorController::placePins(QRectF &frame) {
        // per owner: "in" on the top border, "out" at the bottom, "inout" on the right, evenly spread
        std::map<ElementId, std::vector<ElementId>> byOwner;
        for (const auto &node: Nodes->nodes()) {
            if (const auto owner = PinOwners.find(node.Id); owner != PinOwners.end())
                byOwner[owner->second].push_back(node.Id);
        }
        const auto start = context();
        for (const auto &[owner, pins]: byOwner) {
            const bool onFrame = start && owner == *start;
            QRectF rect;
            if (onFrame) {
                if (frame.isNull())
                    continue;
                // the frame's own parameters: room beside the title tab, the frame wide enough for them
                rect = frame;
            } else if (const auto *node = Nodes->find(owner)) {
                rect = node->Rect;
            } else {
                continue;
            }
            std::map<std::string, std::vector<ElementId>> sides;
            for (const ElementId pin: pins)
                sides[directionOf(Model.element(pin))].push_back(pin);
            for (const auto &[direction, onSide]: sides) {
                const int count = int(onSide.size());
                for (int i = 0; i < count; ++i) {
                    QPointF center;
                    const qreal along = qreal(i + 1) / qreal(count + 1);
                    if (onFrame) {
                        // the parameters of the context on the frame: in on the left border, out on the right,
                        // inout at the bottom (below the title tab, the start is at the top)
                        const qreal top = rect.top() + 40;
                        if (direction == "in")
                            center = QPointF(rect.left(), top + (rect.bottom() - top) * along);
                        else if (direction == "out")
                            center = QPointF(rect.right(), top + (rect.bottom() - top) * along);
                        else
                            center = QPointF(rect.left() + rect.width() * along, rect.bottom());
                    } else if (direction == "inout") {
                        center = QPointF(rect.right(), rect.top() + rect.height() * along);
                    } else {
                        // in on top, out at the bottom; right of the middle, where the successions run
                        const qreal x = rect.left() + rect.width() * (0.4 + 0.6 * along);
                        center = QPointF(x, direction == "in" ? rect.top() : rect.bottom());
                    }
                    const int row = Nodes->indexOf(onSide[std::size_t(i)]);
                    if (row >= 0)
                        Nodes->setRect(row, QRectF(center - QPointF(PinSize / 2, PinSize / 2), QSizeF(PinSize, PinSize)));
                }
            }
        }
    }

    QString BehaviorController::pinSide(const QString &id) const {
        const auto pin = toNode(id);
        return pin ? pinSideOf(*pin) : QString();
    }

    QString BehaviorController::pinSideOf(ElementId pin) const {
        const auto owner = PinOwners.find(pin);
        if (owner == PinOwners.end() || !Model.contains(pin))
            return {};
        const std::string direction = directionOf(Model.element(pin));
        if (context() && owner->second == *context())
            return direction == "in" ? QStringLiteral("left") : direction == "out" ? QStringLiteral("right") : QStringLiteral("bottom");
        return direction == "in" ? QStringLiteral("top") : direction == "out" ? QStringLiteral("bottom") : QStringLiteral("right");
    }

    void BehaviorController::updateCanvasSize() {
        const QRectF bounds = contentBounds();
        const QSizeF size(std::max<qreal>(1600, bounds.right() + 600), std::max<qreal>(1000, bounds.bottom() + 600));
        if (size != CanvasSize) {
            CanvasSize = size;
            emit canvasSizeChanged();
        }
    }

    QRectF BehaviorController::contentBounds() const {
        QRectF bounds;
        for (const auto &node: Nodes->nodes())
            bounds = bounds.isNull() ? node.Rect : bounds.united(node.Rect);
        return bounds;
    }

    QString BehaviorController::contextKey() const {
        return context() ? DiagramLayout::keyOf(Model, *context()) : QString();
    }

    QJsonObject BehaviorController::currentLayout() const {
        QJsonObject layout;
        for (const auto &[id, position]: Positions) {
            if (Model.contains(id))
                layout.insert(DiagramLayout::keyOf(Model, id), DiagramLayout::toJson(position));
        }
        return layout;
    }

    void BehaviorController::saveContextLayout() {
        const QString key = contextKey();
        if (key.isEmpty())
            return;
        if (AutoLayout)
            SavedLayouts.remove(key);
        else
            SavedLayouts.insert(key, currentLayout());
    }

    void BehaviorController::applyContextLayout() {
        const QJsonObject layout = SavedLayouts.value(contextKey()).toObject();
        if (layout.isEmpty())
            return;
        const auto byKey = DiagramLayout::elementsByKey(Model);
        for (auto it = layout.begin(); it != layout.end(); ++it) {
            const auto element = byKey.find(it.key());
            const auto position = DiagramLayout::positionOf(it.value());
            if (element != byKey.end() && position)
                Positions[element->second] = *position;
        }
        if (!Positions.empty())
            AutoLayout = false;
    }

    QJsonObject BehaviorController::layoutState() const {
        QJsonObject layout = SavedLayouts;
        const QString key = contextKey();
        if (!key.isEmpty()) {
            if (AutoLayout)
                layout.remove(key);
            else
                layout.insert(key, currentLayout());
        }
        return layout;
    }

    void BehaviorController::restoreLayout(const QJsonObject &layout) {
        SavedLayouts = layout;
        Positions.clear();
        AutoLayout = true;
        applyContextLayout();
        syncNow();
    }

    void BehaviorController::layoutAll() {
        Positions.clear();
        AutoLayout = true;
        SavedLayouts.remove(contextKey());
        syncNow();
    }

    // --- lookup --------------------------------------------------------------------------------------

    std::optional<ElementId> BehaviorController::toNode(const QString &id) const {
        bool ok = false;
        const auto value = id.toULongLong(&ok);
        if (!ok || !Nodes->find(value))
            return std::nullopt;
        return value;
    }

    QString BehaviorController::nodeAt(qreal x, qreal y) const {
        const QPointF point(x, y);
        const auto &nodes = Nodes->nodes();
        // the pins on the border before the action they are on
        for (const auto &node: nodes) {
            if (isPin(node.Id) && node.Rect.adjusted(-4, -4, 4, 4).contains(point))
                return QString::number(node.Id);
        }
        for (auto it = nodes.rbegin(); it != nodes.rend(); ++it) {
            const qreal grow = it->Rect.height() < 20 ? 8 : 0; // the thin fork / join bars
            if (it->Rect.adjusted(-grow, -grow, grow, grow).contains(point))
                return QString::number(it->Id);
        }
        return {};
    }

    QString BehaviorController::edgeAt(qreal x, qreal y) const {
        const QPointF point(x, y);
        const auto &edges = Edges->edges();
        for (auto it = edges.rbegin(); it != edges.rend(); ++it) {
            const auto *source = Nodes->find(it->Source);
            const auto *target = Nodes->find(it->Target);
            if (!source || !target)
                continue;
            const auto route = source == target ? std::vector<QPointF>{source->Rect.topRight(), source->Rect.topRight()}
                                                : EdgeRouting::routeOf(*Nodes, edges, *it);
            if (EdgeRouting::distanceToRoute(point, route) <= EdgeHitDistance)
                return it->Key;
        }
        return {};
    }

    QRectF BehaviorController::nodeRect(const QString &node) const {
        const auto id = toNode(node);
        return id ? Nodes->find(*id)->Rect : QRectF();
    }

    QString BehaviorController::entryAt(const QString &node, qreal x, qreal y) const {
        const auto id = toNode(node);
        if (!id)
            return {};
        const auto *found = Nodes->find(*id);
        if (found->Kind != DiagramTypes::StateNode || found->Compartments.isEmpty())
            return {};
        // as BehaviorNode draws them: per compartment a line with its caption, then the entries
        Q_UNUSED(x)
        qreal top = found->Rect.top() + HeaderHeight + (found->Keyword.isEmpty() ? 0 : 6);
        for (const auto &compartment: found->Compartments) {
            const auto entries = compartment.toMap().value(QStringLiteral("entries")).toList();
            top += CaptionHeight;
            const int index = int(std::floor((y - top) / EntryHeight));
            if (y >= top && index < entries.size())
                return entries.at(index).toMap().value(QStringLiteral("entryId")).toString();
            top += qreal(entries.size()) * EntryHeight;
        }
        return {};
    }

    // --- selection and moving ------------------------------------------------------------------------

    void BehaviorController::applySelection() {
        for (int row = 0; row < Nodes->count(); ++row)
            Nodes->setSelected(row, SelectedNodes.count(Nodes->nodes()[std::size_t(row)].Id) > 0);
        auto edges = Edges->edges();
        for (auto &edge: edges)
            edge.Selected = SelectedEdges.count(edge.Key) > 0;
        Edges->setEdges(std::move(edges));
        emit selectionChanged();
    }

    void BehaviorController::select(const QString &node, bool additive) {
        if (!additive) {
            SelectedNodes.clear();
            SelectedEdges.clear();
        }
        if (const auto id = toNode(node))
            SelectedNodes.insert(*id);
        applySelection();
    }

    void BehaviorController::selectEdge(const QString &key, bool additive) {
        if (!additive) {
            SelectedNodes.clear();
            SelectedEdges.clear();
        }
        if (Edges->find(key))
            SelectedEdges.insert(key);
        applySelection();
    }

    void BehaviorController::clearSelection() {
        SelectedNodes.clear();
        SelectedEdges.clear();
        applySelection();
    }

    void BehaviorController::moveNode(const QString &node, qreal x, qreal y) {
        const auto id = toNode(node);
        if (!id || isPin(*id)) // a pin stays on the border of its action
            return;
        AutoLayout = false;
        emit layoutEdited();
        QPointF wanted(std::max<qreal>(0, x), std::max<qreal>(0, y));
        // a nested state stays inside its state (which grows to the right and downwards)
        if (const auto parent = parentNode(*id); parent && Positions.count(*parent)) {
            const auto *parentNodeData = Nodes->find(*parent);
            const qreal header = parentNodeData ? sizeOf(*parentNodeData).height() : HeaderHeight;
            wanted.setX(std::max(wanted.x(), Positions[*parent].x() + 4));
            wanted.setY(std::max(wanted.y(), Positions[*parent].y() + header));
        }
        const QPointF delta = wanted - (Positions.count(*id) ? Positions[*id] : wanted);
        Positions[*id] = wanted;
        // the states nested in it move along
        for (auto &[other, position]: Positions) {
            if (other == *id || !Model.contains(other))
                continue;
            for (auto parent = parentNode(other); parent; parent = parentNode(*parent)) {
                if (*parent == *id) {
                    position += delta;
                    break;
                }
            }
        }
        applyRects();
    }

    // --- editing -------------------------------------------------------------------------------------

    bool BehaviorController::runModelChange(const std::function<void()> &change) {
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

    std::string BehaviorController::uniqueName(const std::string &base, ElementId owner) const {
        const auto &siblings = Model.children(owner);
        for (int index = 1;; ++index) {
            const std::string candidate = base + std::to_string(index);
            if (std::none_of(siblings.begin(), siblings.end(),
                             [&](ElementId sibling) { return Model.element(sibling).name() == candidate; }))
                return candidate;
        }
    }

    QString BehaviorController::addNode(int tool, qreal x, qreal y) {
        const auto start = context();
        if (!start) {
            emit statusMessage(tr("Create an action or state definition in the structure view first."));
            return {};
        }
        const QPointF position(std::max<qreal>(0, x - 40), std::max<qreal>(0, y - 18));
        std::optional<ElementId> id;
        QString message;
        const bool ok = runModelChange([&] {
            AutoLayout = false;
            switch (tool) {
                case DiagramTypes::ToolActionUsage: {
                    // onto a state: its "do action"
                    const auto target = toNode(nodeAt(x, y));
                    if (target && Model.element(*target).kind() == ElementKind::StateUsage) {
                        id = Model.addElement(ElementKind::ActionUsage, uniqueName("action", *target), *target);
                        Model.setDeclaredKeyword(*id, std::string("do action"));
                        message = tr("Added a do action - double-click it in the state to make it an entry or exit action.");
                        return;
                    }
                    id = Model.addElement(ElementKind::ActionUsage, uniqueName("action", *start), *start);
                    break;
                }
                case DiagramTypes::ToolState:
                    id = Model.addElement(ElementKind::StateUsage, uniqueName("state", *start), *start);
                    break;
                case DiagramTypes::ToolSendAction:
                case DiagramTypes::ToolAcceptAction:
                case DiagramTypes::ToolAssignAction:
                case DiagramTypes::ToolTerminateAction:
                case DiagramTypes::ToolIfAction:
                case DiagramTypes::ToolWhileAction:
                case DiagramTypes::ToolLoopAction:
                case DiagramTypes::ToolForAction: {
                    // unnamed, as usually written ("then send s to t;"); named when an arrow needs it
                    const auto form = tool == DiagramTypes::ToolSendAction ? Diagram::ActionForm::Send
                                      : tool == DiagramTypes::ToolAcceptAction ? Diagram::ActionForm::Accept
                                      : tool == DiagramTypes::ToolAssignAction ? Diagram::ActionForm::Assign
                                      : tool == DiagramTypes::ToolTerminateAction ? Diagram::ActionForm::Terminate
                                      : tool == DiagramTypes::ToolIfAction ? Diagram::ActionForm::If
                                      : tool == DiagramTypes::ToolWhileAction ? Diagram::ActionForm::While
                                      : tool == DiagramTypes::ToolLoopAction ? Diagram::ActionForm::Loop
                                                                             : Diagram::ActionForm::For;
                    const std::string clause = form == Diagram::ActionForm::Send ? "Signal() to target"
                                               : form == Diagram::ActionForm::Accept ? "Signal"
                                               : form == Diagram::ActionForm::Assign ? "x := 0"
                                               : form == Diagram::ActionForm::If || form == Diagram::ActionForm::While
                                               ? "condition"
                                               : form == Diagram::ActionForm::For ? "x in values" : "";
                    id = Model.addElement(ElementKind::ActionUsage, std::string(), *start);
                    Model.setActionForm(*id, form, clause);
                    message = tr("Double-click it to edit what it does.");
                    break;
                }
                case DiagramTypes::ToolDecision:
                case DiagramTypes::ToolMerge:
                case DiagramTypes::ToolFork:
                case DiagramTypes::ToolJoin: {
                    const std::string keyword = tool == DiagramTypes::ToolDecision ? "decide"
                                                : tool == DiagramTypes::ToolMerge ? "merge"
                                                : tool == DiagramTypes::ToolFork ? "fork" : "join";
                    id = Model.addElement(ElementKind::ControlNode, uniqueName(keyword, *start), *start);
                    Model.setDeclaredKeyword(*id, keyword);
                    break;
                }
                case DiagramTypes::ToolStart:
                case DiagramTypes::ToolDone:
                case DiagramTypes::ToolInitial: {
                    const std::string keyword = tool == DiagramTypes::ToolStart ? "start"
                                                : tool == DiagramTypes::ToolDone ? "done" : "entry";
                    for (const ElementId child: Model.children(*start)) {
                        if (Model.element(child).kind() == ElementKind::ControlNode && Model.element(child).declaredKeyword() == keyword)
                            throw Diagram::ModelError("There is already a " + keyword + " node.");
                    }
                    id = Model.addElement(ElementKind::ControlNode, keyword, *start);
                    Model.setDeclaredKeyword(*id, keyword);
                    if (keyword != "entry")
                        message = tr("Draw an arrow from or to it - without one it is not written into the text.");
                    break;
                }
                default:
                    return;
            }
            Positions[*id] = position;
        });
        if (!ok || !id)
            return {};
        if (!message.isEmpty())
            emit statusMessage(message);
        return QString::number(*id);
    }

    QString BehaviorController::connectNodes(const QString &source, const QString &target) {
        const auto from = toNode(source);
        const auto to = toNode(target);
        if (!from || !to)
            return tr("Draw the arrow onto another node.");
        if (isPin(*from) || isPin(*to)) {
            // from a parameter to a parameter: "flow from pump.fuelOut to burn.fuelIn;"
            if (!isPin(*from) || !isPin(*to))
                return tr("A flow goes from a parameter (pin) to another parameter.");
            if (const auto rule = Diagram::RelationshipRules::canRelate(Model, RelationshipKind::Flow, *from, *to); !rule)
                return QString::fromStdString(rule.Reason);
            runModelChange([&] { Model.addRelationship(RelationshipKind::Flow, *from, *to); });
            emit statusMessage(tr("Flow created."));
            return {};
        }
        // state to state, and from a state into "done" (the final state): a transition
        const bool states = Model.element(*from).kind() == ElementKind::StateUsage &&
                            (Model.element(*to).kind() == ElementKind::StateUsage ||
                             (Model.element(*to).kind() == ElementKind::ControlNode &&
                              Model.element(*to).declaredKeyword() == "done"));
        const RelationshipKind kind = states ? RelationshipKind::Transition : RelationshipKind::Succession;
        // an unnamed action ("send s to t;") that is not declared right after the source gets a name to be referenced
        const auto &targetElement = Model.element(*to);
        const bool needsName = targetElement.name().empty() &&
                               !Diagram::RelationshipRules::canRelate(Model, kind, *from, *to) &&
                               (targetElement.kind() == ElementKind::ActionUsage ||
                                targetElement.kind() == ElementKind::ControlNode);
        if (needsName) {
            std::string base = targetElement.kind() == ElementKind::ControlNode ? keywordOf(targetElement)
                                                                                   : Diagram::toKeyword(targetElement.actionForm());
            if (base.empty())
                base = "action";
            const std::string name = uniqueName(base, targetElement.owner());
            try {
                Model.rename(*to, name);
            } catch (const std::exception &) {
            }
        }
        if (const auto rule = Diagram::RelationshipRules::canRelate(Model, kind, *from, *to); !rule)
            return QString::fromStdString(rule.Reason);
        runModelChange([&] { Model.addRelationship(kind, *from, *to); });
        emit statusMessage(states ? tr("Transition created - double-click it for trigger, guard and effect.")
                                  : tr("Succession created - double-click it for a guard."));
        return {};
    }

    void BehaviorController::deleteEdge(const QString &key) {
        if (!key.startsWith(QStringLiteral("r:")))
            return;
        const auto id = key.mid(2).toULongLong();
        runModelChange([&] {
            if (Model.containsRelationship(id))
                Model.removeRelationship(id);
        });
    }

    void BehaviorController::deleteSelection() {
        const auto edges = SelectedEdges;
        const auto nodes = SelectedNodes;
        SelectedEdges.clear();
        SelectedNodes.clear();
        runModelChange([&] {
            for (const auto &key: edges) {
                const auto id = key.mid(2).toULongLong();
                if (Model.containsRelationship(id))
                    Model.removeRelationship(id);
            }
            for (const ElementId node: nodes) {
                if (Model.contains(node))
                    Model.removeElement(node);
            }
        });
    }
}
