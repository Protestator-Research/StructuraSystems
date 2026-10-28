# StructuraSystems – Abgleich mit „Intro to the SysML v2 Language – Graphical Notation“

Dieses Dokument geht die Präsentation Folie für Folie durch (F.xx = Foliennummer unten rechts auf der Folie) und
zeigt zu jedem Thema ein SysML-Beispiel, mit dem du die Umsetzung im Diagramm-Editor prüfen kannst.

**So prüfst du einen Abschnitt**

1. Den SysML-Block des Abschnitts in ein neues Dokument kopieren (ein Block reicht, jeder ist für sich vollständig).
2. Den Diagramm-Editor öffnen und die angegebene Ansicht wählen (Structure, Behavior, Sequence, Interconnection, …).
3. Unter „Das solltest du sehen“ steht, was die Folie zeigt und was StructuraSystems daraus macht.
4. „Generate SysML“ bzw. Live-Modus: der erzeugte Text muss inhaltlich gleich bleiben (Round Trip).

Alle Beispiele wurden automatisch geprüft: Import → Generierung → Import ist stabil, nichts geht verloren
(„lossless“). Wo eine Anweisung nur als Text erhalten bleibt (nicht zeichenbar), steht das beim Beispiel.

**Status**

| Zeichen | Bedeutung |
|---|---|
| ✅ | wie auf der Folie umgesetzt |
| 🟡 | umgesetzt, aber mit Abweichung (steht dabei) |
| ⬜ | nicht als Grafik umgesetzt, bleibt aber verlustfrei als Text erhalten |

## Übersicht

| Folie | Thema | Ansicht | Status |
|---|---|---|---|
| F.1–15 | Einführung, Sprachüberblick | – | (kein Modellinhalt) |
| F.17–18 | Pakete, Imports `«import»`, `*`, `**` | Structure | ✅ |
| F.19–22 | Definitionspakete, Paket-Kompartimente, qualifizierte Namen, Kurznamen `<1.1>`, Alias | Structure | ✅ (Paket-Kompartiment „members“: 🟡, Pakete sind Rahmen) |
| F.24–25 | Part def, Port def („directed features“), Action def („parameters“), State def | Structure | ✅ |
| F.26 | Spezialisierung mit geerbten Merkmalen `^` | Structure | ✅ |
| F.27 | Redefinition `:>>` / `redefines`, Subsetting `:>` / `subsets` | Structure | ✅ |
| F.29–32 | Usages, `^` geerbt, Enumeration mit/ohne Einheiten, „defined by“ `:` | Structure | ✅ |
| F.34–36 | Zerlegung, „defined by“-Pfeil mit `:`, Teilmenge eines Parts | Structure | ✅ |
| F.37–38 | Referenzen (gestrichelt, hohle Raute), Bindung `=`, Multiplizität | Structure / Interconnection | ✅ |
| F.40 | Interconnection View (Parts in Parts, Ports am Rand, Verbindungen) | **neuer Reiter Interconnection** | ✅ |
| F.41–44 | Interface def, Interface-Verbindung | Structure / Interconnection | 🟡 (kein Ball/Socket mehr, Label `: Typ`) |
| F.46–47 | Variabilität `variation` / `variant` | Structure | ✅ |
| F.50–51 | Actions, Action-Zerlegung | Structure | ✅ |
| F.52 | Part performs action: Pfeil mit `::` | Structure | ✅ |
| F.53–54 | `perform action1` / `perform action action1` | Structure | 🟡 (als Kompartiment „perform actions“, nicht als eigener Knoten) |
| F.56–57 | Action Flow, Ein-/Ausgangsflüsse, Succession Flow | Behavior | 🟡 (Flüsse zwischen Aktionen, ohne Parameter-Pins, ohne Swimlanes) |
| F.58–61 | start/done, decide/merge/fork/join, Guards | Behavior | ✅ |
| F.62 | Szenario mit send/accept | Behavior / Sequence | 🟡 (einige Zeilen nur als Text, siehe Abschnitt) |
| F.64 | States, `entry`/`do`/`exit` („state actions“) | Behavior | ✅ |
| F.65 | Parallele und verschachtelte States | Behavior | ✅ |
| F.66 | State actions und assert constraints im State | Behavior | ✅ |
| F.67 | Part exhibits states | Structure | ✅ |
| F.69 | Sequence View, Nachrichten `vs : VehicleStart`, `«perform»` auf Lebenslinie | Sequence | ✅ |
| F.71, 74–76 | Individuals, Snapshots, Timeslices, Redefinition `:>>` | Structure | ✅ |
| F.78 | Expressions, Default-Werte | Structure | ✅ |
| F.79 | Calc def: `return` gehört zu „parameters“ | Structure | ✅ |
| F.80 | Calc-Usage mit gebundenen Werten | Structure | ✅ |
| F.81 | Binding Connection | Structure / Interconnection | ✅ (siehe Recherche am Ende) |
| F.83 | Quantities & Units | Structure | ✅ |
| F.85–87 | Constraint-Ausdruck, geerbter Constraint `^{…}`, Constraint def | Structure | ✅ |
| F.88 | Binding Constraints (parametrisch) | Parametric | ✅ (vorhandene Ansicht) |
| F.90–95 | Requirements, `require`/`assume`, satisfy, Allocation, Derived | Structure | ✅ (`#derivation connection`: ⬜) |
| F.97 | Use Case | Use Cases | ✅ |
| F.99–100 | Analysis Case, Trade-off | Structure | ✅ |
| F.102 | Verification Case | Structure | ✅ |
| F.104–106 | Dependency, Allocation, Cause-Effect | Structure | ✅ (Cause-Effect: ⬜) |
| F.108–109 | Annotations, Metadaten | Structure | ✅ |
| F.111 | Element-Filter | Structure | ✅ |
| F.113–116 | Views und Viewpoints | Structure | ✅ |
| F.118 | Spracherweiterung (Metadaten-Schlüsselwörter) | Structure | ✅ (als «Stereotyp») |

## 1. Pakete, Imports und Namen (F.17–22)
**Ansicht:** Structure

**Das solltest du sehen**
- Die Pakete sind Rahmen mit Reiter, `Definitions` liegt in `SimpleVehicleModel`.
- Imports zwischen gezeigten Paketen sind **gestrichelte Pfeile mit offener Spitze** und der Beschriftung
  `«import» *` (Paketimport), `«import»` (Elementimport) bzw. `«import» **` (rekursiv) – F.18.
- Ein Import einer Bibliothek (`ISQ::*`) ist ein **gestricheltes Paket** im importierenden Paket mit
  `private «import» *` und dem Namen `ISQ`.
- Der Kurzname steht in spitzen Klammern: `<'1.1'> 'Sports Utility Vehicle'`, der Alias als eigener Knoten
  `«alias» SUV for 'Sports Utility Vehicle'` (F.22).

**Zum Ausprobieren:** einen Pfeil von `VehicleConfigurations` auf `Definitions` ziehen → Art „Import“ wählen →
im Text erscheint `private import Definitions::*;`. Den Pfeil löschen entfernt den Import wieder.

```sysml
package SimpleVehicleModel {
    package Definitions {
        part def <'1.1'> 'Sports Utility Vehicle';
        alias SUV for 'Sports Utility Vehicle';
        part def Vehicle;
    }
    package VehicleConfigurations {
        private import Definitions::*;
        public import Definitions::Vehicle;
        private import Definitions::**;
        private import ISQ::*;
        part vehicle_a : Vehicle;
    }
}
```

## 2. Definitionen (F.24–25)
**Ansicht:** Structure

**Das solltest du sehen**
- `«part def» Vehicle` mit den Kompartimenten **attributes** (`mass :> ISQ::mass` – die Spezialisierung steht im
  Eintrag), **perform actions** (`providePower`) und **exhibit states** (`vehicleStates : VehicleStates`).
  Die Ports sitzen als kleine Quadrate am Rand.
- `«port def» FuelCmdPort` mit **directed features**: `in item fuelCmd : FuelCmd`, `in attribute x : Real`.
- `«action def» ProvidePower` mit **parameters**: `in pwrCmd : PwrCmd`, `out torqueToWheels : Torque[*]`.
- `«state def» VehicleStates` mit **state actions**: `do providePower`.
- `Vehicle` → `providePower`: Pfeil mit offener Spitze und `::` (perform = reference subsetting, F.52).

```sysml
package Definitionen {
    item def FuelCmd;
    item def PwrCmd;
    attribute def Torque;
    port def FuelCmdPort {
        in item fuelCmd : FuelCmd;
        in attribute x : Real;
    }
    action def ProvidePower {
        in pwrCmd : PwrCmd;
        out torqueToWheels : Torque[*];
    }
    action providePower : ProvidePower;
    state def VehicleStates {
        do providePower;
    }
    part def Vehicle {
        attribute mass :> ISQ::mass;
        attribute dryMass;
        attribute cargoMass;
        port pwrCmdPort;
        port vehicleToRoadPort;
        perform providePower;
        exhibit state vehicleStates : VehicleStates;
    }
}
```

## 3. Spezialisierung, Redefinition, Subsetting (F.26–27)
**Ansicht:** Structure

**Das solltest du sehen**
- `FrontAxle → Axle`: hohles Dreieck (Spezialisierung). In `FrontAxle` steht unter attributes
  `steeringAngle` (hinzugefügt) und **`^mass`** (geerbt, F.26 – immer automatisch).
- `FuelCmdPort` zeigt `in item fuelCmd : FuelCmd redefines pwrCmd`; `pwrCmd` wird nicht mehr als geerbt gezeigt,
  weil es redefiniert ist.
- In `Engine4Cyl` (F.27):
  - `cylinders[4] redefines cylinders` – Pfeil **hohles Dreieck mit Querstrich** (Redefinition) auf
    `cylinders : Cylinder[4..8]`,
  - `cylinder1[1]`, `cylinder2[1]` mit `subsets cylinders`, `cylinder3[1]`, `cylinder4[1]` mit `:> cylinders` – als
    Pfeil (hohles Dreieck) auf den redefinierten `cylinders`. Steht das Ziel nicht im Diagramm, steht das
    Schlüsselwort so im Titel, wie es geschrieben wurde (`subsets` bzw. `:>`).
- Die Zerlegung (Raute am Besitzer) ist als **rechtwinkliger Baum** geführt.

```sysml
package Spezialisierung {
    part def Axle {
        attribute mass;
    }
    part def FrontAxle :> Axle {
        attribute steeringAngle;
    }
    port def PwrCmdPort {
        in item pwrCmd : PwrCmd;
    }
    port def FuelCmdPort :> PwrCmdPort {
        in item fuelCmd : FuelCmd redefines pwrCmd;
    }
    part def Engine {
        part cylinders : Cylinder[4..8];
    }
    part def Engine4Cyl :> Engine {
        part redefines cylinders[4];
        part cylinder1[1] subsets cylinders;
        part cylinder2[1] subsets cylinders;
        part cylinder3[1] :> cylinders;
        part cylinder4[1] :> cylinders;
    }
}
```

## 4. Usages, Enumerationen, „defined by“ (F.29–32)
**Ansicht:** Structure

**Das solltest du sehen** (vgl. F.29 `vehicle_b : Vehicle`)
- `vehicle_b : Vehicle` → `Vehicle`: **hohles Dreieck mit `:`** („defined by“, F.32 – immer automatisch, wenn
  die Definition im Diagramm ist).
- attributes: `mass :>> mass = dryMass + cargoMass`, `dryMass :>> dryMass = 1200 [kg]`,
  `cargoMass :>> cargoMass default 0 [kg]` (redefiniert), `color1 : Colors = Colors::blue`, `^velocity` (geerbt).
- ports: `^pwrCmdPort`, perform actions: `^providePower`, exhibit states: `^vehicleStates` – alles geerbt.
- `«enum def» Colors` mit dem Kompartiment **enums** (`red`, `blue`, `green`), `DiameterChoices` mit
  `= 60 [mm]`, `= 70 [mm]` (Werte mit Einheiten werden jetzt als Werte gelesen, F.31).

```sysml
package Usages {
    part def Vehicle {
        attribute mass;
        attribute dryMass;
        attribute cargoMass;
        attribute velocity;
        port pwrCmdPort;
        perform providePower;
        exhibit state vehicleStates;
    }
    action providePower;
    enum def Colors {
        red;
        blue;
        green;
    }
    enum def DiameterChoices :> ISQ::LengthValue {
        enum = 60 [mm];
        enum = 70 [mm];
    }
    part vehicle_b : Vehicle {
        attribute :>> mass = dryMass + cargoMass;
        attribute :>> dryMass = 1200 [kg];
        attribute :>> cargoMass default 0 [kg];
        attribute color1 : Colors = Colors::blue;
        part fuelTank : FuelTank;
    }
    part def FuelTank;
}
```

## 5. Zerlegung (F.34–36)
**Ansicht:** Structure

**Das solltest du sehen**
- `Vehicle` und `RearAxleAssembly` mit ihren Parts als Baum (Raute am Besitzer, rechtwinklig geführt).
- `rearAxleAssembly : RearAxleAssembly` → `«part def» RearAxleAssembly`: hohles Dreieck mit **`:`** am Pfeil (F.34).
- `vehicle_c : Vehicle subsets vehicle_b` → `vehicle_b`: hohles Dreieck (F.36); in `vehicle_c` steht `^bodyAssy`.

```sysml
package Zerlegung {
    part def Vehicle {
        part engine : Engine;
        part transmission : Transmission;
        part rearAxleAssembly : RearAxleAssembly;
    }
    part def RearAxleAssembly {
        part differential : Differential;
        part rearAxle : Axle;
        part rearWheel1 : Wheel;
        part rearWheel2 : Wheel;
    }
    part def Engine;
    part def Transmission;
    part def Differential;
    part def Axle;
    part def Wheel;
    part vehicle_b : Vehicle {
        part bodyAssy : BodyAssy;
    }
    part def BodyAssy;
    part vehicle_c : Vehicle subsets vehicle_b;
}
```

## 6. Referenzen und Multiplizität (F.37–38)
**Ansicht:** Structure

**Das solltest du sehen**
- `ref part hitchBall` / `ref part trailerCoupler`: **gestrichelter Rahmen**, am Besitzer eine **hohle Raute**
  (Referenz, keine Komposition).
- `bind vehicle.hitchBall = trailerHitch.hitchBall`: durchgezogene Linie mit **`=`** (F.37).
- `ref item fuel : Fuel` steht im Kompartiment items von `fuelTank`; `wheels : Wheel[4]` zeigt die Multiplizität.

```sysml
package References {
    part vehicleTrailerSystem {
        part vehicle {
            ref part hitchBall;
        }
        part trailerHitch[1] {
            part hitchBall[1];
            part trailerCoupler[1];
        }
        part trailer {
            ref part trailerCoupler;
        }
        bind vehicle.hitchBall = trailerHitch.hitchBall;
        bind trailerHitch.trailerCoupler = trailer.trailerCoupler;
    }
    part fuelTank {
        ref item fuel : Fuel;
    }
    part wheels : Wheel[4];
}
```

## 7. Interconnection View und Interfaces (F.40–44)
**Ansicht:** **Interconnection** (neuer Reiter rechts neben Sequence), Kontext „part Interconnection::vehicle_b“.

**Das solltest du sehen** (F.40)
- `vehicle_b : Vehicle` als großer Rahmen, darin `fuelTank`, `engine`, `transmission`, `vehicleSoftware`;
  `vehicleController` liegt in `vehicleSoftware`, `ref fuel : Fuel` (gestrichelt, `«item»`) in `fuelTank`.
- Ports als Quadrate am Rand ihres Parts: `p1 : FuelOutPort`, `p2 : ~FuelOutPort`, `drivePort`, `enginePort`.
- Interface `p1 – p2` mit dem Label `: FuelTankToEngine_IF` (ohne «interface», ohne Ball/Socket – wie F.41).
- Fluss `p1.fuel → p2.fuel` als Linie mit **gefülltem Dreieck in der Mitte** und `: Fuel`.
- `connect drivePort to enginePort` als Linie, `bind … = …` mit `=`.
- In der Structure-Ansicht zeigt `«interface def» FuelTankToEngine_IF` die Kompartimente **ports** und den Fluss.

**Zum Ausprobieren:** in der Interconnection-Ansicht einen Part in `engine` ziehen → er wird Part von `engine`
und `engine` wird zum Rahmen; zwei Ports mit einem Pfeil verbinden → Connect/Interface/Flow/Bind wählen.

```sysml
package Interconnection {
    item def Fuel;
    port def FuelOutPort {
        out item fuel : Fuel;
    }
    interface def FuelTankToEngine_IF {
        end port p1 : FuelOutPort;
        end port p2 : ~FuelOutPort;
        flow of Fuel from p1.fuel to p2.fuel;
    }
    part def Vehicle;
    part vehicle_b : Vehicle {
        part fuelTank {
            ref item fuel : Fuel;
            port p1 : FuelOutPort;
        }
        part engine {
            port p2 : ~FuelOutPort;
            port drivePort;
        }
        part transmission {
            port enginePort;
        }
        part vehicleSoftware {
            part vehicleController;
        }
        interface : FuelTankToEngine_IF connect fuelTank.p1 to engine.p2;
        flow of Fuel from fuelTank.p1.fuel to engine.p2.fuel;
        connect engine.drivePort to transmission.enginePort;
        bind engine.drivePort = transmission.enginePort;
    }
}
```

## 8. Variabilität (F.46–47)
**Ansicht:** Structure

**Das solltest du sehen**
- `«variation» engine : Engine` mit `«variant» engine4cyl` / `engine6cyl` darunter, ebenso `transmission`.
- assert constraints von `vehicleFamily` mit dem xor-Ausdruck; `vehicle_a :> vehicleFamily` mit
  `engine :>> engine = engine::engine6cyl` usw.

```sysml
package Variability {
    part def Engine;
    part def Transmission;
    part vehicleFamily {
        variation part engine : Engine {
            variant part engine4cyl;
            variant part engine6cyl;
        }
        variation part transmission : Transmission {
            variant part transmissionAuto;
            variant part transmissionManual;
        }
        assert constraint {
            (engine == engine::engine4cyl) xor (engine == engine::engine6cyl and transmission == transmission::transmissionAuto)
        }
    }
    part vehicle_a :> vehicleFamily {
        part :>> engine = engine::engine6cyl;
        part :>> transmission = transmission::transmissionAuto;
    }
}
```

## 9. Actions und Perform (F.50–54)
**Ansicht:** Structure (und Behavior für die Abläufe)

**Das solltest du sehen**
- `«action» providePower : ProvidePower` mit **parameters** `in fuelCmd : FuelCmd :>> pwrCmd` und **`^out torque : Torque[*]`**
  (geerbt, F.50); seine Aktionen `generateTorque` … `transferTorque` als Aktionsbaum (Raute) darunter (F.51).
- `vehicle_b` → `providePower`: offener Pfeil mit **`::`** (F.52); `engine` → `generateTorque` ebenso
  (`perform providePower.generateTorque`).
- `part0`: **perform actions** `action1`, `action2` (F.53 zeigt die Variante als Kompartiment).

```sysml
package Aktionen {
    item def PwrCmd;
    item def FuelCmd;
    attribute def Torque;
    action def ProvidePower {
        in pwrCmd : PwrCmd;
        out torque : Torque[*];
    }
    action providePower : ProvidePower {
        in fuelCmd : FuelCmd :>> pwrCmd;
        action generateTorque;
        action amplifyTorque;
        action distributeTorque;
        action transferTorque;
    }
    part def Vehicle;
    part def Engine;
    part vehicle_b : Vehicle {
        perform providePower;
        part engine : Engine {
            perform providePower.generateTorque;
        }
    }
    part part0 {
        perform action action1;
        perform action2;
    }
    action action2;
}
```

## 10. Action Flow (F.56–62)
**Ansicht:** Behavior, Kontext „action ActionFlow::transportPassenger“ bzw. „…::providePowerFlow“.

**Das solltest du sehen**
- Rahmen mit dem Reiter **„action transportPassenger“** um den Ablauf.
- Aktionen mit **«action»**, Successions **gestrichelt mit offener Spitze**, Guards `[vehicleStarted]`,
  `[anotherDestination]`, `[else]` (F.58–60).
- Steuerknoten: Raute (decide/merge), Balken (fork/join), Start (gefüllter Kreis), Done (Kreis mit Punkt);
  daneben nur der Name (`merge1`, nicht mehr `merge merge1`).
- `providePowerFlow`: zwischen den Aktionen zusätzlich die **Flüsse** (gefülltes Dreieck in der Mitte, `: Torque`, F.57).
  `succession flow` wird jetzt gelesen und geschrieben (vorher nur Text).

**Abweichungen:** Parameter-Pins an den Aktionen und Swimlanes (F.56) sind nicht gezeichnet; der Fluss geht von
Aktion zu Aktion, der Parameter steht im Text.

**Folie 62 (Szenario):** Bindings/Flüsse wie `flow of VehicleStart from trigger1.vs to startEngine.vs;` werden jetzt
als Fluss gelesen. Nur als Text (verlustfrei) bleiben: `action :>> trigger1 accept … via …` (anonyme redefinierte
Aktionen mit send/accept), `event startVehicle.driverReady;` und die Kurzform `interface driver.p1 to vehicle.p1;`
(ohne `connect`).

```sysml
package ActionFlow {
    action transportPassenger {
        first start;
        then unlockDoor;
        action unlockDoor;
        then fork1;
        fork fork1;
        then openDoor;
        then providePower;
        action openDoor;
        then join1;
        action providePower;
        then join1;
        join join1;
        then startVehicle;
        action startVehicle;
        if vehicleStarted then merge1;
        merge merge1;
        then driveVehicle;
        action driveVehicle;
        then lockDoor;
        action lockDoor;
        then decision1;
        decide decision1;
        if anotherDestination then merge1;
        else done;
    }
    attribute def Torque;
    action providePowerFlow {
        action generateTorque {
            out torque : Torque;
        }
        action amplifyTorque {
            in torque : Torque;
            out amplified : Torque;
        }
        action transferTorque {
            in amplified : Torque;
        }
        flow of Torque from generateTorque.torque to amplifyTorque.torque;
        succession flow of Torque from amplifyTorque.amplified to transferTorque.amplified;
        first generateTorque then amplifyTorque;
        first amplifyTorque then transferTorque;
    }
}
```

## 11. States (F.64–67)
**Ansicht:** Behavior, Kontext „state States::vehicleStates“.

**Das solltest du sehen** (F.65/66)
- Rahmen **„state vehicleStates : VehicleStates «parallel»“**.
- `operatingStates` und `healthStates` **nebeneinander** (parallel) und ihre States **darin** gezeichnet.
- Jeder State mit **«state»**; `on` mit den Kompartimenten **state actions** (`entry performSelfTest`,
  `do providePower`, `exit applyParkingBrake`) und **assert constraints** (`{electricalPower <= 500}`).
- Transitionen durchgezogen mit offener Spitze, Label `vehicleStart [brakePedalDepressed] / send engineHealthStatus to driver`.
- Verschiebt man `operatingStates`, gehen die inneren States mit.
- In der Structure-Ansicht: `«state def» VehicleStates` mit **state actions** `entry action1`, `do action2`,
  `exit action3` (vorher fälschlich „perform action1“); `vehicle_b` mit perform actions und **exhibit states** (F.67).

```sysml
package States {
    action action1;
    action action2;
    action action3;
    action performSelfTest;
    action providePower;
    action applyParkingBrake;
    state def VehicleStates {
        entry action1;
        do action2;
        exit action3;
    }
    state vehicleStates : VehicleStates parallel {
        state operatingStates {
            entry;
            then off;
            state off;
            state starting {
                do action startEngine;
                exit action sendStatus;
            }
            state on {
                entry performSelfTest;
                do providePower;
                exit applyParkingBrake;
                assert constraint {
                    electricalPower <= 500
                }
            }
            transition first off accept vehicleStart if brakePedalDepressed do send engineHealthStatus to driver then starting;
            transition first starting if vehicleStarted then on;
            transition first on accept vehicleOff then off;
        }
        state healthStates {
            entry;
            then normal;
            state normal;
            state maintenance;
            state degraded;
            transition first normal then maintenance;
            transition first normal then degraded;
            transition first degraded then maintenance;
            transition first maintenance then normal;
        }
    }
    part vehicle_b {
        perform providePower;
        exhibit state vehicleStates2 : VehicleStates;
    }
}
```

## 12. Sequence View (F.69)
**Ansicht:** Sequence

**Das solltest du sehen**
- Lebenslinien `«part» driver : Driver` und `«part» vehicle_b : Vehicle` (auch bei `ref part` steht «part»).
- Ereignisse als Punkte (`driverReady`, `vsSent`, …). Die `then`-Folge auf **einer** Lebenslinie wird nicht mehr als
  Pfeil gezeichnet – die Reihenfolge von oben nach unten zeigt sie.
- Nachrichten `vs : VehicleStart` und `es : EngineStatus`.
- **`«perform» startEngine`** als Kasten auf der Lebenslinie von `vehicle_b`, zwischen `vsReceived` und `esSent`.
- `then` zwischen Ereignissen verschiedener Lebenslinien: gestrichelter Pfeil.

```sysml
package Scenario_10 {
    item def VehicleStart;
    item def EngineStatus;
    part def Driver;
    part def Vehicle;
    action startEngine;
    occurrence def StartVehicle {
        ref part driver : Driver {
            event occurrence driverReady;
            then event occurrence vsSent;
            then event occurrence esReceived;
        }
        ref part vehicle_b : Vehicle {
            event occurrence doorClosed;
            then event occurrence vsReceived;
            perform startEngine;
            then event occurrence esSent;
        }
        message vs of VehicleStart from driver.vsSent to vehicle_b.vsReceived;
        message es of EngineStatus from vehicle_b.esSent to driver.esReceived;
    }
}
```

## 13. Individuals, Timeslices (F.71, 74–76)
**Ansicht:** Structure

**Das solltest du sehen**
- `«individual» «part def» Vehicle-1` → `Vehicle` (Spezialisierung), mit `vin :>> vin = 1` und `^mass` (F.71).
- `«individual» «part» vehicle_b-1 : 'Vehicle-1'` mit `:` auf `Vehicle-1` und Subsetting auf `vehicle_b`.
- Die Timeslices `«timeslice» vehicle_b_t1-t2` / `vehicle_b_t2-t3` hängen am Individual (zeitliche Zerlegung,
  Halbkreis am Besitzer).
- In den Timeslices: `leftFront : 'Wheel-1' :>> leftFront` – Pfeil **hohles Dreieck mit Querstrich** auf
  `leftFront` von `Vehicle` (Redefinition, F.76 „wie Folie, als `:>>`“).

```sysml
package Individuals {
    part def Vehicle {
        attribute vin : Integer;
        attribute mass;
        part leftFront : Wheel;
        part rightFront : Wheel;
    }
    part def Wheel;
    individual part def 'Vehicle-1' :> Vehicle {
        attribute :>> vin = 1;
    }
    individual part def 'Wheel-1' :> Wheel;
    individual part def 'Wheel-2' :> Wheel;
    part vehicle_b : Vehicle;
    individual part 'vehicle_b-1' : 'Vehicle-1' :> vehicle_b {
        timeslice 'vehicle_b_t1-t2' {
            part :>> leftFront : 'Wheel-1';
            part :>> rightFront : 'Wheel-2';
        }
        then timeslice 'vehicle_b_t2-t3' {
            part :>> leftFront : 'Wheel-2';
            part :>> rightFront : 'Wheel-1';
        }
    }
}
```

## 14. Expressions und Calculations (F.78–80)
**Ansicht:** Structure

**Das solltest du sehen**
- `vehicle_b`: `mass :>> mass = dryMass + cargoMass + fuelTank.mass`, `dryMass :>> dryMass = sum(partMasses)`,
  `cargoMass :>> cargoMass default 0 [kg]`, `partMasses = (…)` (F.78).
- `«calc def» Force` mit **parameters**: `in m :> ISQ::mass`, `in a :> ISQ::acceleration`,
  **`return f :> ISQ::force = m * a`** – kein eigenes Kompartiment „result“ mehr (F.79).
- `vehicle` mit calcs: `force : Force(m = 1500 [kg], a = 9.806 [m / s ^ 2])` (F.80) und dem Attribut `f1 = Force(…)`.

```sysml
package Calcs {
    part def FuelTank {
        attribute mass;
    }
    part def Vehicle {
        attribute mass;
        attribute dryMass;
        attribute cargoMass;
        part fuelTank : FuelTank;
    }
    part vehicle_b : Vehicle {
        attribute :>> mass = dryMass + cargoMass + fuelTank.mass;
        attribute :>> dryMass = sum(partMasses);
        attribute :>> cargoMass default 0 [kg];
        attribute partMasses = (fuelTank.mass, 100 [kg]);
    }
    calc def Force {
        in attribute m :> ISQ::mass;
        in attribute a :> ISQ::acceleration;
        return f :> ISQ::force = m * a;
    }
    part vehicle {
        calc force : Force {
            in m = 1500 [kg];
            in a = 9.806 [m / s ^ 2];
        }
        attribute f1 = Force(m = 1500 [kg], a = 9.806 [m / s ^ 2]);
    }
}
```

## 15. Binding Connection und Einheiten (F.81, F.83)
**Ansicht:** Structure und Interconnection (Kontext `vehicle`)

**Das solltest du sehen**
- `bind engine = engine4cyl`: Linie mit `=` zwischen den beiden Parts (Bindung von Parts ist jetzt erlaubt).
- `bind pwrPort = powerTrain.pwrPort`: Port des Parts an den Port des inneren Parts gebunden (F.81, zweiter Punkt).
- `m : MassValue = 25 [kg]` – Wert mit Einheit (F.83). `bind y = x` zwischen Attributen bleibt eine Bindung
  (Attribute sind keine Knoten, deshalb ohne Linie).

```sysml
package Binding {
    part def Engine;
    part def Engine4Cyl :> Engine;
    part vehicle {
        part engine : Engine;
        part engine4cyl : Engine4Cyl;
        bind engine = engine4cyl;
        attribute m : MassValue = 25 [kg];
        attribute y;
        attribute x = 1;
        bind y = x;
        port pwrPort;
        part powerTrain {
            port pwrPort;
        }
        bind pwrPort = powerTrain.pwrPort;
    }
}
```

## 16. Constraints (F.85–87)
**Ansicht:** Structure

**Das solltest du sehen**
- `«part def» FuelTank` mit attributes `mass :> ISQ::mass`, `fuelMassMax :> ISQ::mass`, items `ref fuel : Fuel` und
  **assert constraints** `{ fuel.fuelMass <= fuelMassMax }` (F.85).
- `fuelTank : FuelTank` → `FuelTank` mit `:`; darin `fuelMassMax :>> fuelMassMax = 60 [kg]`, `^mass`,
  `ref fuel1 :>> fuel` und **`^{ fuel.fuelMass <= fuelMassMax }`** (geerbter Constraint, F.86).
- `«constraint def» StraightLineDynamics` mit **parameters** und **expression** (ohne geschweifte Klammern, F.87);
  `vehicle_b` mit dem assert constraint `sld : StraightLineDynamics` und den gebundenen Parametern.

```sysml
package Constraints {
    item def Fuel {
        attribute fuelMass;
    }
    part def FuelTank {
        attribute mass :> ISQ::mass;
        ref item fuel : Fuel {
            attribute redefines fuelMass;
        }
        attribute fuelMassMax :> ISQ::mass;
        assert constraint {
            fuel.fuelMass <= fuelMassMax
        }
    }
    part fuelTank : FuelTank {
        attribute :>> fuelMassMax = 60 [kg];
        ref item fuel1 :>> fuel;
    }
    constraint def StraightLineDynamics {
        in power :> ISQ::power;
        in mass :> ISQ::mass;
        in delta_t :> ISQ::time;
        in v_in :> ISQ::speed;
        out v_out :> ISQ::speed;
        out a_out :> ISQ::acceleration;
        a_out == power / (mass * v_in) and v_out == v_in + a_out * delta_t
    }
    part vehicle_b {
        attribute mass = 1500 [kg];
        attribute speed;
        assert constraint sld : StraightLineDynamics {
            in power = 100 [W];
            in mass = vehicle_b.mass;
            in delta_t = 0.1 [s];
            in v_in = vehicle_b.speed;
        }
    }
}
```

## 17. Requirements (F.90–95)
**Ansicht:** Structure

**Das solltest du sehen**
- `«requirement def» <'1_1'> MassRequirement` mit **doc**, attributes und **constraints**:
  `require { massActual <= massRequired }`, `assume { massFluid <= 40 [kg] }` (ein Kompartiment, F.90).
- `vehicleSpecification` mit **subject** `v : Vehicle`, darunter `<'1'> massReqt : MassRequirement` mit
  `^require {…}` und `^assume {…}` (F.92).
- `vehicle_b` → `vehicleSpecification`: gestrichelter Pfeil `«satisfy»`.

**Nur Text:** `#derivation connection { end #original ::> …; end #derive ::> …; }` (F.95) wird verlustfrei als
Textknoten gezeigt.

```sysml
package Requirements {
    part def Vehicle {
        attribute mass;
        attribute fluidMass;
    }
    requirement def <'1_1'> MassRequirement {
        doc /* The actual mass shall be less than the required mass. */
        attribute massRequired;
        attribute massActual;
        attribute massFluid;
        require constraint {
            massActual <= massRequired
        }
        assume constraint {
            massFluid <= 40 [kg]
        }
    }
    requirement vehicleSpecification {
        subject v : Vehicle;
        requirement <'1'> massReqt : MassRequirement;
    }
    part vehicle_b : Vehicle {
        attribute :>> mass = 1500 [kg];
        satisfy vehicleSpecification;
    }
    #derivation connection {
        end #original ::> vehicleSpecification.massReqt;
        end #derive ::> engineSpecification;
    }
    requirement engineSpecification;
}
```

## 18. Use Case, Analysis, Verification (F.97–102)
**Ansicht:** Use Cases (für den Use Case), Structure (für alles)

**Das solltest du sehen**
- `«use case def» TransportPassenger` mit objective (doc), subject `vehicle : Vehicle`, actors, include.
- `«analysis def» FuelEconomyAnalysis` mit subject und `return fuelEconomyResult : Real` in **parameters**.
- `«verification def» MassTest` mit objective `verify vehicleSpecification` und dem `«verify»`-Pfeil.
- `verificationSystem` mit perform actions `massVerificationTest`.

```sysml
package Cases {
    part def Vehicle;
    part def Driver;
    part def Passenger;
    use case def TransportPassenger {
        objective {
            doc /* Deliver passenger to destination safely */
        }
        subject vehicle : Vehicle;
        actor driver : Driver;
        actor passengers : Passenger[0..4];
        include use case getInVehicle;
        include use case getOutOfVehicle;
    }
    use case transportPassenger : TransportPassenger;
    analysis def FuelEconomyAnalysis {
        subject vehicle : Vehicle;
        return fuelEconomyResult : Real;
    }
    analysis fuelEconomyAnalysis : FuelEconomyAnalysis;
    requirement vehicleSpecification;
    verification def MassTest {
        subject testVehicle : Vehicle;
        objective {
            verify vehicleSpecification;
        }
    }
    part verificationSystem {
        perform massVerificationTest;
    }
    verification massVerificationTest : MassTest;
}
```

## 19. Dependency und Allocation (F.104–106)
**Ansicht:** Structure

**Das solltest du sehen**
- `«item» marketSurvey` (Items auf Paketebene sind jetzt Knoten) und der gestrichelte Abhängigkeitspfeil
  von `vehicleSpecification` (F.104).
- `torqueGenerator` → `powerTrain`: `«allocate»` (offene Spitze), mit der Unter-Allokation
  `generateTorque → engine.generateTorque` als Detail am Pfeil (F.105).

**Nicht umgesetzt:** Cause-Effect (F.106, `#cause`/`#effect`-Metadaten an Verbindungen) – bleibt als Text erhalten.

```sysml
package Relationships {
    item marketSurvey;
    requirement vehicleSpecification;
    dependency from vehicleSpecification to marketSurvey;
    part torqueGenerator {
        perform action generateTorque;
    }
    part powerTrain {
        part engine {
            perform action generateTorque;
        }
    }
    allocate torqueGenerator to powerTrain {
        allocate torqueGenerator.generateTorque to powerTrain.engine.generateTorque;
    }
}
```

## 20. Annotationen, Filter, Views (F.108–116)
**Ansicht:** Structure

**Das solltest du sehen**
- `«metadata def» Safety`, in `seatBelt` und `bumper` das Kompartiment metadata `@Safety {isMandatory = …}`.
- doc-Kompartiment an `vehicle`; der Kommentar bleibt im Text (wird nicht gezeichnet).
- `SafetyFeatures` mit `«import» **`-Pfeil und dem Filter `@Safety` (F.111).
- `«view def» PartsTreeView` und `«view» vehiclePartsTree : PartsTreeView` mit expose `vehicle::**` (F.113–116).

```sysml
package Annotations {
    private import ScalarValues::*;
    metadata def Safety {
        attribute isMandatory : Boolean;
    }
    part vehicle {
        part seatBelt {
            @Safety {
                isMandatory = true;
            }
        }
        part bumper {
            @Safety {
                isMandatory = false;
            }
        }
        doc /* A vehicle with safety features */
        comment /* Dies ist ein Kommentar */
    }
    package SafetyFeatures {
        private import vehicle::**;
        filter @Safety;
    }
    view def PartsTreeView;
    view vehiclePartsTree : PartsTreeView {
        expose vehicle::**;
    }
}
```

## Was in dieser Runde geändert wurde (deine Liste)

| Punkt | Ursache | Änderung |
|---|---|---|
| «import» fehlt auf Pfeilen | Imports waren eigene Knoten | Import zwischen gezeigten Paketen = gestrichelter Pfeil `«import» *` / `«import» **` / `«import»`; Bibliotheksimport = gestricheltes Paket |
| F.26 `^` | geerbte Merkmale wurden nicht gesucht | Geerbte Merkmale (aus Definition, Spezialisierung, Subsetting, Redefinition) erscheinen automatisch mit `^`; redefinierte nicht |
| subset | `subsets` wurde nicht gelesen | `subsets` wird gelesen und so geschrieben, wie es dasteht; Anzeige mit dem Original-Schlüsselwort |
| F.34 `:` am Pfeil | „defined by“ war kein Pfeil | Neuer Pfeil „defined by“: hohles Dreieck mit `:`; ziehbar (setzt den Typ), löschbar |
| F.40 | Interconnection View fehlte | Neuer Reiter **Interconnection** |
| F.52 `::` | „perform“ war kein Pfeil | Pfeil mit offener Spitze und `::`; ziehbar (schreibt `perform a.b;`) |
| F.56–64 entry/do/exit | `entry action1;` wurde als „perform action1“ gezeigt | Kompartiment **state actions** (`entry action1`); Successions gestrichelt mit offener Spitze; Rahmen; «action»/«state» |
| F.65 parallel | verschachtelte States wurden nicht gezeichnet | States in States, parallel nebeneinander, mitbewegt |
| F.69 / Sequenz-Operationen | `«perform»` fehlte, Label „of T“ | `«perform»`-Kasten auf der Lebenslinie, `vs : VehicleStart`, `then` auf einer Lebenslinie ohne Pfeil |
| F.76 redefine | Redefinition war kein Pfeil | hohles Dreieck mit Querstrich, Titel `leftFront :>> leftFront` |
| F.78 / F.82 / F.86 | Beispiele fehlten | Abschnitte 14, 15, 16 |
| F.79 result | eigenes Kompartiment „result“ | `return …` gehört zu **parameters** |
| F.80 | Werte der calc-Usage fehlten | `force : Force(m = 1500 [kg], a = …)` |
| F.81 | Bindungen nur zwischen Attributen/Ports | Bindungen auch zwischen Parts/Items; Linie mit `=` |
| F.85 | `assert constraint` stand unter „constraints“ mit „assert“ davor | Kompartiment **assert constraints** `{…}`, geerbt `^{…}` |
| Pfeile allgemein | | Fluss: gefülltes Dreieck in der Mitte, `: Fuel`; Interface ohne Ball/Socket, `: Typ`; Bindung `=`; Komposition/Referenz/Timeslice als rechtwinkliger Baum; ref gestrichelt |
| weitere | | Port def „directed features“, Action def „parameters“, enum „enums“ und `enum = 60 [mm]`, Requirement „constraints“ mit `require`/`assume`, Items auf Paketebene, `succession flow`, Flüsse an geerbte Merkmale (`fuelTank.p1.fuel`) und zwischen Aktionsparametern |

## Bekannte Lücken (bleiben verlustfrei als Text, werden aber nicht gezeichnet)

- Parameter-Pins an Aktionen und Swimlanes im Action Flow (F.56).
- Anonyme redefinierte Aktionen mit Form, z. B. `action :>> trigger1 accept vs : VehicleStart via vehicle.p1;` (F.62).
- `event startVehicle.driverReady;` (Ereignis als Referenz, F.62).
- Kurzform `interface driver.p1 to vehicle.p1;` ohne `connect` (F.62) – mit `interface connect … to …;` wird es gezeichnet.
- `#derivation connection { end #original …; }` (F.95) und Cause-Effect (F.106).
- `perform action action1` als eigener Knoten mit hohler Raute (F.53) – wird als Eintrag „perform actions“ gezeigt.
- Das Layout der Interconnection-Ansicht wird (noch) nicht im Dokument gespeichert; es wird automatisch angeordnet.

## Recherche: Wie sehen Bindings aus? (F.81)

Folie 81 hat kein Bild, nur den Text: das Symbol ist **`«bind»` oder `=`**. In der Präsentation sind Bindungen an
mehreren Stellen gezeichnet:

- **F.37 (References):** `vehicle.hitchBall` ↔ `trailerHitch.hitchBall` – eine **durchgezogene Linie ohne Pfeilspitze
  mit `=`** daneben.
- **F.40 (Interconnection):** Port am Rand von `vehicle_b` an den Port eines inneren Parts (`=` an der Linie) und die
  Räder an die Achse.
- **F.56/57 (Action Flow):** Parameter der Aktion `providePower` an die Parameter der inneren Aktionen (`=` an der Linie).
- **F.102 (Verification):** `v : Vehicle` an das Subjekt, `verdict` an das Ergebnis.

Laut SysML-v2-Spezifikation (Graphical Notation, „binding connection“) ist die Bindung eine Verbindung ohne
Richtung: durchgezogene Linie, ohne Spitzen, mit der Beschriftung `=` (alternativ «bind»). So zeichnet
StructuraSystems sie jetzt: in Structure und Interconnection als Linie mit `=`, im Text `bind a = b;`.
