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
    standardButtons: Dialog.Cancel | Dialog.Discard
    Component.onCompleted: standardButton(Dialog.Discard).text = qsTr("Close without saving")

    Label {
        width: 340
        wrapMode: Text.Wrap
        text: qsTr("\"%1\" has changes that were not saved. They will be lost if you close it.").arg(dialog.documentTitle)
    }

    onDiscarded: {
        AppController.closeDocument(documentIndex)
        close()
    }
}
