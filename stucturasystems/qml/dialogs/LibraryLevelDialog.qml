import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import StructuraSystems

// Sets the level of a local library by hand. The level is stored in .structurasystems/workspace.json of the opened folder.
Dialog {
    id: dialog

    property string path
    property int startLevel: 1

    /** Opens the dialog for the file (relative path in the opened folder). */
    function openFor(filePath, currentLevel) {
        path = filePath
        startLevel = currentLevel > 0 ? currentLevel : 1
        open()
    }

    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay
    modal: true
    title: qsTr("Set library level")
    width: 460

    onAboutToShow: level.value = startLevel

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        Label {
            Layout.fillWidth: true
            text: dialog.path
            elide: Text.ElideMiddle
            font.weight: Font.Medium
            color: Theme.textPrimary
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.textSecondary
            text: qsTr("A document sees the libraries of the same or a lower level. Level 0 is the standard library, "
                       + "the open document is always on top. Libraries that import other libraries should have a higher level than these.")
        }
        RowLayout {
            Layout.fillWidth: true
            Label {
                text: qsTr("Level")
                color: Theme.textPrimary
            }
            SpinBox {
                id: level
                from: 1
                to: 999
                editable: true
            }
            Item { Layout.fillWidth: true }
        }
    }

    footer: DialogFooter {
        Button {
            text: qsTr("Cancel")
            flat: true
            onClicked: dialog.reject()
        }
        Button {
            text: qsTr("Set level")
            highlighted: true
            onClicked: dialog.accept()
        }
    }

    onAccepted: AppController.libraries.setLevel(path, level.value)
}
