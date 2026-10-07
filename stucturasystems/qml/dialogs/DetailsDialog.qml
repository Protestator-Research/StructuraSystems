import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import StructuraSystems

// Shows the technical details of a message in selectable text.
Dialog {
    id: dialog

    property string details

    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay
    modal: true
    title: qsTr("Details")
    width: Math.min(640, (Overlay.overlay ? Overlay.overlay.width : 640) - 48)
    height: Math.min(420, (Overlay.overlay ? Overlay.overlay.height : 420) - 48)

    contentItem: ScrollView {
        TextArea {
            id: text
            readOnly: true
            selectByMouse: true
            wrapMode: TextEdit.Wrap
            text: dialog.details
            font.family: Theme.monoFamily
            font.pixelSize: 12
            background: null
        }
    }

    footer: DialogFooter {
        Button {
            text: qsTr("Copy")
            flat: true
            onClicked: {
                text.selectAll()
                text.copy()
                text.deselect()
            }
        }
        Button {
            text: qsTr("Close")
            highlighted: true
            onClicked: dialog.close()
        }
    }
}
