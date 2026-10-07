import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import StructuraSystems

// Material navigation rail: destinations at the top, settings at the bottom.
Pane {
    id: control

    /** Index of the selected destination: 0 explorer, 1 online, 2 digital twins. */
    property int currentIndex: 0
    /** True while the side panel next to the rail shows the selected destination. */
    property bool panelOpen: true

    signal destinationClicked(int index)
    signal settingsClicked()

    implicitWidth: Theme.railWidth
    padding: 0
    topPadding: 8
    bottomPadding: 8
    Material.background: Theme.rail

    ColumnLayout {
        anchors.fill: parent
        spacing: 4

        NavRailItem {
            Layout.fillWidth: true
            text: qsTr("Explorer")
            iconSource: Theme.iconFolder
            selected: control.panelOpen && control.currentIndex === 0
            onClicked: control.destinationClicked(0)
        }
        NavRailItem {
            Layout.fillWidth: true
            text: qsTr("Online")
            iconSource: Theme.iconCloud
            selected: control.panelOpen && control.currentIndex === 1
            onClicked: control.destinationClicked(1)
        }
        NavRailItem {
            Layout.fillWidth: true
            text: qsTr("Twins")
            iconSource: Theme.iconTwin
            selected: control.panelOpen && control.currentIndex === 2
            onClicked: control.destinationClicked(2)
        }
        Item { Layout.fillHeight: true }
        NavRailItem {
            Layout.fillWidth: true
            text: qsTr("Settings")
            iconSource: Theme.iconSettings
            onClicked: control.settingsClicked()
        }
    }

    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: Theme.outline
    }
}
