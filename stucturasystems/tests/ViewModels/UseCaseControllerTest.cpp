//
// UseCaseController: use cases, actors, the subject and «include» - read and written (SysML v2, table 23).
//

#include "../TestFramework.h"
#include "QtPrinting.h"

#include <DiagramController.h>
#include <UseCaseController.h>

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

    int count(const UseCaseController &controller, DiagramTypes::NodeKind kind) {
        int result = 0;
        for (const auto &node: controller.nodes()->nodes())
            result += node.Kind == kind ? 1 : 0;
        return result;
    }

    int edges(const UseCaseController &controller, DiagramTypes::EdgeKind kind) {
        int result = 0;
        for (const auto &edge: controller.edges()->edges())
            result += edge.Kind == kind ? 1 : 0;
        return result;
    }
}

TEST_CASE("Use cases: read with actors, subject and include") {
    SemanticModel model;
    CHECK(SysMLTextImporter::importText(
            "package Mission {\n"
            "    use case def Drive {\n"
            "        subject vehicle : Vehicle;\n"
            "        actor driver : Person;\n"
            "        actor passengers : Person[0..4];\n"
            "        include use case refuel : Refuel;\n"
            "        include use case park;\n"
            "    }\n"
            "    use case def Refuel {\n"
            "        subject vehicle : Vehicle;\n"
            "        actor driver : Person;\n"
            "    }\n"
            "    use case maintain : Maintain {\n"
            "        actor mechanic : Person;\n"
            "        include drive;\n"
            "    }\n"
            "    use case drive : Drive;\n"
            "}\n", model).lossless());
    UseCaseController view(model);
    const ElementId mission = *find(model, RootId, "Mission");
    CHECK_EQ(view.contextId(), key(mission)); // the first place with use cases
    // Drive, Refuel, maintain, drive, and the included "park" (no definition shown for it)
    CHECK_EQ(count(view, DiagramTypes::UseCaseNode), 5);
    CHECK_EQ(count(view, DiagramTypes::ActorNode), 3); // driver (twice declared), passengers, mechanic
    CHECK_EQ(count(view, DiagramTypes::BoundaryNode), 1);
    CHECK_EQ(edges(view, DiagramTypes::Association), 6); // 4 of the actors, 2 of the subject (Drive, Refuel)
    CHECK_EQ(edges(view, DiagramTypes::Include), 3); // refuel -> Refuel, park, maintain -> drive
    // the subject: a node «subject» above its use cases with a line to each of them
    const ElementId drive = *find(model, mission, "Drive");
    const ElementId refuel = *find(model, mission, "Refuel");
    const ElementId vehicle = model.children(drive).at(0);
    const QRectF subject = view.nodeRect(key(vehicle));
    CHECK(!subject.intersects(view.nodeRect(key(drive))));
    CHECK(subject.bottom() < view.nodeRect(key(drive)).top());
    int subjectLines = 0;
    for (const auto &edge: view.edges()->edges()) {
        if (edge.Kind == DiagramTypes::Association && edge.Source == vehicle) {
            ++subjectLines;
            CHECK((edge.Target == drive || edge.Target == refuel));
        }
    }
    CHECK_EQ(subjectLines, 2);
    // actors left of the use cases
    const ElementId driver = model.children(drive).at(1);
    CHECK(view.nodeRect(key(driver)).right() < view.nodeRect(key(drive)).left());
}

TEST_CASE("Use cases: drawing writes the SysML text") {
    SemanticModel model;
    UseCaseController view(model);
    DiagramController structure(model);
    CHECK_EQ(view.contextId(), QString("0"));
    const QString drive = view.addNode(DiagramTypes::ToolUseCaseDefinition, 400, 100);
    const QRectF driveRect = view.nodeRect(drive);
    // actor and subject are dropped onto a use case
    CHECK(view.addNode(DiagramTypes::ToolActor, 50, 50).isEmpty());
    const QString actor = view.addNode(DiagramTypes::ToolActor, driveRect.center().x(), driveRect.center().y());
    CHECK(!actor.isEmpty());
    const QString subject = view.addNode(DiagramTypes::ToolSubject, driveRect.center().x(), driveRect.center().y());
    CHECK(!subject.isEmpty());
    CHECK(view.updateGroup(subject, {{"name", "vehicle"}, {"typeName", "Vehicle"}, {"multiplicity", ""}}).isEmpty());
    CHECK(view.updateGroup(actor, {{"name", "driver"}, {"typeName", "Person"}, {"multiplicity", ""}}).isEmpty());
    // a second use case dropped onto the subject belongs to the same system
    const QRectF subjectRect = view.nodeRect(subject);
    CHECK(!subjectRect.intersects(driveRect));
    const QString refuel = view.addNode(DiagramTypes::ToolUseCaseDefinition, subjectRect.center().x(), subjectRect.center().y());
    CHECK(model.children(refuel.toULongLong()).size() == 1);
    CHECK(model.element(model.children(refuel.toULongLong()).front()).kind() == ElementKind::Subject);
    // the actor takes part in it, drive includes it
    CHECK(view.connectNodes(actor, refuel).isEmpty());
    CHECK(!view.connectNodes(actor, refuel).isEmpty()); // already
    CHECK(view.connectNodes(drive, refuel).isEmpty());
    CHECK(!view.connectNodes(drive, refuel).isEmpty()); // already
    // a use case usage outside the boundary, included by reference
    const QString park = view.addNode(DiagramTypes::ToolUseCase, 1200, 600);
    CHECK(structure.updateElement(park, {{"name", "park"}, {"typeName", "Park"}}).isEmpty());
    CHECK(view.connectNodes(refuel, park).isEmpty());
    CHECK(view.connectNodes(park, subject).isEmpty()); // into the system

    std::string text = generated(model);
    CHECK_EQ(text, std::string("use case def UseCase1 {\n"
                               "    actor driver : Person;\n"
                               "    subject vehicle : Vehicle;\n"
                               "    include use case useCase2 : UseCase2;\n"
                               "}\n"
                               "use case def UseCase2 {\n"
                               "    subject vehicle : Vehicle;\n"
                               "    actor driver : Person;\n"
                               "    include park;\n"
                               "}\n"
                               "use case park : Park {\n"
                               "    subject vehicle : Vehicle;\n"
                               "}\n"));
    SemanticModel again;
    CHECK(SysMLTextImporter::importText(text, again).lossless());
    CHECK_EQ(generated(again), text);
    view.syncNow();
    CHECK_EQ(count(view, DiagramTypes::ActorNode), 1);
    CHECK_EQ(count(view, DiagramTypes::BoundaryNode), 1);
    CHECK_EQ(edges(view, DiagramTypes::Include), 2);

    // renaming the actor renames both declarations; the subject moves on its own
    CHECK(view.updateGroup(actor, {{"name", "pilot"}, {"typeName", ""}, {"multiplicity", "1"}}).isEmpty());
    text = generated(model);
    CHECK(text.find("actor pilot[1];") != std::string::npos);
    CHECK(text.find("actor driver") == std::string::npos);
    const QRectF before = view.nodeRect(drive);
    const QRectF frame = view.nodeRect(subject);
    view.moveNode(subject, frame.x() + 100, frame.y() + 50);
    CHECK(view.nodeRect(drive).topLeft() == before.topLeft());
    CHECK(view.nodeRect(subject).topLeft() == frame.topLeft() + QPointF(100, 50));

    // deleting: a line of the actor, the include, the whole actor
    QString line;
    QString include;
    for (const auto &edge: view.edges()->edges()) {
        if (edge.Kind == DiagramTypes::Association && key(edge.Target) == refuel)
            line = edge.Key;
        if (edge.Kind == DiagramTypes::Include && key(edge.Source) == drive)
            include = edge.Key;
    }
    CHECK_EQ(view.elementFor(include), QString::number(model.children(drive.toULongLong()).back()));
    view.deleteEdge(line);
    view.deleteEdge(include);
    text = generated(model);
    CHECK(text.find("include use case") == std::string::npos);
    view.select(actor, false);
    view.deleteSelection();
    CHECK(generated(model).find("actor") == std::string::npos);
    // the subject group as well
    view.select(subject, false);
    view.deleteSelection();
    CHECK(generated(model).find("subject") == std::string::npos);
}
