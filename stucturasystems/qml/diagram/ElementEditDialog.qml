import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import StructuraSystems

// Edits one element of the diagram: name, id (short name), type, multiplicity, value and prefixes (e.g. "derived
// #measurable"), for actions the perform style, for constraints and calculations the expression (and assert /
// require / assume), for requirements, use cases and verifications their text (doc); other statements kept as text
// are edited as text. Usages may redefine inherited features (":>> eng", "part redefines cyl[4];"), actions may
// send, accept, assign, terminate or be an if / while / loop / for, states may be parallel.
Dialog {
    id: dialog

    property DiagramController controller
    property string elementId
    property var details: ({})

    function openFor(id) {
        elementId = id
        details = controller.elementDetails(id)
        if (details.name === undefined)
            return
        nameField.text = details.name
        typeField.text = details.typeName
        multiplicityField.text = details.multiplicity
        valueField.text = details.value
        prefixField.text = details.prefixes
        referenceCheck.checked = details.referenceAction
        roleBox.currentIndex = Math.max(0, ["entry", "do", "exit", "action"].indexOf(details.role))
        textArea.text = details.text
        expressionArea.text = details.expression
        assertCheck.checked = details.assertion === true
        shortNameField.text = details.shortName !== undefined ? details.shortName : ""
        docArea.text = details.doc !== undefined ? details.doc : ""
        constraintRoleBox.currentIndex = Math.max(0, constraintRoleBox.roles.indexOf(details.constraintRole))
        redefinesField.text = details.redefines !== undefined ? details.redefines : ""
        redefinitionKeywordBox.currentIndex = details.redefinitionKeyword === "redefines" ? 1 : 0
        nameFromRedefinitionCheck.checked = details.nameFromRedefinition === true
        formBox.currentIndex = Math.max(0, formBox.forms.indexOf(details.actionForm))
        clauseField.text = details.actionClause !== undefined ? details.actionClause : ""
        untilField.text = details.until !== undefined ? details.until : ""
        parallelCheck.checked = details.parallel === true
        targetField.text = details.target !== undefined ? details.target : ""
        aboutField.text = details.about !== undefined ? details.about : ""
        errorLabel.text = ""
        open()
        if (details.isTextual) {
            textArea.forceActiveFocus()
        } else {
            nameField.forceActiveFocus()
            nameField.selectAll()
        }
    }

    function apply() {
        let values = {
            name: nameField.text,
            typeName: typeField.text,
            multiplicity: multiplicityField.text,
            value: valueField.text,
            prefixes: prefixField.text,
            referenceAction: referenceCheck.checked
        }
        if (details.hasExpression)
            values.expression = expressionArea.text
        if (details.isConstraintUsage && details.inRequirement === true)
            values.constraintRole = constraintRoleBox.roles[constraintRoleBox.currentIndex]
        else if (details.isConstraintUsage)
            values.assertion = assertCheck.checked
        if (details.hasShortName)
            values.shortName = shortNameField.text
        if (details.hasDoc)
            values.doc = docArea.text
        if (details.stateAction)
            values.role = ["entry", "do", "exit", "action"][roleBox.currentIndex]
        if (details.hasRedefinitions) {
            values.redefines = redefinesField.text
            values.redefinitionKeyword = redefinitionKeywordBox.currentText
            values.nameFromRedefinition = nameFromRedefinitionCheck.checked
        }
        if (details.isActionUsage) {
            values.actionForm = formBox.forms[formBox.currentIndex]
            values.actionClause = clauseField.text
            values.until = untilField.text
        }
        if (details.isState)
            values.parallel = parallelCheck.checked
        if (details.hasTarget)
            values.target = targetField.text
        if (details.hasAbout)
            values.about = aboutField.text
        const error = details.isTextual
                      ? controller.updateElement(elementId, { text: textArea.text })
                      : controller.updateElement(elementId, values)
        if (error.length > 0)
            errorLabel.text = error
        else
            close()
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    width: details.isTextual === true || details.hasExpression === true || details.hasDoc === true ? 620 : 460
    title: qsTr("Edit %1").arg(details.kindLabel !== undefined ? details.kindLabel : "")

    footer: DialogFooter {
        Button {
            text: qsTr("Cancel")
            flat: true
            onClicked: dialog.reject()
        }
        Button {
            text: qsTr("Apply")
            highlighted: true
            onClicked: dialog.apply()
        }
    }

    GridLayout {
        width: dialog.availableWidth
        columns: 2
        columnSpacing: 12
        rowSpacing: 8

        // Statement kept as text (constraint, function, ...)
        ScrollView {
            Layout.columnSpan: 2
            Layout.fillWidth: true
            Layout.preferredHeight: 260
            visible: dialog.details.isTextual === true

            TextArea {
                id: textArea
                font.family: Theme.monoFamily
                font.pixelSize: 13
                wrapMode: TextEdit.NoWrap
                selectByMouse: true
                Accessible.name: qsTr("SysML text")
            }
        }

        Label {
            visible: dialog.details.isTextual !== true && dialog.details.anonymous !== true
            text: qsTr("Name")
            color: Theme.textSecondary
        }
        TextField {
            id: nameField
            visible: dialog.details.isTextual !== true && dialog.details.anonymous !== true
            enabled: !nameFromRedefinitionCheck.checked
            Layout.fillWidth: true
            placeholderText: dialog.details.mayBeUnnamed === true ? qsTr("optional") : ""
            onAccepted: dialog.apply()
            Accessible.name: qsTr("Name")
        }

        // short name: requirement <'1.1'> vehicleMass
        Label {
            visible: dialog.details.hasShortName === true
            text: qsTr("Id")
            color: Theme.textSecondary
        }
        TextField {
            id: shortNameField
            objectName: "shortNameField"
            visible: dialog.details.hasShortName === true
            Layout.fillWidth: true
            placeholderText: qsTr("optional, e.g. R1 or 1.2")
            onAccepted: dialog.apply()
            Accessible.name: qsTr("Id (short name)")
        }

        Label {
            visible: dialog.details.typed === true
            text: qsTr("Type")
            color: Theme.textSecondary
        }
        TextField {
            id: typeField
            visible: dialog.details.typed === true
            Layout.fillWidth: true
            placeholderText: dialog.details.typePlaceholder !== undefined ? dialog.details.typePlaceholder : ""
            onAccepted: dialog.apply()
            Accessible.name: qsTr("Type")
        }

        Label {
            visible: dialog.details.hasMultiplicity === true
            text: qsTr("Multiplicity")
            color: Theme.textSecondary
        }
        TextField {
            id: multiplicityField
            visible: dialog.details.hasMultiplicity === true
            Layout.fillWidth: true
            placeholderText: "6, 0..1, 1..*"
            onAccepted: dialog.apply()
            Accessible.name: qsTr("Multiplicity")
        }

        Label {
            visible: dialog.details.hasValue === true
            text: qsTr("Value")
            color: Theme.textSecondary
        }
        TextField {
            id: valueField
            visible: dialog.details.hasValue === true
            Layout.fillWidth: true
            placeholderText: "decodeFuelTrim(VariableA)"
            font.family: Theme.monoFamily
            onAccepted: dialog.apply()
            Accessible.name: qsTr("Value")
        }

        Label {
            visible: dialog.details.isTextual !== true
            text: qsTr("Prefixes")
            color: Theme.textSecondary
        }
        TextField {
            id: prefixField
            visible: dialog.details.isTextual !== true
            Layout.fillWidth: true
            placeholderText: "derived #measurable"
            onAccepted: dialog.apply()
            Accessible.name: qsTr("Prefixes and metadata")
        }

        // import / alias / expose / render / filter: what follows the keyword
        Label {
            visible: dialog.details.hasTarget === true
            text: dialog.details.targetLabel !== undefined ? dialog.details.targetLabel : ""
            color: Theme.textSecondary
        }
        TextField {
            id: targetField
            objectName: "targetField"
            visible: dialog.details.hasTarget === true
            Layout.fillWidth: true
            font.family: Theme.monoFamily
            placeholderText: dialog.details.targetPlaceholder !== undefined ? dialog.details.targetPlaceholder : ""
            onAccepted: dialog.apply()
            Accessible.name: qsTr("Target")
        }
        // metadata: the annotated elements
        Label {
            visible: dialog.details.hasAbout === true
            text: qsTr("About")
            color: Theme.textSecondary
        }
        TextField {
            id: aboutField
            objectName: "aboutField"
            visible: dialog.details.hasAbout === true
            Layout.fillWidth: true
            placeholderText: qsTr("optional, e.g. vehicle::interior::seatBelt, vehicle::bodyAssy::bumper")
            onAccepted: dialog.apply()
            Accessible.name: qsTr("Annotated elements")
        }

        // redefinition of inherited features: "part smallEng : SmallEngine redefines eng;", ":>> mass = 2000.0;"
        Label {
            visible: dialog.details.hasRedefinitions === true
            text: qsTr("Redefines")
            color: Theme.textSecondary
        }
        RowLayout {
            visible: dialog.details.hasRedefinitions === true
            Layout.fillWidth: true
            spacing: 6

            ComboBox {
                id: redefinitionKeywordBox
                objectName: "redefinitionKeywordBox"
                Layout.preferredWidth: 130
                model: [":>>", "redefines"]
            }
            TextField {
                id: redefinesField
                objectName: "redefinesField"
                Layout.fillWidth: true
                placeholderText: qsTr("optional, e.g. eng or Vehicle::mass")
                onAccepted: dialog.apply()
                Accessible.name: qsTr("Redefined features")
            }
        }
        CheckBox {
            id: nameFromRedefinitionCheck
            objectName: "nameFromRedefinitionCheck"
            Layout.columnSpan: 2
            visible: dialog.details.hasRedefinitions === true
            enabled: redefinesField.text.trim().length > 0
            text: qsTr("No name of its own: named like the redefined feature (\"part :>> cyl[4];\")")
        }

        // what an action does: send / accept / assign / terminate, if / while / loop / for
        Label {
            visible: dialog.details.isActionUsage === true
            text: qsTr("Does")
            color: Theme.textSecondary
        }
        ComboBox {
            id: formBox

            readonly property var forms: ["action", "send", "accept", "assign", "terminate", "if", "while", "loop", "for"]

            objectName: "actionFormBox"
            visible: dialog.details.isActionUsage === true
            Layout.fillWidth: true
            model: [qsTr("nothing special (action)"), qsTr("send ... to / via ..."), qsTr("accept ... (via, at, after, when)"),
                    qsTr("assign x := value"), qsTr("terminate"), qsTr("if condition { ... }"),
                    qsTr("while condition { ... }"), qsTr("loop { ... } until condition"),
                    qsTr("for x in values { ... }")]
        }
        Label {
            visible: dialog.details.isActionUsage === true && formBox.currentIndex > 0 && formBox.currentIndex !== 7
            text: formBox.currentIndex === 5 || formBox.currentIndex === 6 ? qsTr("Condition") : qsTr("What")
            color: Theme.textSecondary
        }
        TextField {
            id: clauseField
            objectName: "actionClauseField"
            visible: dialog.details.isActionUsage === true && formBox.currentIndex > 0 && formBox.currentIndex !== 7
            Layout.fillWidth: true
            font.family: Theme.monoFamily
            placeholderText: ["", "new Show(picture) to screen", "scene : Scene via viewPort", "position := x_out",
                              qsTr("optional, e.g. processor"), "charge < 100", "n < 10", "", "power in powerProfile"]
                             [formBox.currentIndex]
            onAccepted: dialog.apply()
            Accessible.name: qsTr("What the action does")
        }
        Label {
            visible: dialog.details.isActionUsage === true && (formBox.currentIndex === 6 || formBox.currentIndex === 7)
            text: qsTr("Until")
            color: Theme.textSecondary
        }
        TextField {
            id: untilField
            objectName: "untilField"
            visible: dialog.details.isActionUsage === true && (formBox.currentIndex === 6 || formBox.currentIndex === 7)
            Layout.fillWidth: true
            font.family: Theme.monoFamily
            placeholderText: qsTr("optional, e.g. charge >= 100")
            onAccepted: dialog.apply()
            Accessible.name: qsTr("Until condition")
        }

        CheckBox {
            id: parallelCheck
            objectName: "parallelCheck"
            Layout.columnSpan: 2
            visible: dialog.details.isState === true
            text: qsTr("Parallel (its states are concurrent)")
        }

        // Expression of a constraint (condition) or calculation (result), written last in its body
        Label {
            visible: dialog.details.hasExpression === true
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: 8
            text: dialog.details.isConstraint === true ? qsTr("Constraint") : qsTr("Result")
            color: Theme.textSecondary
        }
        ScrollView {
            visible: dialog.details.hasExpression === true
            Layout.fillWidth: true
            Layout.preferredHeight: 120

            TextArea {
                id: expressionArea
                font.family: Theme.monoFamily
                font.pixelSize: 13
                wrapMode: TextEdit.NoWrap
                selectByMouse: true
                placeholderText: dialog.details.isConstraint === true ? "x >= 0.0 and x <= 255" : "raw * 100.0 / 128.0 - 100.0"
                Accessible.name: qsTr("Expression")
            }
        }
        Label {
            visible: dialog.details.hasExpression === true
            Layout.columnSpan: 2
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            font.pixelSize: 12
            color: Theme.textSecondary
            text: dialog.details.isConstraint === true
                  ? qsTr("Parameters (in x : Real) are added with the Attribute tool and edited by double-clicking them.")
                  : qsTr("Parameters and the result (return) are attributes: add them with the Attribute tool, set \"in\" or \"return\" as prefix.")
        }

        // text of a requirement / use case / verification (doc /* ... */)
        Label {
            visible: dialog.details.hasDoc === true
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: 8
            text: qsTr("Text")
            color: Theme.textSecondary
        }
        ScrollView {
            visible: dialog.details.hasDoc === true
            Layout.fillWidth: true
            Layout.preferredHeight: 110

            TextArea {
                id: docArea
                objectName: "docArea"
                font.pixelSize: 13
                wrapMode: TextEdit.Wrap
                selectByMouse: true
                placeholderText: qsTr("The vehicle shall ...")
                Accessible.name: qsTr("Text (doc)")
            }
        }

        // a constraint in a requirement: require / assume / assert / plain
        Label {
            visible: dialog.details.isConstraintUsage === true && dialog.details.inRequirement === true
            text: qsTr("Kind")
            color: Theme.textSecondary
        }
        ComboBox {
            id: constraintRoleBox

            readonly property var roles: ["require constraint", "assume constraint", "assert constraint", "constraint"]

            objectName: "constraintRoleBox"
            visible: dialog.details.isConstraintUsage === true && dialog.details.inRequirement === true
            Layout.fillWidth: true
            model: [qsTr("require (the requirement demands it)"), qsTr("assume (it may be taken as given)"),
                    qsTr("assert"), qsTr("constraint")]
        }

        // entry / do / exit action of a state
        Label {
            visible: dialog.details.stateAction === true
            text: qsTr("Role")
            color: Theme.textSecondary
        }
        ComboBox {
            id: roleBox
            visible: dialog.details.stateAction === true
            Layout.fillWidth: true
            model: [qsTr("entry action"), qsTr("do action"), qsTr("exit action"), qsTr("action (no role)")]
        }

        CheckBox {
            id: assertCheck
            Layout.columnSpan: 2
            visible: dialog.details.isConstraintUsage === true && dialog.details.inRequirement !== true
            text: qsTr("Assert (the constraint must always hold)")
        }

        CheckBox {
            id: referenceCheck
            Layout.columnSpan: 2
            visible: dialog.details.isAction === true
            text: qsTr("Reference an existing action (perform name;)")
        }

        Label {
            id: errorLabel
            Layout.columnSpan: 2
            Layout.fillWidth: true
            visible: text.length > 0
            wrapMode: Text.Wrap
            color: Theme.danger
        }
    }
}
