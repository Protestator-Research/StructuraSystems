import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import StructuraSystems

// Commit message input for the current online document.
Dialog {
    id: dialog

    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay
    modal: true
    title: qsTr("Commit changes")
    width: 460

    onAboutToShow: message.text = ""
    onOpened: message.forceActiveFocus()

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        Label {
            Layout.fillWidth: true
            text: qsTr("Describe what you changed. The message is stored with the commit on the server.")
            wrapMode: Text.Wrap
            color: Theme.textSecondary
        }
        TextArea {
            id: message
            Layout.fillWidth: true
            Layout.preferredHeight: 110
            placeholderText: qsTr("Commit message")
            wrapMode: TextEdit.Wrap
            selectByMouse: true
            Keys.onPressed: event => {
                if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                        && (event.modifiers & Qt.ControlModifier) && commitButton.enabled) {
                    commitButton.clicked()
                    event.accepted = true
                }
            }
        }
    }

    footer: DialogButtonBox {
        Button {
            text: qsTr("Cancel")
            flat: true
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
        Button {
            id: commitButton
            text: qsTr("Commit")
            highlighted: true
            enabled: message.text.trim().length > 0
            icon.source: Theme.iconCommit
            icon.color: "transparent"
            icon.width: 18
            icon.height: 18
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
        }
    }

    onAccepted: AppController.commitCurrent(message.text.trim())
}
