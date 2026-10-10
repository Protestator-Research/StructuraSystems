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
        errorLabel.text = ""
        open()
        nameField.forceActiveFocus()
    }

    function apply() {
        const error = controller.updateEdge(edgeKey, { name: nameField.text, typeName: typeField.text,
                                                       guard: guardField.text, trigger: triggerField.text,
                                                       effect: effectField.text, sequenced: sequencedBox.checked })
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
