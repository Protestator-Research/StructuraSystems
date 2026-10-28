import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import StructuraSystems

// Asks for confirmation before a document with unsaved changes is closed.
Dialog {
    id: dialog

    property int documentIndex: -1
    property string documentTitle

    function openFor(index, title) {
        documentIndex = index
        documentTitle = title
        open()
    }

    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay
    modal: true
    title: qsTr("Close without saving?")
    footer: DialogFooter {
        Button {
            text: qsTr("Cancel")
            flat: true
            onClicked: dialog.reject()
        }
        Button {
            text: qsTr("Close without saving")
            highlighted: true
            Material.accent: Theme.dangerFill
            onClicked: dialog.discard()
        }
    }

    contentItem: Item {
        implicitWidth: 340
        implicitHeight: message.implicitHeight

        Label {
            id: message
            width: parent.width
            wrapMode: Text.Wrap
            text: qsTr("\"%1\" has changes that were not saved. They will be lost if you close it.").arg(dialog.documentTitle)
        }
    }

    function discard() {
        AppController.closeDocument(documentIndex)
        close()
    }
}
