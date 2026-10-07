import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import StructuraSystems

ApplicationWindow {
    id: root

    width: 1280
    height: 800
    visible: true
    title: qsTr("Structura Systems")

    Material.theme: Material.System

    // Placeholder: proves that the ViewModels are reachable. The real UI follows in a later phase.
    Label {
        anchors.centerIn: parent
        text: AppController.statusText
    }
}
