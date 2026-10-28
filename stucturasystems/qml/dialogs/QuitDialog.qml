pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import StructuraSystems

// Asks what to do with unsaved documents before the application quits.
Dialog {
    id: dialog

    property bool hasLocal: false

    signal quitAccepted()

    function openDialog() {
        hasLocal = AppController.hasUnsavedLocalChanges()
        open()
    }

    function _finish(save) {
        if (save && !AppController.saveAllLocal())
            return // The error is reported by the controller; stay open so nothing is lost.
        close()
        quitAccepted()
        Qt.quit()
    }

    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay
    modal: true
    title: qsTr("Unsaved changes")
    width: 460

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: qsTr("These documents have changes that were not saved:")
        }

        Frame {
            Layout.fillWidth: true
            padding: 8

            ColumnLayout {
                anchors.fill: parent
                spacing: 8

                Repeater {
                    model: AppController.documents

                    RowLayout {
                        id: entry

                        required property string title
                        required property bool modified
                        required property var document

                        Layout.fillWidth: true
                        visible: modified
                        spacing: 8

                        Icon {
                            size: 18
                            source: entry.document && entry.document.isOnline ? Theme.iconOnlineProject : Theme.iconProject
                        }
                        Label {
                            Layout.fillWidth: true
                            text: entry.title
                            elide: Text.ElideRight
                            font.weight: Font.Medium
                            color: Theme.textPrimary
                        }
                        Label {
                            text: entry.document && entry.document.isOnline ? qsTr("online, needs a commit")
                                                                            : qsTr("local file")
                            font.pixelSize: 12
                            color: Theme.textSecondary
                        }
                    }
                }
            }
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            font.pixelSize: 12
            color: Theme.textSecondary
            text: qsTr("Only local files can be saved here. Changes of online projects are kept on the server "
                       + "only after a commit, so they are lost when you quit.")
        }
    }

    footer: DialogFooter {
        spread: true

        Button {
            text: qsTr("Cancel")
            flat: true
            onClicked: dialog.reject()
        }
        Item { Layout.fillWidth: true }
        Button {
            text: qsTr("Discard && quit")
            flat: true
            Material.foreground: Theme.danger
            onClicked: dialog._finish(false)
        }
        Button {
            text: qsTr("Save all && quit")
            highlighted: true
            flat: false
            visible: dialog.hasLocal
            icon.source: Theme.iconSave
            icon.color: "transparent"
            icon.width: 18
            icon.height: 18
            onClicked: dialog._finish(true)
        }
    }
}
