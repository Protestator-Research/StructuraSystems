import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import StructuraSystems

// Side panel "Digital Twins".
Pane {
    id: pane

    readonly property bool available: AppController.connected && AppController.currentDocument !== null
                                      && AppController.currentDocument.isOnline

    signal createRequested()

    padding: 0
    Material.background: Theme.panel

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Label {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.topMargin: 14
            text: qsTr("Digital twins")
            font.pixelSize: 18
            font.weight: Font.Medium
            color: Theme.textPrimary
        }

        EmptyState {
            Layout.fillWidth: true
            Layout.fillHeight: true
            iconSource: Theme.iconTwin
            iconSize: 56
            title: qsTr("Mirror a model as digital twin")
            text: qsTr("Select elements of an online project and create a digital twin on the server. "
                       + "A wizard guides you through naming and selecting the elements.")

            Button {
                text: qsTr("Create digital twin")
                highlighted: true
                enabled: pane.available && !AppController.busy
                icon.source: Theme.iconTwin
                icon.color: "transparent"
                onClicked: pane.createRequested()
            }
            Label {
                width: 280
                visible: !pane.available
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                font.pixelSize: 12
                color: Theme.textSecondary
                text: AppController.connected ? qsTr("Open an online project to create a digital twin from it.")
                                              : qsTr("Connect to the server and open an online project first.")
            }
        }
    }
}
