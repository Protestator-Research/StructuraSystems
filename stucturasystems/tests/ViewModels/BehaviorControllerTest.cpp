//
// BehaviorController: actions, control nodes, states and the arrows between them - read and written.
//

#include "../TestFramework.h"
#include "QtPrinting.h"

#include <BehaviorController.h>
#include <QJsonObject>
#include <DiagramController.h>
#include <EdgeRouting.h>

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
    CHECK_EQ(entries.at(0).toMap().value("text").toString(), QString("entry warmUp"));
    CHECK_EQ(entries.at(1).toMap().value("text").toString(), QString("do run"));
    QString transition;
    for (const auto &edge: behavior.edges()->edges())
        if (edge.Kind == DiagramTypes::Transition)
            transition = edge.Label;
    CHECK_EQ(transition, QString("StartSignal [fuel > 0] / action ignite"));
    // the entry action can be found in the state for editing (header 40, keyword 6, caption 16)
    const QString warmUp = behavior.entryAt(key(on), onNode->Rect.center().x(), onNode->Rect.top() + 40 + 6 + 16 + 9);
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

TEST_CASE("Behavior: send, accept, assign, if and loop actions are shown and drawn") {
    SemanticModel model;
    CHECK(SysMLTextImporter::importText(
            "action def ChargeBattery {\n"
            "    first start;\n"
            "    then accept Plugged;\n"
            "    loop action charging {\n"
            "        action monitor;\n"
            "        then if charge < 100 {\n"
            "            assign charge := charge + 1;\n"
            "        }\n"
            "    } until charge >= 100;\n"
            "    then send Charged() to driver;\n"
            "    then done;\n"
            "}\n", model).lossless());
    BehaviorController behavior(model);
    const ElementId definition = model.children(RootId).front();
    behavior.setContextId(key(definition));
    // start, accept, charging, send, done
    CHECK_EQ(behavior.nodes()->count(), 5);
    QStringList keywords;
    for (const auto &node: behavior.nodes()->nodes())
        keywords.append(node.Keyword);
    CHECK(keywords.contains("«accept action»"));
    CHECK(keywords.contains("«loop»"));
    CHECK(keywords.contains("«send action»"));
    const ElementId charging = *find(model, definition, "charging");
    // «loop» charging with the compartments 'loop body' and 'until condition' (graphical BNF)
    const DiagramNode *loopNode = behavior.nodes()->find(charging);
    CHECK_EQ(loopNode->Title, QString("charging"));
    CHECK_EQ(DiagramNodeModel::entryCount(*loopNode, "loop body"), 2);
    CHECK_EQ(DiagramNodeModel::entryCount(*loopNode, "until condition"), 1);
    // the body of the loop and of the if can be chosen as well
    int bodies = 0;
    for (const auto &context: behavior.contexts()) {
        const QString title = context.toMap().value("title").toString();
        bodies += title.startsWith("loop ") || title.startsWith("if ") ? 1 : 0;
    }
    CHECK_EQ(bodies, 2);

    // new actions from the palette: unnamed, named as soon as an arrow needs a name
    const QString send = behavior.addNode(DiagramTypes::ToolSendAction, 400, 400);
    CHECK(!send.isEmpty());
    CHECK(model.element(send.toULongLong()).name().empty());
    CHECK(generated(model).find("    send Signal() to target;\n") != std::string::npos);
    const ElementId accept = model.children(definition)[1];
    CHECK(behavior.connectNodes(key(accept), send).isEmpty()); // not declared right after accept: named
    CHECK(generated(model).find("    then send1;\n") != std::string::npos);
    CHECK(generated(model).find("    action send1 send Signal() to target;\n") != std::string::npos);
    const QString loop = behavior.addNode(DiagramTypes::ToolLoopAction, 500, 500);
    CHECK(behavior.connectNodes(send, loop).isEmpty()); // right after send: "then loop { }"
    CHECK(generated(model).find("    then loop {\n    }\n") != std::string::npos);

    // edited in the element dialog: what it does
    DiagramController structure(model);
    QVariantMap details = structure.elementDetails(loop);
    CHECK(details.value("isActionUsage").toBool());
    CHECK_EQ(details.value("actionForm").toString(), QString("loop"));
    details["actionForm"] = "while";
    details["actionClause"] = "n < 3";
    details["until"] = "ready";
    CHECK(structure.updateElement(loop, details).isEmpty());
    CHECK(generated(model).find("    then while n < 3 {\n    } until ready;\n") != std::string::npos);
    details["actionClause"] = "";
    CHECK(!structure.updateElement(loop, details).isEmpty()); // a while needs a condition

    SemanticModel again;
    CHECK(SysMLTextImporter::importText(generated(model), again).lossless());
    CHECK_EQ(generated(again), generated(model));
}

TEST_CASE("Behavior: parallel states, do and entry actions with a form") {
    SemanticModel model;
    CHECK(SysMLTextImporter::importText(
            "state vehicleStates parallel {\n"
            "    state healthStates {\n"
            "        do senseTemperature;\n"
            "        state normal;\n"
            "        state maintenance {\n"
            "            entry assign maintenanceTime := maintenanceTime + interval;\n"
            "        }\n"
            "        transition first normal accept at maintenanceTime then maintenance;\n"
            "        transition first maintenance accept after 48 [h] then normal;\n"
            "    }\n"
            "}\n", model).lossless());
    BehaviorController behavior(model);
    const ElementId machine = model.children(RootId).front();
    const ElementId health = *find(model, machine, "healthStates");
    behavior.setContextId(key(health));
    CHECK(behavior.stateMachine());
    CHECK_EQ(behavior.nodes()->count(), 2); // normal, maintenance ("do" is not a node)
    const ElementId maintenance = *find(model, health, "maintenance");
    CHECK_EQ(DiagramNodeModel::entryCount(*behavior.nodes()->find(maintenance), "state actions"), 1);
    QStringList labels;
    for (const auto &edge: behavior.edges()->edges())
        labels.append(edge.Label);
    CHECK(labels.contains("at maintenanceTime"));
    CHECK(labels.contains("after 48 [h]"));

    DiagramController structure(model);
    QVariantMap details = structure.elementDetails(key(machine));
    CHECK(details.value("isState").toBool());
    CHECK(details.value("parallel").toBool());
    details["parallel"] = false;
    CHECK(structure.updateElement(key(machine), details).isEmpty());
    CHECK(generated(model).rfind("state vehicleStates {\n", 0) == 0);
}

TEST_CASE("Behavior: positions are kept per action and restored") {
    const std::string text = "action def A {\n    first start;\n    then action a;\n    then action b;\n}\n"
                             "action def B {\n    action c;\n}\n";
    SemanticModel model;
    CHECK(SysMLTextImporter::importText(text, model).lossless());
    BehaviorController behavior(model);
    const ElementId a = model.children(RootId)[0];
    const ElementId b = model.children(RootId)[1];
    behavior.setContextId(key(a));
    CHECK(behavior.layoutState().isEmpty()); // automatic layout: nothing to store
    const ElementId actionA = *find(model, a, "a");
    behavior.moveNode(key(actionA), 300, 250);
    behavior.setContextId(key(b)); // the positions of A are kept while B is shown
    const QJsonObject layout = behavior.layoutState();
    CHECK(layout.contains("A"));
    CHECK(layout.value("A").toObject().contains("A::a"));

    SemanticModel again;
    CHECK(SysMLTextImporter::importText(text, again).lossless());
    BehaviorController restored(again);
    restored.restoreLayout(layout);
    restored.setContextId(key(again.children(RootId)[0]));
    CHECK(restored.nodeRect(key(*find(again, again.children(RootId)[0], "a"))).topLeft() == QPointF(300, 250));
}

TEST_CASE("Behavior: nested and parallel states inside their state, keywords and the frame (slides 64, 65)") {
    SemanticModel model;
    CHECK(SysMLTextImporter::importText(
            "state vehicleStates parallel {\n"
            "    state operatingStates {\n"
            "        entry;\n"
            "        then off;\n"
            "        state off;\n"
            "        state on {\n"
            "            entry action performSelfTest;\n"
            "            assert constraint { electricalPower <= 500 }\n"
            "        }\n"
            "        transition first off then on;\n"
            "    }\n"
            "    state healthStates {\n"
            "        state normal;\n"
            "    }\n"
            "}\n", model).lossless());
    BehaviorController behavior(model);
    const ElementId machine = model.children(RootId).front();
    behavior.setContextId(key(machine));
    CHECK_EQ(behavior.frameTitle(), QString("state vehicleStates «parallel»"));
    const ElementId operating = *find(model, machine, "operatingStates");
    const ElementId health = *find(model, machine, "healthStates");
    const ElementId on = *find(model, operating, "on");
    const ElementId normal = *find(model, health, "normal");
    // operatingStates, healthStates and their nested states (entry, off, on, normal)
    CHECK_EQ(behavior.nodes()->count(), 6);
    const QRectF operatingRect = behavior.nodes()->find(operating)->Rect;
    const QRectF healthRect = behavior.nodes()->find(health)->Rect;
    CHECK(operatingRect.contains(behavior.nodes()->find(on)->Rect));
    CHECK(healthRect.contains(behavior.nodes()->find(normal)->Rect));
    CHECK(!operatingRect.intersects(healthRect));
    CHECK(std::abs(operatingRect.top() - healthRect.top()) < 1); // parallel: side by side
    CHECK(behavior.frameRect().contains(operatingRect.united(healthRect)));
    CHECK_EQ(behavior.nodes()->find(on)->Keyword, QString("«state»"));
    CHECK_EQ(DiagramNodeModel::entryCount(*behavior.nodes()->find(on), "state actions"), 1);
    CHECK_EQ(DiagramNodeModel::entryCount(*behavior.nodes()->find(on), "assert constraints"), 1);
    // moving a state moves its nested states
    const QRectF before = behavior.nodes()->find(on)->Rect;
    behavior.moveNode(key(operating), operatingRect.x() + 50, operatingRect.y() + 30);
    const QRectF after = behavior.nodes()->find(on)->Rect;
    CHECK(std::abs(after.x() - before.x() - 50) < 0.5);
    CHECK(std::abs(after.y() - before.y() - 30) < 0.5);
}

TEST_CASE("Behavior: input / output flows between actions (slide 57)") {
    SemanticModel model;
    CHECK(SysMLTextImporter::importText(
            "action providePower {\n"
            "    action generateTorque {\n"
            "        out torque : Torque;\n"
            "    }\n"
            "    action amplifyTorque {\n"
            "        in torque : Torque;\n"
            "    }\n"
            "    flow of Torque from generateTorque.torque to amplifyTorque.torque;\n"
            "    first generateTorque then amplifyTorque;\n"
            "}\n", model).lossless());
    BehaviorController behavior(model);
    int flows = 0;
    for (const auto &edge: behavior.edges()->edges()) {
        if (edge.Kind != DiagramTypes::Flow)
            continue;
        ++flows;
        CHECK_EQ(edge.Label, QString("of Torque"));
        // from pin to pin: the parameters on the border of their actions (graphical BNF: param-t / param-b)
        CHECK_EQ(model.element(edge.Source).name(), std::string("torque"));
        CHECK_EQ(model.element(model.element(edge.Source).owner()).name(), std::string("generateTorque"));
        CHECK_EQ(model.element(model.element(edge.Target).owner()).name(), std::string("amplifyTorque"));
        CHECK(edge.FilledHead);
        CHECK(!edge.Dashed);
    }
    CHECK_EQ(flows, 1);
}

TEST_CASE("Behavior: parameters as pins on the actions and the frame, flows and succession flows between them") {
    const std::string text =
            "action def ProvidePower {\n"
            "    in item fuelIn : Fuel;\n"
            "    out item torque : Fuel;\n"
            "    action pump {\n"
            "        in item cmd : Fuel;\n"
            "        out item fuelOut : Fuel;\n"
            "    }\n"
            "    action burn {\n"
            "        in item fuelIn : Fuel;\n"
            "        out item heat : Fuel;\n"
            "        inout item level : Fuel;\n"
            "    }\n"
            "    first start then pump;\n"
            "    first pump then burn;\n"
            "    flow of Fuel from pump.fuelOut to burn.fuelIn;\n"
            "    succession flow from burn.heat to torque;\n"
            "    flow from fuelIn to pump.cmd;\n"
            "}\n";
    SemanticModel model;
    CHECK(SysMLTextImporter::importText(text, model).lossless());
    BehaviorController behavior(model);
    const ElementId providePower = model.children(RootId).at(0);
    const ElementId fuelIn = model.children(providePower).at(0);
    const ElementId torque = model.children(providePower).at(1);
    const ElementId pump = model.children(providePower).at(2);
    const ElementId burn = model.children(providePower).at(3);
    const ElementId cmd = model.children(pump).at(0);
    const ElementId fuelOut = model.children(pump).at(1);
    const ElementId level = model.children(burn).at(2);
    // pins: on top of the action for "in", at its bottom for "out", on its right for "inout"
    for (const ElementId pin: {cmd, fuelOut, level, fuelIn, torque})
        CHECK(behavior.nodes()->find(pin) != nullptr);
    const QRectF pumpRect = behavior.nodes()->find(pump)->Rect;
    const QRectF burnRect = behavior.nodes()->find(burn)->Rect;
    CHECK(std::abs(behavior.nodes()->find(cmd)->Rect.center().y() - pumpRect.top()) < 0.5);
    CHECK(std::abs(behavior.nodes()->find(fuelOut)->Rect.center().y() - pumpRect.bottom()) < 0.5);
    CHECK(std::abs(behavior.nodes()->find(level)->Rect.center().x() - burnRect.right()) < 0.5);
    CHECK_EQ(behavior.pinSide(key(cmd)), QString("top"));
    CHECK_EQ(behavior.pinSide(key(fuelOut)), QString("bottom"));
    CHECK_EQ(behavior.pinSide(key(level)), QString("right"));
    CHECK_EQ(behavior.nodes()->find(cmd)->Keyword, QString("in"));
    CHECK_EQ(behavior.nodes()->find(cmd)->Title, QString("cmd : Fuel"));
    // the parameters of the context on its frame: in on the left, out on the right
    const QRectF frame = behavior.frameRect();
    CHECK(std::abs(behavior.nodes()->find(fuelIn)->Rect.center().x() - frame.left()) < 0.5);
    CHECK(std::abs(behavior.nodes()->find(torque)->Rect.center().x() - frame.right()) < 0.5);
    CHECK_EQ(behavior.pinSide(key(fuelIn)), QString("left"));
    int flows = 0;
    int successionFlows = 0;
    for (const auto &edge: behavior.edges()->edges()) {
        if (edge.Kind != DiagramTypes::Flow)
            continue;
        CHECK(edge.FilledHead);
        CHECK(!EdgeRouting::routeOf(*behavior.nodes(), behavior.edges()->edges(), edge).empty());
        if (edge.Dashed) {
            ++successionFlows;
            CHECK_EQ(edge.Target, torque);
        } else {
            ++flows;
        }
    }
    CHECK_EQ(flows, 2);
    CHECK_EQ(successionFlows, 1);
    // a pin stays on the border when dragged; the node at a pin is the pin
    const QRectF before = behavior.nodes()->find(cmd)->Rect;
    behavior.moveNode(key(cmd), 400, 400);
    CHECK(behavior.nodes()->find(cmd)->Rect == before);
    CHECK_EQ(behavior.nodeAt(before.center().x(), before.center().y()), key(cmd));
    // a flow drawn from a pin to a pin
    CHECK(behavior.connectNodes(key(level), key(cmd)).isEmpty());
    const std::string generated = SysMLCodeGenerator().generate(model);
    CHECK(generated.find("flow from burn.level to pump.cmd;") != std::string::npos);
    CHECK(!behavior.connectNodes(key(level), key(pump)).isEmpty()); // a pin only to a pin
    SemanticModel again;
    CHECK(SysMLTextImporter::importText(generated, again).lossless());
    CHECK_EQ(SysMLCodeGenerator().generate(again), generated);
}

TEST_CASE("Behavior: swimlanes for the parts performing the actions (perform-actions-swimlanes)") {
    const std::string text =
            "part part0 {\n"
            "    perform action0;\n"
            "    part part1 {\n"
            "        perform action0.action1;\n"
            "        perform action0.action4;\n"
            "    }\n"
            "    part part2 {\n"
            "        perform action0.action2;\n"
            "    }\n"
            "}\n"
            "action action0 {\n"
            "    action action1;\n"
            "    action action2;\n"
            "    action action4;\n"
            "    action other;\n"
            "    first action1 then action2;\n"
            "    first action2 then action4;\n"
            "}\n";
    SemanticModel model;
    CHECK(SysMLTextImporter::importText(text, model).lossless());
    BehaviorController behavior(model);
    const ElementId action0 = model.children(RootId).at(1);
    behavior.setContextId(key(action0));
    behavior.syncNow();
    const QVariantList lanes = behavior.lanes();
    CHECK_EQ(lanes.size(), 2);
    if (lanes.size() != 2)
        return;
    CHECK_EQ(lanes.at(0).toMap().value("title").toString(), QString("part0.part1"));
    CHECK_EQ(lanes.at(1).toMap().value("title").toString(), QString("part0.part2"));
    const auto laneRect = [&](int index) {
        const auto map = lanes.at(index).toMap();
        return QRectF(map.value("x").toReal(), map.value("y").toReal(), map.value("width").toReal(), map.value("height").toReal());
    };
    const ElementId action1 = model.children(action0).at(0);
    const ElementId action2 = model.children(action0).at(1);
    const ElementId action4 = model.children(action0).at(2);
    const ElementId other = model.children(action0).at(3);
    CHECK(laneRect(0).contains(behavior.nodeRect(key(action1))));
    CHECK(laneRect(0).contains(behavior.nodeRect(key(action4))));
    CHECK(laneRect(1).contains(behavior.nodeRect(key(action2))));
    CHECK(!laneRect(0).intersects(behavior.nodeRect(key(other))));
    CHECK(!laneRect(1).intersects(behavior.nodeRect(key(other))));
    CHECK(std::abs(laneRect(0).top() - laneRect(1).top()) < 0.5); // aligned
    CHECK(behavior.frameRect().contains(laneRect(0).united(laneRect(1))));
}

