import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import StructuraSystems

// Edits one element of the diagram: name, id (short name), type, multiplicity, value and prefixes (e.g. "derived
// #measurable"), for actions the perform style, for constraints and calculations the expression (and assert /
// require / assume), for requirements, use cases and verifications their text (doc); other statements kept as text
// are edited as text.
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
            visible: dialog.details.isTextual !== true
            text: qsTr("Name")
            color: Theme.textSecondary
        }
        TextField {
            id: nameField
            visible: dialog.details.isTextual !== true
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
