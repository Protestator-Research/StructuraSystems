import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import StructuraSystems

// Read-only diagram of one SysMLv2/KerML block, shown inside its element card (collapsed by default).
Item {
    id: preview

    property string source
    property string language: "SysMLv2"

    /** "Open in diagram editor" was clicked. */
    signal openRequested

    implicitHeight: 340

    DiagramPreviewModel {
        id: previewModel
        source: preview.source
        language: preview.language
        onUpdated: fitTimer.restart()
    }

    // The layout is repeated once the real node sizes are known: fit again afterwards.
    Timer {
        id: fitTimer
        interval: 60
        onTriggered: canvas.fitToContent()
    }
    Connections {
        target: previewModel.controller.nodes
        function onGeometryChanged() { fitTimer.restart() }
    }
    onWidthChanged: fitTimer.restart()

    ColumnLayout {
        anchors.fill: parent
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                Layout.fillWidth: true
                elide: Text.ElideRight
                font.pixelSize: 12
                color: Theme.textSecondary
                text: previewModel.elementCount === 0
                      ? qsTr("Nothing in this block can be shown as a diagram (yet).")
                      : previewModel.hiddenCount > 0
                        ? qsTr("Not shown: %1").arg(previewModel.hiddenSummary)
                        : qsTr("Everything in this block is shown.")

                HoverHandler {
                    id: summaryHover
                }
                ToolTip.visible: summaryHover.hovered && previewModel.hiddenCount > 0
                ToolTip.text: previewModel.hiddenDetails
                ToolTip.delay: 500
            }
            Button {
                flat: true
                text: qsTr("Open in diagram editor")
                icon.source: Theme.iconDiagram
                icon.color: "transparent"
                icon.width: 18
                icon.height: 18
                onClicked: preview.openRequested()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 8
            color: "transparent"
            border.width: 1
            border.color: Theme.outline
            clip: true

            DiagramCanvas {
                id: canvas
                anchors.fill: parent
                anchors.margins: 1
                readOnly: true
                controller: previewModel.controller
            }
        }
    }
}
