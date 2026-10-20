import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import StructuraSystems

// Asks for confirmation before the content of a document with unsaved changes is replaced (branch switch, commit checkout).
Dialog {
    id: dialog

    property string message
    property string confirmText: qsTr("Discard changes")
    /** Function that is called when the user confirms. */
    property var action: null

    function openFor(dialogTitle, text, buttonText, callback) {
        title = dialogTitle
        message = text
        confirmText = buttonText
        action = callback
        open()
    }

    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay
    modal: true
    footer: DialogFooter {
        Button {
            text: qsTr("Cancel")
            flat: true
            onClicked: dialog.reject()
        }
        Button {
            text: dialog.confirmText
            highlighted: true
            Material.accent: Theme.dangerFill
            onClicked: dialog.accept()
        }
    }

    contentItem: Item {
        implicitWidth: 360
        implicitHeight: label.implicitHeight

        Label {
            id: label
            width: parent.width
            wrapMode: Text.Wrap
            text: dialog.message
        }
    }

    onAccepted: {
        const callback = action
        action = null
        if (callback)
            callback()
    }
    onRejected: action = null
}
