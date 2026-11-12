# Diagram editor

The diagram editor shows a SysML v2 (or KerML) block of a document as diagrams and writes every change back into
the text. Text and diagram are two views of the same model: everything the diagram shows can be drawn in it, and
everything drawn becomes SysML v2 text that reads back into the same diagram.

## Opening it

- **Ctrl+Shift+D** or the diagram button of the navigation rail: the editor for the current document. Its generated
  block is created on the first change.
- **Open in diagram editor** in the diagram preview of a SysML card: the editor for exactly that card.

One window per document. The window is closed together with its document.

## Views

| View | Shows | Drawn with |
| --- | --- | --- |
| Structure | packages (frames), definitions, parts, ports, items, requirements, cases, interactions, allocations, views, metadata, imports | palette tiles, arrows between nodes (connection, flow, interface, specialization, satisfy, verify, frame, allocate, dependency) |
| Parametric | values, calculations and constraints of one part with their bindings | values, calc / constraint definitions, lines between pins |
| Behavior | the flow of one action (actions, control nodes, send / accept / assign / terminate, if / while / loop / for) or the states of one state machine | palette tiles, arrows (succession, transition with trigger, guard and effect) |
| Use cases | use cases, actors, the subject as system boundary, «include» | palette tiles, lines |
| Sequence | the lifelines, events, «perform» boxes and messages of one interaction (occurrence definition) | lifelines, drag from lifeline to lifeline |
| Interconnection | one part (or part definition) with its parts, items and ports nested inside it, and the connections, flows, interfaces and bindings between them | palette tiles (dropped into a part), arrows between ports / parts |

### Notation (as in *Intro to the SysML v2 Language – Graphical Notation*)

| Drawn | Meaning | Text |
| --- | --- | --- |
| dashed line, open head, `«import» *` / `«import» **` / `«import»` | import between two packages shown | `private import Definitions::*;` |
| dashed package with `«import» *` | import of a library package (ISQ, ScalarValues) | `private import ISQ::*;` |
| hollow triangle with `:` | usage defined by a definition | `part engine : Engine;` |
| hollow triangle | specialization / subsetting | `part def B :> A;`, `part c subsets b;` |
| hollow triangle with a bar | redefinition | `part :>> leftFront;` |
| open head with `::` | a part performs an action | `perform providePower;` |
| filled diamond / hollow diamond, right-angled tree | composition / reference (`ref part`, dashed node) | |
| half disc | timeslice / snapshot of an occurrence | `timeslice t1 { ... }` |
| filled triangle in the middle, `name : Item` | flow | `flow of Fuel from a.p1 to b.p2;` |
| line with `: Type` | interface / connection | `interface : I connect a to b;` |
| line with `=` | binding | `bind a = b;` |
| dashed line, open head | succession (behavior), «satisfy», «verify», dependency | `first a then b;` |
| solid line, open head | transition, message, «allocate» | `transition first a then b;` |

Inherited features are listed automatically with `^` (`^mass`, `^providePower`, `^{fuel.fuelMass <= fuelMassMax}`);
redefined ones are listed under their own entry (`mass :>> mass = 1500 [kg]`). Compartments follow the slides:
*attributes*, *ports*, *directed features* (port definitions), *parameters* (actions, calculations with `return`),
*perform actions*, *exhibit states*, *state actions* (`entry` / `do` / `exit`), *assert constraints*, *constraints*
(requirements: `require {...}`, `assume {...}`), *enums*, *expression*.

The behavior view draws a frame with the name of the action or state shown, nested states inside their state (a
parallel state's states side by side) and input / output flows between actions.

Double-click a node, an entry or an arrow to edit it (name, id, type, multiplicity, value, prefixes, redefinitions,
what an action does, documentation, ...). Del removes the selection.

- **Type**: while typing, the fitting definitions of the block are offered (port defs for a port, part defs for a
  part, attribute and enum defs for an attribute, ...), for attributes also library types (`ScalarValues::Real`,
  `ISQ::MassValue`, ...). Up / Down and Enter choose, Esc keeps the input. A simple name that is defined nowhere is
  marked (*'KrafPort' is not defined in this block.*) with the tick *Create port def KrafPort*: on *Apply* the
  definition is created in the package of the element. Qualified names (`ISQ::TorqueValue`) count as library types.
- **Ports**: parts, part definitions, items and port definitions list their ports in the edit dialog: add, rename,
  retype or remove them there (with the same type suggestions); then only the connections are drawn.
- Ports sit on the left (`in`) or right border; a part grows until its ports fit. Their labels are placed where they
  cover neither other ports, their labels nor other nodes, and follow when a port or a node moves.
- In the document, a block written by the diagram editor shows its diagram below the code (with the stored positions)
  until it is hidden with the *Diagram* button.

## Generated block and hand edits

The editor writes into one dedicated block. Its first line carries a fingerprint of the code below it:

```
// Generated by the StructuraSystems diagram editor (fingerprint 3f2a9c0d11e4b7a8). Manual edits pause the generation.
// Layout: {"s":{"vehicle":[40,40],"vehicle::engine":[40,180]},"b":{"ProvidePower":{"ProvidePower::check":[220,40]}}}
part vehicle {
    part engine;
}
```

- **Live**: every change is written immediately. **On demand**: only *Generate SysML* writes.
- A block whose code no longer matches its fingerprint was edited by hand: the editor stops writing until you choose
  *Replace with diagram* (the diagram wins) or *Keep text* (the diagram is read again from the text).
- A hand-written block opened in the editor is only written after *Replace with diagram*.
- The second line (`// Layout: ...`) stores the node positions of the structure, behavior and use case views. It is
  not part of the fingerprint: moving a node never counts as an edit of the code. Without it (or for elements not
  listed) the nodes are arranged automatically.
- Empty lines between statements and the indentation of the block (spaces or tabs) are kept when it is written back.

## Round trip

Reading a block and writing it back gives the same text, apart from formatting that has no meaning (spacing within a
line, the order of `:>` and `[...]`, `then b;` for a succession written elsewhere, relationships written at the end of
their body, one-line bodies written on several lines). Statements the diagram does not model are kept word for word as
text and shown as text entries; the bar above the diagram lists what of the block the diagram does not show.

Covered as model elements (following the slides of *Introduction to the SysML v2 Language*):

| Area | Language |
| --- | --- |
| Packages and namespaces | `package`, `import` (visibility, `::*`, `::**`, filter conditions, `all`), `alias`, `filter`, comments, `doc`, short names `<'1.1'>` |
| Structure | part / attribute / item / port / interface / enum / occurrence / connection / flow / allocation definitions and usages, specialization `:>`, redefinition `:>>` / `redefines`, multiplicities, `ordered` / `nonunique`, values (`=`, `:=`, `default`), prefixes (`ref`, `derived`, `abstract`, `variation`, `variant`, `individual`, `#metadata`, ...), conjugated ports, `snapshot` / `timeslice` |
| Connections | `connect`, `connection`, `interface`, `flow` (with payload and flow definition), `bind` |
| Behavior | actions, `perform`, successions (`first`, `then`, guards), `decide` / `merge` / `fork` / `join`, `start` / `done`, `send`, `accept` (`via`, `at`, `after`, `when`), `assign`, `terminate`, `if` / `else`, `while`, `loop ... until`, `for`, states, `parallel`, `entry` / `do` / `exit`, transitions with trigger, guard and effect |
| Calculations and constraints | `calc def`, `constraint def`, KerML `function` / `predicate`, usages, parameters, `assert constraint` |
| Requirements and cases | requirements, `subject`, `actor`, `stakeholder`, `require` / `assume constraint`, `satisfy`, use cases with `include`, verifications with `verify`, analysis cases, `concern`, `viewpoint`, `frame` |
| Interactions | event occurrences, messages (`then message`), successions between lifelines |
| Allocation and dependencies | `allocate` (with refining allocations), `allocation def`, `dependency` |
| Metadata and views | `metadata def`, `metadata ... about`, `@Type { ... }`, `view def`, `view` with `expose`, `render`, sub-views, `satisfy` of a viewpoint |

Kept as text (complete, but not drawn): user-defined keywords (`#scenario def ...`), `rendering def`, connections
with `references` or end multiplicities (`connect [0..1] a to [1] b`), flows between action parameters, `frame concern
c;`, `:> annotatedElement : ...` in metadata definitions, a dependency with several ends.

## Architecture

```
stucturasystems/src/Diagram/          plain C++20, no Qt (library StructuraDiagramCore)
  Semantic/SysMLElement               element and relationship kinds, the data of one element
  Semantic/SemanticModel              the element tree and the relationships, change notification
  Semantic/RelationshipRules          what may own / connect what (used by the model and the views)
  Conversion/SysMLTextImporter        text -> model (everything unknown is kept as Textual element)
  Conversion/SysMLCodeGenerator       model -> text
  Conversion/GeneratedBlockGuard      marker line, fingerprint, layout line
stucturasystems/src/ViewModels/Diagram/   Qt view models
  DiagramEditorModel                  one editor window: loads the block, generates, writes, guards hand edits
  DiagramController                   structure view
  ParametricController, BehaviorController, UseCaseController, SequenceController
  DiagramNodeModel, DiagramEdgeModel  nodes and edges for QML
  EdgeLayerItem, EdgeRouting          drawing of the lines (routed around the nodes in between)
  DiagramLayout                       keys of the stored node positions
stucturasystems/qml/diagram/          windows, canvases, nodes, palette, dialogs
```

The views never store model data of their own: each change is a change of the `SemanticModel`, which notifies the
views and the editor model (which regenerates the text).

## Tests

```
cmake --preset conan-debug -DRUN_TESTS=ON
cmake --build --preset conan-debug
ctest --test-dir build/Debug       # StructuraDiagramCoreTests, StructuraDiagramViewModelTests
```

The core tests need no Qt and can also be built directly:

```
cd stucturasystems
g++ -std=c++20 -Wall -Wextra -Werror -Isrc -Itests src/Diagram/*/*.cpp tests/Diagram/*.cpp tests/TestMain.cpp -o core_tests
./core_tests
```

Besides the tests per language area (with the examples of the slides), *random models survive generate -> import ->
generate* builds 1000 random models with every element and relationship kind and checks that text written by the
generator reads back into the same model and the same text.

## Adding a language construct

1. `SysMLElement` / `SemanticModel`: the kind or field and its setter (with checks: what may not be written must not be
   storable).
2. `RelationshipRules`: who may own it, what it may be connected to.
3. `SysMLTextImporter`: read it; anything that does not fit falls back to text.
4. `SysMLCodeGenerator`: write it.
5. A test with the example text (written back exactly) and an extension of the random round trip.
6. The views: node or compartment entry, palette tile, fields in the edit dialog.
