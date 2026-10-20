import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import StructuraSystems

// Confirmation for deleting a project. A local project is moved to the recycle bin, an online project is deleted on
// the server for good: the user has to type its name first.
Dialog {
    id: dialog

    /** True for a project on the server, false for a local project file. */
    property bool online: false
    /** Row of the project in the SOURCE model, reported by confirmed. */
    property int sourceRow: -1
    property string projectName

    signal confirmed(int sourceRow, string projectName)

    function openFor(row, name) {
        sourceRow = row
        projectName = name
        nameField.text = ""
        open()
    }

    readonly property bool _canDelete: !online || nameField.text === projectName

    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay
    modal: true
    width: 420
    title: online ? qsTr("Delete online project?") : qsTr("Delete project?")
    onOpened: if (online) nameField.forceActiveFocus()
    onAccepted: confirmed(sourceRow, projectName)

    footer: DialogFooter {
        Button {
            text: qsTr("Cancel")
            flat: true
            onClicked: dialog.reject()
        }
        Button {
            text: qsTr("Delete")
            highlighted: true
            enabled: dialog._canDelete
            Material.accent: Theme.dangerFill
            icon.source: Theme.iconTrash
            icon.color: "transparent"
            icon.width: 18
            icon.height: 18
            onClicked: dialog.accept()
        }
    }

    ColumnLayout {
        width: dialog.availableWidth
        spacing: 12

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: dialog.online
                  ? qsTr("Are you sure you want to delete the project \"%1\"? It is permanently deleted on the server for all users and cannot be undone. The open document will be closed.").arg(dialog.projectName)
                  : qsTr("Are you sure you want to delete the project? The file \"%1\" will be moved to the recycle bin and the open document will be closed. Unsaved changes are lost.").arg(dialog.projectName)
        }
        Label {
            Layout.fillWidth: true
            visible: dialog.online
            wrapMode: Text.Wrap
            text: qsTr("Type the project name to confirm:")
            color: Theme.textSecondary
        }
        TextField {
            id: nameField

            Layout.fillWidth: true
            visible: dialog.online
            placeholderText: dialog.projectName
            selectByMouse: true
            Accessible.name: qsTr("Project name")
            onAccepted: if (dialog._canDelete) dialog.accept()
        }
    }
}
