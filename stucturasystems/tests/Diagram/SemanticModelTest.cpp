//
// SemanticModel: tree operations, validation and change notifications.
//

#include "../TestFramework.h"

#include <Diagram/Semantic/SemanticModel.h>

using namespace StructuraSystems::Diagram;

TEST_CASE("Model: qualified names and ownership") {
    SemanticModel model;
    const auto vehicle = model.addElement(ElementKind::PartUsage, "vehicle");
    const auto engine = model.addElement(ElementKind::PartUsage, "engine", vehicle);
    const auto cylinders = model.addElement(ElementKind::PartUsage, "cylinders", engine);

    CHECK_EQ(model.qualifiedName(cylinders), std::string("vehicle::engine::cylinders"));
    CHECK(model.isAncestor(vehicle, cylinders));
    CHECK(!model.isAncestor(cylinders, vehicle));
    CHECK_EQ(model.children(RootId).size(), std::size_t{1});
    CHECK_EQ(model.element(engine).owner(), vehicle);
}

TEST_CASE("Model: invalid elements are rejected") {
    SemanticModel model;
    CHECK_THROWS_AS(model.addElement(ElementKind::PartUsage, ""), ModelError);
    CHECK_THROWS_AS(model.addElement(ElementKind::Attribute, "mass"), ModelError); // needs a part as owner
    const auto vehicle = model.addElement(ElementKind::PartUsage, "vehicle");
    const auto mass = model.addElement(ElementKind::Attribute, "mass", vehicle);
    CHECK_THROWS_AS(model.addElement(ElementKind::Attribute, "mass", vehicle), ModelError); // duplicate name
    CHECK_THROWS_AS(model.addElement(ElementKind::PartUsage, "x", mass), ModelError);      // attribute owns nothing
    CHECK_THROWS_AS(model.addElement(ElementKind::PartUsage, "x", 4711), ModelError);      // unknown owner
    CHECK_THROWS_AS(model.setMultiplicity(model.addElement(ElementKind::Package, "P"), Multiplicity::exactly(2)),
                    ModelError);
}

TEST_CASE("Model: removing an element removes its subtree and relationships") {
    SemanticModel model;
    const auto vehicle = model.addElement(ElementKind::PartUsage, "vehicle");
    const auto engine = model.addElement(ElementKind::PartUsage, "engine", vehicle);
    const auto cylinders = model.addElement(ElementKind::PartUsage, "cylinders", engine);
    const auto transmission = model.addElement(ElementKind::PartUsage, "transmission", vehicle);
    model.addRelationship(RelationshipKind::Connection, cylinders, transmission);

    model.removeElement(engine);
    CHECK(!model.contains(engine));
    CHECK(!model.contains(cylinders));
    CHECK(model.contains(transmission));
    CHECK(model.relationships().empty());
    CHECK_EQ(model.children(vehicle).size(), std::size_t{1});
}

TEST_CASE("Model: reparent (composition) and cycle protection") {
    SemanticModel model;
    const auto vehicle = model.addElement(ElementKind::PartUsage, "vehicle");
    const auto engine = model.addElement(ElementKind::PartUsage, "engine");

    model.reparent(engine, vehicle);
    CHECK_EQ(model.element(engine).owner(), vehicle);
    CHECK(model.children(RootId).size() == 1);
    CHECK_THROWS_AS(model.reparent(vehicle, engine), ModelError); // would create a cycle
    CHECK_THROWS_AS(model.reparent(vehicle, vehicle), ModelError);

    const auto otherEngine = model.addElement(ElementKind::PartUsage, "engine");
    CHECK_THROWS_AS(model.reparent(otherEngine, vehicle), ModelError); // name already used in vehicle
}

TEST_CASE("Model: reparent drops connections that became part-to-own-subpart") {
    SemanticModel model;
    const auto a = model.addElement(ElementKind::PartUsage, "a");
    const auto b = model.addElement(ElementKind::PartUsage, "b");
    model.addRelationship(RelationshipKind::Connection, a, b);
    model.reparent(b, a);
    CHECK(model.relationships().empty());
}

TEST_CASE("Model: rename keeps sibling names unique") {
    SemanticModel model;
    const auto a = model.addElement(ElementKind::PartUsage, "a");
    model.addElement(ElementKind::PartUsage, "b");
    CHECK_THROWS_AS(model.rename(a, "b"), ModelError);
    model.rename(a, "c");
    CHECK_EQ(model.element(a).name(), std::string("c"));
}

TEST_CASE("Model: listeners are notified") {
    SemanticModel model;
    std::vector<ModelChange::Type> changes;
    const auto token = model.addListener([&](const ModelChange &change) { changes.push_back(change.ChangeType); });

    const auto a = model.addElement(ElementKind::PartUsage, "a");
    const auto b = model.addElement(ElementKind::PartUsage, "b");
    const auto connection = model.addRelationship(RelationshipKind::Connection, a, b);
    model.rename(a, "c");
    model.removeRelationship(connection);
    model.removeListener(token);
    model.removeElement(b);

    const std::vector<ModelChange::Type> expected = {
            ModelChange::Type::ElementAdded, ModelChange::Type::ElementAdded,
            ModelChange::Type::RelationshipAdded, ModelChange::Type::ElementChanged,
            ModelChange::Type::RelationshipRemoved};
    CHECK(changes == expected);
}
