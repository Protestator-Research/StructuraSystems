import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import StructuraSystems

// Name and type of a connection, interface, flow, message or allocation ("connection pipe : Hose connect ...",
// "flow of Fuel from ...", "message m of SetSpeed from ..."), the guard / trigger / effect of a succession or transition.
Dialog {
    id: dialog

    property DiagramController controller
    property string edgeKey
    property var details: ({})

    function openFor(key) {
        edgeKey = key
        details = controller.edgeDetails(key)
        if (details.editable !== true)
            return
        nameField.text = details.name
        typeField.text = details.typeName
        guardField.text = details.guard !== undefined ? details.guard : ""
        triggerField.text = details.trigger !== undefined ? details.trigger : ""
        effectField.text = details.effect !== undefined ? details.effect : ""
        sequencedBox.checked = details.sequenced === true
        definitionField.text = details.definition !== undefined ? details.definition : ""
        moreSourcesField.text = details.moreSources !== undefined ? details.moreSources : ""
        moreTargetsField.text = details.moreTargets !== undefined ? details.moreTargets : ""
        sourceMultiplicityField.text = details.sourceMultiplicity !== undefined ? details.sourceMultiplicity : ""
        targetMultiplicityField.text = details.targetMultiplicity !== undefined ? details.targetMultiplicity : ""
        errorLabel.text = ""
        open()
        nameField.forceActiveFocus()
    }

    function apply() {
        let error = controller.updateEdge(edgeKey, { name: nameField.text, typeName: typeField.text,
                                                       guard: guardField.text, trigger: triggerField.text,
                                                       effect: effectField.text, sequenced: sequencedBox.checked,
                                                       definition: definitionField.text })
        if (error.length === 0 && details.hasEnds === true) {
            const values = { name: nameField.text, typeName: typeField.text, moreTargets: moreTargetsField.text }
            if (details.hasClients === true)
                values.moreSources = moreSourcesField.text
            const more = controller.updateEdge(edgeKey, values)
            if (more.length > 0)
                error = more
        }
        if (error.length === 0 && details.hasMultiplicities === true) {
            const more = controller.updateEdge(edgeKey, { name: nameField.text, typeName: typeField.text,
                                                          sourceMultiplicity: sourceMultiplicityField.text,
                                                          targetMultiplicity: targetMultiplicityField.text })
            if (more.length > 0)
                error = more
        }
        if (error.length > 0)
            errorLabel.text = error
        else
            close()
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    width: 420
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

        Label {
            text: qsTr("Name")
            color: Theme.textSecondary
        }
        TextField {
            id: nameField
            Layout.fillWidth: true
            placeholderText: qsTr("optional")
            onAccepted: dialog.apply()
        }
        // "flow : FuelFlow of Fuel from ...": the flow definition
        Label {
            visible: dialog.details.hasDefinition === true
            text: qsTr("Definition")
            color: Theme.textSecondary
        }
        TextField {
            id: definitionField
            objectName: "flowDefinitionField"
            visible: dialog.details.hasDefinition === true
            Layout.fillWidth: true
            placeholderText: qsTr("optional, e.g. FuelFlow")
            onAccepted: dialog.apply()
        }
        Label {
            visible: dialog.details.hasType !== false
            text: dialog.details.typeLabel !== undefined ? dialog.details.typeLabel : qsTr("Type")
            color: Theme.textSecondary
        }
        TextField {
            id: typeField
            visible: dialog.details.hasType !== false
            Layout.fillWidth: true
            placeholderText: dialog.details.typePlaceholder !== undefined ? dialog.details.typePlaceholder : ""
            onAccepted: dialog.apply()
        }
        // transition: accept trigger, if guard, do effect; succession: if guard ("else" for the else branch)
        Label {
            visible: dialog.details.hasTrigger === true
            text: qsTr("Trigger (accept)")
            color: Theme.textSecondary
        }
        TextField {
            id: triggerField
            visible: dialog.details.hasTrigger === true
            Layout.fillWidth: true
            placeholderText: "StartSignal"
            onAccepted: dialog.apply()
        }
        Label {
            visible: dialog.details.hasGuard === true
            text: qsTr("Guard (if)")
            color: Theme.textSecondary
        }
        TextField {
            id: guardField
            visible: dialog.details.hasGuard === true
            Layout.fillWidth: true
            placeholderText: dialog.details.hasTrigger === true ? "fuelLevel > 0" : qsTr("x > 0, or else")
            font.family: Theme.monoFamily
            onAccepted: dialog.apply()
        }
        Label {
            visible: dialog.details.hasTrigger === true
            text: qsTr("Effect (do)")
            color: Theme.textSecondary
        }
        TextField {
            id: effectField
            visible: dialog.details.hasTrigger === true
            Layout.fillWidth: true
            placeholderText: "action ignite"
            onAccepted: dialog.apply()
        }

        // n-ary: "connect (a, b, c)", "dependency a, b to c, d"
        Label {
            visible: dialog.details.hasClients === true
            text: qsTr("More clients")
            color: Theme.textSecondary
        }
        TextField {
            id: moreSourcesField
            visible: dialog.details.hasClients === true
            Layout.fillWidth: true
            placeholderText: qsTr("optional: names, separated by commas")
            onAccepted: dialog.apply()
        }
        Label {
            visible: dialog.details.hasEnds === true
            text: dialog.details.hasClients === true ? qsTr("More suppliers") : qsTr("More ends")
            color: Theme.textSecondary
        }
        TextField {
            id: moreTargetsField
            objectName: "moreEndsField"
            visible: dialog.details.hasEnds === true
            Layout.fillWidth: true
            placeholderText: qsTr("optional: names, separated by commas")
            onAccepted: dialog.apply()
        }
        // "connect [0..1] a to [1] b"
        Label {
            visible: dialog.details.hasMultiplicities === true
            text: qsTr("Multiplicity (source / target)")
            color: Theme.textSecondary
        }
        RowLayout {
            visible: dialog.details.hasMultiplicities === true
            Layout.fillWidth: true
            TextField {
                id: sourceMultiplicityField
                Layout.fillWidth: true
                placeholderText: "0..1"
                onAccepted: dialog.apply()
            }
            TextField {
                id: targetMultiplicityField
                Layout.fillWidth: true
                placeholderText: "1..*"
                onAccepted: dialog.apply()
            }
        }

        // message: "then message ..." - after the message written before it
        CheckBox {
            id: sequencedBox
            objectName: "sequencedBox"
            visible: dialog.details.hasSequence === true
            Layout.columnSpan: 2
            text: qsTr("then - after the message before it (\"then message\")")
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
