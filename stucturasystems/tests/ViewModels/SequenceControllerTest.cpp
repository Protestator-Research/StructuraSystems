//
// SequenceController: lifelines, events and messages of an interaction - read and written.
//

#include "../TestFramework.h"
#include "QtPrinting.h"

#include <DiagramController.h>
#include <SequenceController.h>

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

    int count(const SequenceController &controller, DiagramTypes::NodeKind kind) {
        int result = 0;
        for (const auto &node: controller.nodes()->nodes())
            result += node.Kind == kind ? 1 : 0;
        return result;
    }

    const char *CruiseControl =
            "occurrence def CruiseControlInteraction {\n"
            "    ref part driver : Driver {\n"
            "        event occurrence setSpeedSent;\n"
            "    }\n"
            "    ref part vehicle : Vehicle {\n"
            "        part cruiseController : CruiseController {\n"
            "            event occurrence setSpeedReceived;\n"
            "            then event occurrence sensedSpeedReceived;\n"
            "            then event occurrence fuelCommandSent;\n"
            "        }\n"
            "        part speedometer : Speedometer {\n"
            "            event occurrence sensedSpeedSent;\n"
            "        }\n"
            "        part engine : Engine {\n"
            "            event occurrence fuelCommandReceived;\n"
            "        }\n"
            "    }\n"
            "    message setSpeedMessage of SetSpeed from driver.setSpeedSent to vehicle.cruiseController.setSpeedReceived;\n"
            "    then message sensedSpeedMessage of SensedSpeed from vehicle.speedometer.sensedSpeedSent to "
            "vehicle.cruiseController.sensedSpeedReceived;\n"
            "    message fuelCommandMessage of FuelCommand from vehicle.cruiseController.fuelCommandSent to "
            "vehicle.engine.fuelCommandReceived;\n"
            "}\n";
}

TEST_CASE("Sequence: lifelines, nested lifelines, events and messages of an interaction") {
    SemanticModel model;
    CHECK(SysMLTextImporter::importText(CruiseControl, model).lossless());
    SequenceController sequence(model);
    const ElementId interaction = model.children(RootId).front();
    CHECK_EQ(sequence.contextId(), key(interaction));
    // driver, cruiseController, speedometer, engine; vehicle is the header above the last three
    CHECK_EQ(count(sequence, DiagramTypes::LifelineNode), 4);
    CHECK_EQ(count(sequence, DiagramTypes::LifelineGroupNode), 1);
    CHECK_EQ(count(sequence, DiagramTypes::EventNode), 6);
    int messages = 0;
    int thens = 0;
    for (const auto &edge: sequence.edges()->edges()) {
        messages += edge.Kind == DiagramTypes::Message ? 1 : 0;
        thens += edge.Kind == DiagramTypes::Succession ? 1 : 0;
    }
    CHECK_EQ(messages, 3);
    CHECK_EQ(thens, 2);

    const ElementId driver = *find(model, interaction, "driver");
    const ElementId vehicle = *find(model, interaction, "vehicle");
    const ElementId controller = *find(model, vehicle, "cruiseController");
    const ElementId speedometer = *find(model, vehicle, "speedometer");
    const ElementId engine = *find(model, vehicle, "engine");
    const auto y = [&](ElementId owner, const std::string &event) {
        return sequence.nodeRect(key(*find(model, owner, event))).center().y();
    };
    // a message is horizontal: its two events in one row
    CHECK_EQ(y(driver, "setSpeedSent"), y(controller, "setSpeedReceived"));
    CHECK_EQ(y(speedometer, "sensedSpeedSent"), y(controller, "sensedSpeedReceived"));
    CHECK_EQ(y(controller, "fuelCommandSent"), y(engine, "fuelCommandReceived"));
    // top to bottom in the order of the events on a lifeline
    CHECK(y(controller, "setSpeedReceived") < y(controller, "sensedSpeedReceived"));
    CHECK(y(controller, "sensedSpeedReceived") < y(controller, "fuelCommandSent"));
    // lifelines left to right in the order of the text, nested ones below the header of their part
    CHECK(sequence.nodeRect(key(driver)).center().x() < sequence.nodeRect(key(controller)).center().x());
    CHECK(sequence.nodeRect(key(controller)).center().x() < sequence.nodeRect(key(engine)).center().x());
    CHECK(sequence.nodeRect(key(driver)).top() < sequence.nodeRect(key(controller)).top());
    // the header is not an element of its own: it stands for the part
    const QString header = sequence.nodeAt(sequence.nodeRect(key(controller)).center().x(),
                                           sequence.nodeRect(key(driver)).top() + 10);
    CHECK_EQ(sequence.elementOf(header), key(vehicle));
}

TEST_CASE("Sequence: drawing lifelines, events and messages writes the SysML text") {
    SemanticModel model;
    DiagramController structure(model);
    const QString interaction = structure.addElement(DiagramTypes::ToolOccurrenceDefinition, 100, 100);
    SequenceController sequence(model);
    sequence.setContextId(interaction);
    CHECK_EQ(sequence.contextId(), interaction);

    const QString driver = sequence.addNode(DiagramTypes::ToolLifeline, 100, 60);
    const QString vehicle = sequence.addNode(DiagramTypes::ToolLifeline, 400, 60);
    CHECK(!driver.isEmpty());
    CHECK(!vehicle.isEmpty());
    const qreal driverX = sequence.nodeRect(driver).center().x();
    const qreal vehicleX = sequence.nodeRect(vehicle).center().x();
    CHECK(driverX < vehicleX);
    // a third lifeline dropped between them goes between them in the text
    const QString middle = sequence.addNode(DiagramTypes::ToolLifeline, (driverX + vehicleX) / 2, 60);
    CHECK(sequence.nodeRect(middle).center().x() > sequence.nodeRect(driver).center().x());
    CHECK(sequence.nodeRect(middle).center().x() < sequence.nodeRect(vehicle).center().x());

    // a message from lifeline to lifeline: two events and the message
    const qreal row = sequence.nodeRect(driver).top() + 160;
    CHECK(sequence.connectPoints(sequence.nodeRect(driver).center().x(), row,
                                 sequence.nodeRect(vehicle).center().x(), row).isEmpty());
    // an answer below it
    CHECK(sequence.connectPoints(sequence.nodeRect(vehicle).center().x(), row + 120,
                                 sequence.nodeRect(driver).center().x(), row + 120).isEmpty());
    std::string text = generated(model);
    CHECK_EQ(text, std::string("occurrence def Interaction1 {\n"
                               "    ref part part1 {\n"
                               "        event occurrence message1Sent;\n"
                               "        event occurrence message2Received;\n"
                               "    }\n"
                               "    ref part part3;\n"
                               "    ref part part2 {\n"
                               "        event occurrence message1Received;\n"
                               "        event occurrence message2Sent;\n"
                               "    }\n"
                               "    message message1 from part1.message1Sent to part2.message1Received;\n"
                               "    message message2 from part2.message2Sent to part1.message2Received;\n"
                               "}\n"));
    SemanticModel again;
    CHECK(SysMLTextImporter::importText(text, again).lossless());
    CHECK_EQ(generated(again), text);

    // a message onto an existing event; "then" between two events of different lifelines
    const ElementId part1 = driver.toULongLong();
    const ElementId part2 = vehicle.toULongLong();
    const QRectF sent = sequence.nodeRect(key(*find(model, part1, "message1Sent")));
    CHECK(!sequence.connectPoints(sent.center().x(), sent.center().y(), sent.center().x(), sent.center().y() + 50).isEmpty());
    sequence.setArrowKind(1);
    const QRectF answer = sequence.nodeRect(key(*find(model, part2, "message2Sent")));
    CHECK(sequence.connectPoints(sent.center().x(), sent.center().y(), answer.center().x(), answer.center().y()).isEmpty());
    CHECK(generated(model).find("    first part1.message1Sent then part2.message2Sent;\n") != std::string::npos);

    // the edge dialog: payload and "then message"
    QString message;
    for (const auto &edge: sequence.edges()->edges())
        if (edge.Kind == DiagramTypes::Message && message.isEmpty())
            message = edge.Key;
    CHECK(structure.edgeDetails(message).value("hasSequence").toBool());
    CHECK(structure.updateEdge(message, {{"name", "setSpeed"}, {"typeName", "SetSpeed"}, {"sequenced", false}}).isEmpty());
    CHECK(generated(model).find("    message setSpeed of SetSpeed from part1.message1Sent to part2.message1Received;\n") !=
          std::string::npos);

    // moving an event down below the other one on its lifeline changes their order in the text
    const ElementId received = *find(model, part1, "message2Received");
    sequence.moveEvent(key(*find(model, part1, "message1Sent")), sequence.nodeRect(key(received)).bottom() + 30);
    CHECK_EQ(model.element(model.children(part1).front()).name(), std::string("message2Received"));

    // moving a lifeline to the left end
    sequence.moveLifeline(vehicle, 0);
    CHECK_EQ(model.children(interaction.toULongLong()).front(), part2);

    // deleting a lifeline deletes its events and the messages
    sequence.select(vehicle, false);
    sequence.deleteSelection();
    for (const auto &relationship: model.relationships())
        CHECK(relationship.Kind != RelationshipKind::Message);
}

TEST_CASE("Sequence: messages between whole lifelines and events dropped onto a lifeline") {
    SemanticModel model;
    CHECK(SysMLTextImporter::importText("occurrence def Startup {\n"
                                        "    ref part driver : Driver;\n"
                                        "    ref part vehicle : Vehicle {\n"
                                        "        event occurrence doorClosed;\n"
                                        "    }\n"
                                        "    message vs of VehicleStart from driver to vehicle;\n"
                                        "}\n", model).lossless());
    SequenceController sequence(model);
    CHECK_EQ(sequence.edges()->count(), 1);
    // the message starts and ends at an end of its own (no event)
    const auto &edge = sequence.edges()->edges().front();
    CHECK(sequence.elementOf(QString::number(edge.Source)).isEmpty());
    // an event dropped onto the lifeline of the driver
    const ElementId interaction = model.children(RootId).front();
    const ElementId driver = *find(model, interaction, "driver");
    const QRectF lifeline = sequence.nodeRect(key(driver));
    const QString event = sequence.addNode(DiagramTypes::ToolEvent, lifeline.center().x(), lifeline.top() + 300);
    CHECK_EQ(model.element(event.toULongLong()).owner(), driver);
    CHECK(sequence.addNode(DiagramTypes::ToolEvent, lifeline.right() + 400, lifeline.top() + 300).isEmpty());
}

TEST_CASE("Structure view: occurrences, events, allocations and dependencies") {
    SemanticModel model;
    CHECK(SysMLTextImporter::importText(CruiseControl, model).lossless());
    DiagramController structure(model);
    const ElementId interaction = model.children(RootId).front();
    const auto *node = structure.nodes()->find(interaction);
    CHECK(node != nullptr);
    CHECK_EQ(node->Keyword, QString("«occurrence def»"));
    CHECK_EQ(DiagramNodeModel::entryCount(*node, "messages"), 3);
    const ElementId vehicle = *find(model, interaction, "vehicle");
    const ElementId controller = *find(model, vehicle, "cruiseController");
    const auto *controllerNode = structure.nodes()->find(controller);
    CHECK(controllerNode != nullptr);
    CHECK_EQ(DiagramNodeModel::entryCount(*controllerNode, "events"), 3);

    // allocation between two parts (drawn as arrow), between actions of parts (drawn between the parts)
    SemanticModel allocations;
    CHECK(SysMLTextImporter::importText("part torqueGenerator {\n"
                                        "    perform action generateTorque;\n"
                                        "}\n"
                                        "part powerTrain {\n"
                                        "    part engine {\n"
                                        "        perform action generateTorque;\n"
                                        "    }\n"
                                        "}\n"
                                        "allocate torqueGenerator to powerTrain {\n"
                                        "    allocate torqueGenerator.generateTorque to powerTrain.engine.generateTorque;\n"
                                        "}\n", allocations).lossless());
    DiagramController view(allocations);
    int allocated = 0;
    for (const auto &edge: view.edges()->edges()) {
        if (edge.Kind == DiagramTypes::Allocation) {
            ++allocated;
            CHECK(edge.Label.startsWith("«allocate»"));
        }
    }
    CHECK_EQ(allocated, 2);

    // drawing: an allocation and a dependency with the arrow preset
    const ElementId generator = allocations.children(RootId).front();
    const ElementId powerTrain = allocations.children(RootId)[1];
    const QVariantList kinds = view.allowedEdgeKinds(key(generator), key(powerTrain));
    CHECK(kinds.contains(int(DiagramTypes::Allocation)));
    CHECK(kinds.contains(int(DiagramTypes::Dependency)));
    view.setArrowDefault(DiagramTypes::AlwaysDependency);
    const QRectF target = view.nodeRect(key(powerTrain));
    view.arrowReleased(key(generator), target.center().x(), target.center().y());
    CHECK(generated(allocations).find("dependency from torqueGenerator to powerTrain;\n") != std::string::npos);
    CHECK(!view.allowedEdgeKinds(key(generator), key(powerTrain)).contains(int(DiagramTypes::Dependency))); // once

    // an allocation definition starts with its two ends
    const QString definition = view.addElement(DiagramTypes::ToolAllocationDefinition, 600, 600);
    CHECK(generated(allocations).find("allocation def Allocation1 {\n    end logical;\n    end physical;\n}\n") !=
          std::string::npos);
    CHECK_EQ(DiagramNodeModel::entryCount(*view.nodes()->find(definition.toULongLong()), "ends"), 2);

    // an event dropped onto a part
    CHECK(!view.addElement(DiagramTypes::ToolEvent, target.center().x(), target.center().y()).isEmpty());
    CHECK(generated(allocations).find("event occurrence event1;") != std::string::npos);
}
