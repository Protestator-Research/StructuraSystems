import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import StructuraSystems

// Name, type and multiplicity of an actor or of the subject (system boundary) of the use case view. SysML declares
// them in every use case they take part in; the dialog changes all those declarations at once.
Dialog {
    id: dialog

    property UseCaseController controller
    property string nodeId
    property var details: ({})

    function openFor(id) {
        details = controller.groupDetails(id)
        if (details.name === undefined)
            return
        nodeId = details.node
        nameField.text = details.name
        typeField.text = details.typeName
        multiplicityField.text = details.multiplicity
        errorLabel.text = ""
        open()
        nameField.forceActiveFocus()
        nameField.selectAll()
    }

    function apply() {
        const error = controller.updateGroup(nodeId, { name: nameField.text, typeName: typeField.text,
                                                       multiplicity: multiplicityField.text })
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
            objectName: "groupNameField"
            Layout.fillWidth: true
            onAccepted: dialog.apply()
        }
        Label {
            text: qsTr("Type")
            color: Theme.textSecondary
        }
        TextField {
            id: typeField
            objectName: "groupTypeField"
            Layout.fillWidth: true
            placeholderText: dialog.details.typePlaceholder !== undefined ? dialog.details.typePlaceholder : ""
            onAccepted: dialog.apply()
        }
        Label {
            text: qsTr("Multiplicity")
            color: Theme.textSecondary
        }
        TextField {
            id: multiplicityField
            Layout.fillWidth: true
            placeholderText: "0..4"
            onAccepted: dialog.apply()
        }
        Label {
            Layout.columnSpan: 2
            Layout.fillWidth: true
            visible: (dialog.details.count || 0) > 1
            wrapMode: Text.Wrap
            font.pixelSize: 12
            color: Theme.textSecondary
            text: qsTr("Declared in %1 use cases - all of them are changed.").arg(dialog.details.count || 0)
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
