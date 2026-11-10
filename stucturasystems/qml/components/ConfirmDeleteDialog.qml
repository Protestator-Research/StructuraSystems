import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import StructuraSystems

// Confirmation for deleting a project (and, with openForUsage, for removing a project usage). A local project is moved to the recycle bin, an online project is deleted on
// the server for good: the user has to type its name first.
Dialog {
    id: dialog

    /** True for a project on the server, false for a local project file. */
    property bool online: false
    /** Row of the project in the SOURCE model, reported by confirmed. */
    property int sourceRow: -1
    property string projectName
    /** Set by openForUsage: another title, text and button; nothing has to be typed. projectName then holds the id of the usage. */
    property string customTitle
    property string customMessage
    property string confirmText

    signal confirmed(int sourceRow, string projectName)

    function openFor(row, name) {
        customTitle = ""
        customMessage = ""
        confirmText = ""
        sourceRow = row
        projectName = name
        nameField.text = ""
        open()
    }

    /** Asks before a project usage is removed; confirmed reports (-1, id of the usage). */
    function openForUsage(usageId, usedProjectName, documentTitle) {
        customTitle = qsTr("Remove project usage?")
        customMessage = qsTr("Remove the usage of \"%1\" from \"%2\"? This creates a commit on the server. The used project itself is not changed.")
                .arg(usedProjectName).arg(documentTitle)
        confirmText = qsTr("Remove")
        sourceRow = -1
        projectName = usageId
        nameField.text = ""
        open()
    }

    readonly property bool _canDelete: !online || nameField.text === projectName

    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay
    modal: true
    width: 420
    title: customTitle.length > 0 ? customTitle : (online ? qsTr("Delete online project?") : qsTr("Delete project?"))
    onOpened: if (online) nameField.forceActiveFocus()
    onAccepted: confirmed(sourceRow, projectName)

    footer: DialogFooter {
        Button {
            text: qsTr("Cancel")
            flat: true
            onClicked: dialog.reject()
        }
        Button {
            text: dialog.confirmText.length > 0 ? dialog.confirmText : qsTr("Delete")
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
            text: dialog.customMessage.length > 0 ? dialog.customMessage : dialog.online
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
