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

TEST_CASE("Rules: part definitions only allow specialization (and a dependency)") {
    SemanticModel model;
    const auto car = model.addElement(ElementKind::PartDefinition, "Car");
    const auto sportsCar = model.addElement(ElementKind::PartDefinition, "SportsCar");
    const auto kinds = RelationshipRules::allowedRelationshipKinds(model, sportsCar, car);
    CHECK(kinds.size() == 2);
    CHECK(offers(kinds, RelationshipKind::Specialization));
    CHECK(offers(kinds, RelationshipKind::Dependency)); // possible between any two elements
}

TEST_CASE("Rules: mixed kinds and self relations are rejected") {
    SemanticModel model;
    const auto def = model.addElement(ElementKind::PartDefinition, "Car");
    const auto usage = model.addElement(ElementKind::PartUsage, "car");
    CHECK(RelationshipRules::allowedRelationshipKinds(model, usage, def) ==
          std::vector<RelationshipKind>{RelationshipKind::Dependency});
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

TEST_CASE("Rules: messages, allocations and dependencies") {
    SemanticModel model;
    const auto interaction = model.addElement(ElementKind::OccurrenceDefinition, "Interaction");
    const auto driver = model.addElement(ElementKind::PartUsage, "driver", interaction);
    const auto vehicle = model.addElement(ElementKind::PartUsage, "vehicle", interaction);
    const auto sent = model.addElement(ElementKind::EventOccurrence, "sent", driver);
    const auto received = model.addElement(ElementKind::EventOccurrence, "received", vehicle);
    const auto later = model.addElement(ElementKind::EventOccurrence, "later", vehicle);
    CHECK(RelationshipRules::canRelate(model, RelationshipKind::Message, sent, received));
    CHECK(RelationshipRules::canRelate(model, RelationshipKind::Message, driver, vehicle)); // between lifelines
    CHECK(!RelationshipRules::canRelate(model, RelationshipKind::Message, vehicle, received)); // own event
    CHECK(!RelationshipRules::canRelate(model, RelationshipKind::Message, interaction, sent));
    // events: successions on one lifeline and across lifelines, not with other kinds there
    CHECK(RelationshipRules::canRelate(model, RelationshipKind::Succession, received, later));
    CHECK(RelationshipRules::canRelate(model, RelationshipKind::Succession, sent, later));
    // allocation between usages, not definitions; dependency between anything
    const auto logical = model.addElement(ElementKind::PartUsage, "logical");
    const auto generate = model.addElement(ElementKind::ActionPerformance, "generate", logical);
    const auto engine = model.addElement(ElementKind::PartUsage, "engine");
    CHECK(RelationshipRules::canRelate(model, RelationshipKind::Allocation, logical, engine));
    CHECK(RelationshipRules::canRelate(model, RelationshipKind::Allocation, generate, engine));
    CHECK(!RelationshipRules::canRelate(model, RelationshipKind::Allocation, logical, generate));
    CHECK(!RelationshipRules::canRelate(model, RelationshipKind::Allocation, interaction, engine));
    CHECK(RelationshipRules::canRelate(model, RelationshipKind::Dependency, interaction, engine));
    model.addRelationship(RelationshipKind::Dependency, interaction, engine);
    CHECK(!RelationshipRules::canRelate(model, RelationshipKind::Dependency, interaction, engine)); // duplicate
    // events live in parts, items, ports, actions and occurrences
    CHECK(RelationshipRules::canOwn(model, engine, ElementKind::EventOccurrence));
    CHECK(!RelationshipRules::canOwn(model, RootId, ElementKind::EventOccurrence));
    CHECK(!RelationshipRules::canOwn(model, sent, ElementKind::EventOccurrence));
}

TEST_CASE("Model: refining allocations go with the one they refine; the declaring body is reset") {
    SemanticModel model;
    const auto logical = model.addElement(ElementKind::Package, "Logical");
    const auto generator = model.addElement(ElementKind::PartUsage, "torqueGenerator", logical);
    const auto generate = model.addElement(ElementKind::ActionPerformance, "generateTorque", generator);
    const auto powerTrain = model.addElement(ElementKind::PartUsage, "powerTrain");
    const auto engine = model.addElement(ElementKind::PartUsage, "engine", powerTrain);
    const auto engineGenerate = model.addElement(ElementKind::ActionPerformance, "generateTorque", engine);
    const auto top = model.addRelationship(RelationshipKind::Allocation, generator, powerTrain);
    const auto refined = model.addRelationship(RelationshipKind::Allocation, generate, engineGenerate);
    model.setParentRelationship(refined, top);
    CHECK_THROWS_AS(model.setParentRelationship(top, refined), ModelError); // no cycle
    model.setRelationshipContext(top, logical);
    CHECK(model.relationship(top).Context == std::optional<ElementId>(logical));
    CHECK_THROWS_AS(model.setRelationshipContext(top, generator), ModelError); // not one of its ends
    model.removeRelationship(top);
    CHECK(!model.containsRelationship(refined));

    const auto again = model.addRelationship(RelationshipKind::Dependency, engine, generator);
    model.setRelationshipContext(again, powerTrain);
    model.removeElement(powerTrain); // engine (an end) goes, so does the dependency
    CHECK(!model.containsRelationship(again));
    const auto other = model.addElement(ElementKind::PartUsage, "other");
    const auto dependency = model.addRelationship(RelationshipKind::Dependency, other, generator);
    const auto holder = model.addElement(ElementKind::Package, "Holder");
    model.setRelationshipContext(dependency, holder);
    model.removeElement(holder);
    CHECK(model.containsRelationship(dependency));
    CHECK(!model.relationship(dependency).Context.has_value());
}
