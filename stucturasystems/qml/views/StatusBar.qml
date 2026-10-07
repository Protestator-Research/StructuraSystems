import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import StructuraSystems

// Thin bar at the bottom of the window.
Pane {
    id: bar

    property int elementCount: -1

    implicitHeight: 28
    padding: 0
    leftPadding: 12
    rightPadding: 12
    background: Rectangle {
        color: Theme.rail
        Rectangle {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: Theme.outline
        }
    }

    contentItem: RowLayout {
        spacing: 16

        Label {
            Layout.fillWidth: true
            text: AppController.statusText
            elide: Text.ElideRight
            font.pixelSize: 12
            color: Theme.textSecondary
        }
        ProgressBar {
            Layout.preferredWidth: 120
            Layout.preferredHeight: 4
            visible: AppController.busy
            indeterminate: true
        }
        Label {
            visible: bar.elementCount >= 0
            text: bar.elementCount === 1 ? qsTr("1 element") : qsTr("%1 elements").arg(bar.elementCount)
            font.pixelSize: 12
            color: Theme.textSecondary
        }
        Row {
            spacing: 6
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 8
                height: 8
                radius: 4
                color: AppController.connected ? Theme.success : Theme.textSecondary
            }
            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: AppController.connected ? qsTr("Connected") : qsTr("Offline")
                font.pixelSize: 12
                color: Theme.textSecondary
            }
        }
    }
}
