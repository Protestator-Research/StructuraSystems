//
// BehaviorController: actions, control nodes, states and the arrows between them - read and written.
//

#include "../TestFramework.h"
#include "QtPrinting.h"

#include <BehaviorController.h>
#include <DiagramController.h>

#include <Diagram/Conversion/SysMLCodeGenerator.h>
#include <Diagram/Conversion/SysMLTextImporter.h>
#include <Diagram/Semantic/SemanticModel.h>

using namespace StructuraSystems::Client;
using namespace StructuraSystems::Diagram;

namespace {
    std::optional<ElementId> find(const SemanticModel &model, ElementId owner, const std::string &name) {
        for (const ElementId id: model.children(owner)) {
            if (model.element(id).name() == name)
                return id;
        }
        return std::nullopt;
    }

    QString key(ElementId id) {
        return QString::number(id);
    }

    std::string generated(const SemanticModel &model) {
        return SysMLCodeGenerator().generate(model);
    }

    int count(const BehaviorController &controller, DiagramTypes::NodeKind kind) {
        int result = 0;
        for (const auto &node: controller.nodes()->nodes())
            result += node.Kind == kind ? 1 : 0;
        return result;
    }
}

TEST_CASE("Behavior: an action flow with decision, fork and join") {
    SemanticModel model;
    CHECK(SysMLTextImporter::importText(
            "action def ProvidePower {\n"
            "    first start;\n"
            "    then check;\n"
            "    action check;\n"
            "    then decision;\n"
            "    decide decision;\n"
            "    if load > 0.8 then boost;\n"
            "    else normal;\n"
            "    action boost;\n"
            "    then split;\n"
            "    action normal;\n"
            "    then split;\n"
            "    fork split;\n"
            "    then a;\n"
            "    then b;\n"
            "    action a;\n"
            "    action b;\n"
            "    then done;\n"
            "}\n", model).lossless());
    BehaviorController behavior(model);
    CHECK(!behavior.stateMachine());
    // start, check, decision, boost, normal, split, a, b, done
    CHECK_EQ(behavior.nodes()->count(), 9);
    CHECK_EQ(count(behavior, DiagramTypes::DecisionNode), 1);
    CHECK_EQ(count(behavior, DiagramTypes::ForkNode), 1);
    CHECK_EQ(count(behavior, DiagramTypes::StartNode), 1);
    CHECK_EQ(count(behavior, DiagramTypes::DoneNode), 1);
    CHECK_EQ(behavior.edges()->count(), 9);
    QStringList labels;
    for (const auto &edge: behavior.edges()->edges())
        if (!edge.Label.isEmpty())
            labels.append(edge.Label);
    CHECK_EQ(labels, QStringList({QString("[load > 0.8]"), QString("[else]")}));
    // the start is above the decision, the decision above its branches
    const ElementId context = behavior.contextId().toULongLong();
    const QRectF start = behavior.nodeRect(key(*find(model, context, "start")));
    const QRectF decision = behavior.nodeRect(key(*find(model, context, "decision")));
    const QRectF boost = behavior.nodeRect(key(*find(model, context, "boost")));
    CHECK(start.top() < decision.top());
    CHECK(decision.top() < boost.top());
}

TEST_CASE("Behavior: drawing an action flow writes the SysML text") {
    SemanticModel model;
    DiagramController structure(model);
    const QString definition = structure.addElement(DiagramTypes::ToolActionDefinition, 100, 100);
    BehaviorController behavior(model);
    behavior.setContextId(definition);
    CHECK_EQ(behavior.contextId(), definition);

    const QString start = behavior.addNode(DiagramTypes::ToolStart, 100, 50);
    const QString first = behavior.addNode(DiagramTypes::ToolActionUsage, 100, 150);
    const QString decision = behavior.addNode(DiagramTypes::ToolDecision, 100, 250);
    const QString second = behavior.addNode(DiagramTypes::ToolActionUsage, 50, 350);
    const QString done = behavior.addNode(DiagramTypes::ToolDone, 100, 450);
    CHECK(behavior.addNode(DiagramTypes::ToolStart, 300, 50).isEmpty()); // only one start
    CHECK(behavior.connectNodes(start, first).isEmpty());
    CHECK(behavior.connectNodes(first, decision).isEmpty());
    CHECK(behavior.connectNodes(decision, second).isEmpty());
    CHECK(behavior.connectNodes(decision, done).isEmpty());
    CHECK(behavior.connectNodes(second, done).isEmpty());
    CHECK_EQ(behavior.edges()->count(), 5);

    // a guard on one branch, "else" on the other
    QString toSecond;
    QString toDone;
    for (const auto &edge: behavior.edges()->edges()) {
        if (key(edge.Source) == decision && key(edge.Target) == second)
            toSecond = edge.Key;
        if (key(edge.Source) == decision && key(edge.Target) == done)
            toDone = edge.Key;
    }
    CHECK(structure.edgeDetails(toSecond).value("hasGuard").toBool());
    CHECK(structure.updateEdge(toSecond, {{"name", ""}, {"typeName", ""}, {"guard", "x > 0"}}).isEmpty());
    CHECK(structure.updateEdge(toDone, {{"name", ""}, {"typeName", ""}, {"guard", "else"}}).isEmpty());

    const std::string text = generated(model);
    CHECK_EQ(text, std::string("action def Action1 {\n"
                               "    first start;\n"
                               "    then action1;\n"
                               "    action action1;\n"
                               "    then decide1;\n"
                               "    decide decide1;\n"
                               "    if x > 0 then action2;\n"
                               "    else done;\n"
                               "    action action2;\n"
                               "    then done;\n"
                               "}\n"));
    // read back: the same flow
    SemanticModel again;
    CHECK(SysMLTextImporter::importText(text, again).lossless());
    CHECK_EQ(generated(again), text);

    // deleting
    behavior.select(second, false);
    behavior.deleteSelection();
    CHECK(generated(model).find("action2") == std::string::npos);
}

TEST_CASE("Behavior: a state machine with initial state, transitions and entry/do/exit actions") {
    SemanticModel model;
    CHECK(SysMLTextImporter::importText(
            "state def EngineStates {\n"
            "    entry;\n"
            "    then off;\n"
            "    state off;\n"
            "    state on {\n"
            "        entry action warmUp;\n"
            "        do action run;\n"
            "    }\n"
            "    transition first off accept StartSignal if fuel > 0 do action ignite then on;\n"
            "}\n", model).lossless());
    DiagramController structure(model);
    BehaviorController behavior(model);
    CHECK(behavior.stateMachine());
    CHECK_EQ(behavior.nodes()->count(), 3); // entry, off, on
    const ElementId context = behavior.contextId().toULongLong();
    const ElementId on = *find(model, context, "on");
    const auto *onNode = behavior.nodes()->find(on);
    CHECK(onNode->Kind == DiagramTypes::StateNode);
    CHECK_EQ(onNode->Compartments.size(), 1);
    const auto entries = onNode->Compartments.front().toMap().value("entries").toList();
    CHECK_EQ(entries.at(0).toMap().value("text").toString(), QString("entry / warmUp"));
    CHECK_EQ(entries.at(1).toMap().value("text").toString(), QString("do / run"));
    QString transition;
    for (const auto &edge: behavior.edges()->edges())
        if (edge.Kind == DiagramTypes::Transition)
            transition = edge.Label;
    CHECK_EQ(transition, QString("StartSignal [fuel > 0] / action ignite"));
    // the entry action can be found in the state for editing
    const QString warmUp = behavior.entryAt(key(on), onNode->Rect.center().x(), onNode->Rect.top() + 40 + 4 + 9);
    CHECK_EQ(warmUp, key(*find(model, on, "warmUp")));

    // new state, transition back, an exit action dropped onto the state
    const QString stopped = behavior.addNode(DiagramTypes::ToolState, 500, 300);
    CHECK(behavior.connectNodes(key(on), stopped).isEmpty());
    const QString action = behavior.addNode(DiagramTypes::ToolActionUsage, behavior.nodeRect(stopped).center().x(),
                                            behavior.nodeRect(stopped).center().y());
    CHECK_EQ(model.element(action.toULongLong()).owner(), stopped.toULongLong());
    CHECK(structure.elementDetails(action).value("stateAction").toBool());
    CHECK(structure.updateElement(action, {{"name", "coolDown"}, {"typeName", ""}, {"multiplicity", ""},
                                           {"prefixes", ""}, {"role", "exit"}}).isEmpty());
    QString newTransition;
    for (const auto &edge: behavior.edges()->edges())
        if (key(edge.Source) == key(on) && key(edge.Target) == stopped)
            newTransition = edge.Key;
    CHECK(structure.edgeDetails(newTransition).value("hasTrigger").toBool());
    CHECK(structure.updateEdge(newTransition, {{"name", "stop"}, {"typeName", ""}, {"guard", ""},
                                               {"trigger", "StopSignal"}, {"effect", ""}}).isEmpty());
    const std::string text = generated(model);
    CHECK(text.find("    state state1 {\n        exit action coolDown;\n    }\n") != std::string::npos);
    CHECK(text.find("    transition stop first on accept StopSignal then state1;\n") != std::string::npos);
    SemanticModel again;
    CHECK(SysMLTextImporter::importText(text, again).lossless());
    CHECK_EQ(generated(again), text);

    // the initial state only once; a transition needs two states
    CHECK(behavior.addNode(DiagramTypes::ToolInitial, 10, 10).isEmpty());
    const QString initial = key(*find(model, context, "entry"));
    CHECK(behavior.connectNodes(initial, stopped).isEmpty()); // a second initial succession is allowed
}
