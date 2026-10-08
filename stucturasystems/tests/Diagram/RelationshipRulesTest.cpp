//
// RelationshipRules: which edges/nestings the editor may offer.
//

#include "../TestFramework.h"

#include <Diagram/Semantic/RelationshipRules.h>
#include <Diagram/Semantic/SemanticModel.h>

#include <algorithm>

using namespace StructuraSystems::Diagram;

namespace {
    bool offers(const std::vector<RelationshipKind> &kinds, RelationshipKind kind) {
        return std::find(kinds.begin(), kinds.end(), kind) != kinds.end();
    }
}

TEST_CASE("Rules: two parts can be connected, flowed and specialized") {
    SemanticModel model;
    const auto a = model.addElement(ElementKind::PartUsage, "a");
    const auto b = model.addElement(ElementKind::PartUsage, "b");
    const auto kinds = RelationshipRules::allowedRelationshipKinds(model, a, b);
    CHECK(offers(kinds, RelationshipKind::Connection));
    CHECK(offers(kinds, RelationshipKind::Flow));
    CHECK(offers(kinds, RelationshipKind::Specialization));
}

TEST_CASE("Rules: part definitions only allow specialization") {
    SemanticModel model;
    const auto car = model.addElement(ElementKind::PartDefinition, "Car");
    const auto sportsCar = model.addElement(ElementKind::PartDefinition, "SportsCar");
    const auto kinds = RelationshipRules::allowedRelationshipKinds(model, sportsCar, car);
    CHECK(kinds.size() == 1);
    CHECK(offers(kinds, RelationshipKind::Specialization));
}

TEST_CASE("Rules: mixed kinds and self relations are rejected") {
    SemanticModel model;
    const auto def = model.addElement(ElementKind::PartDefinition, "Car");
    const auto usage = model.addElement(ElementKind::PartUsage, "car");
    CHECK(RelationshipRules::allowedRelationshipKinds(model, usage, def).empty());
    CHECK(RelationshipRules::allowedRelationshipKinds(model, usage, usage).empty());
    CHECK(!RelationshipRules::canRelate(model, RelationshipKind::Connection, usage, 999));
}

TEST_CASE("Rules: no relationship between a part and its own sub-part") {
    SemanticModel model;
    const auto vehicle = model.addElement(ElementKind::PartUsage, "vehicle");
    const auto engine = model.addElement(ElementKind::PartUsage, "engine", vehicle);
    CHECK(!RelationshipRules::canRelate(model, RelationshipKind::Connection, vehicle, engine));
    CHECK(!RelationshipRules::canRelate(model, RelationshipKind::Flow, engine, vehicle));
    CHECK(!RelationshipRules::canRelate(model, RelationshipKind::Specialization, engine, vehicle));
    CHECK(RelationshipRules::allowedRelationshipKinds(model, engine, vehicle).empty());
}

TEST_CASE("Rules: duplicates and specialization cycles are rejected") {
    SemanticModel model;
    const auto a = model.addElement(ElementKind::PartDefinition, "A");
    const auto b = model.addElement(ElementKind::PartDefinition, "B");
    const auto c = model.addElement(ElementKind::PartDefinition, "C");
    model.addRelationship(RelationshipKind::Specialization, b, a);
    model.addRelationship(RelationshipKind::Specialization, c, b);
    CHECK(!RelationshipRules::canRelate(model, RelationshipKind::Specialization, b, a)); // duplicate
    CHECK(!RelationshipRules::canRelate(model, RelationshipKind::Specialization, a, c)); // A :> C :> B :> A
    CHECK_THROWS_AS(model.addRelationship(RelationshipKind::Specialization, a, c), ModelError);

    const auto x = model.addElement(ElementKind::PartUsage, "x");
    const auto y = model.addElement(ElementKind::PartUsage, "y");
    model.addRelationship(RelationshipKind::Connection, x, y);
    CHECK(!RelationshipRules::canRelate(model, RelationshipKind::Connection, y, x)); // connect is undirected
    CHECK(RelationshipRules::canRelate(model, RelationshipKind::Flow, y, x));
}

TEST_CASE("Rules: ownership") {
    SemanticModel model;
    const auto package = model.addElement(ElementKind::Package, "P");
    const auto def = model.addElement(ElementKind::PartDefinition, "Car", package);
    CHECK(RelationshipRules::canOwn(model, RootId, ElementKind::PartUsage));
    CHECK(!RelationshipRules::canOwn(model, RootId, ElementKind::Attribute));
    CHECK(RelationshipRules::canOwn(model, def, ElementKind::Attribute));
    CHECK(RelationshipRules::canOwn(model, def, ElementKind::ActionPerformance));
    CHECK(!RelationshipRules::canOwn(model, def, ElementKind::Package));
    CHECK(!RelationshipRules::canOwn(model, package, ElementKind::Attribute));
    CHECK(!RelationshipRules::canReparent(model, package, def)); // package below its own child
    const auto reason = RelationshipRules::canOwn(model, package, ElementKind::Attribute).Reason;
    CHECK(!reason.empty());
}
