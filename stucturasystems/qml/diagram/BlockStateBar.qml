import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import StructuraSystems

// Hint above the canvas when the editor must not write into the block on its own: the generated block was
// edited by hand, a hand-written block is open, or a generated block could not be read completely.
Rectangle {
    id: bar

    /** DiagramEditorModel.BlockState */
    property int blockState: DiagramEditorModel.NoBlock
    /** What of the block the diagram does not show (DiagramEditorModel.hiddenSummary). */
    property string hiddenSummary
    property int hiddenCount: 0
    /** Every hidden construct with its line number (tooltip). */
    property string hiddenDetails
    property bool _dismissed: false
    readonly property bool shown: !_dismissed && (blockState === DiagramEditorModel.EditedByHand
                                                  || blockState === DiagramEditorModel.NotLoaded
                                                  || blockState === DiagramEditorModel.HandWritten)

    signal replaceRequested
    signal reloadRequested

    onBlockStateChanged: _dismissed = false

    visible: shown
    implicitHeight: shown ? content.implicitHeight + 16 : 0
    color: Qt.alpha(Theme.warning, Theme.dark ? 0.18 : 0.12)

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: Qt.alpha(Theme.warning, 0.5)
    }

    RowLayout {
        id: content

        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 8
        spacing: 10

        Icon {
            size: 20
            source: Theme.iconWarning
        }
        Label {
            readonly property string lost: bar.hiddenCount > 0
                                           ? " " + qsTr("Not shown in the diagram and removed on replace: %1.").arg(bar.hiddenSummary)
                                           : ""

            Layout.fillWidth: true
            wrapMode: Text.Wrap
            maximumLineCount: 4
            elide: Text.ElideRight
            color: Theme.textPrimary
            text: bar.blockState === DiagramEditorModel.HandWritten
                  ? qsTr("This block was written by hand. The diagram writes into it only after you replace it.") + lost
                  : bar.blockState === DiagramEditorModel.NotLoaded
                    ? qsTr("This generated block could not be read completely, so it is not overwritten automatically.") + lost
                    : qsTr("The block was edited in the text view. Writing from the diagram is paused so your changes are not lost.")

            HoverHandler {
                id: textHover
            }
            ToolTip.visible: textHover.hovered && bar.hiddenDetails.length > 0
            ToolTip.text: bar.hiddenDetails
            ToolTip.delay: 500
        }
        Button {
            visible: bar.blockState === DiagramEditorModel.EditedByHand
            text: qsTr("Load text into diagram")
            onClicked: bar.reloadRequested()
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Show the current text of the block in the diagram (unwritten diagram changes are dropped).")
            ToolTip.delay: 600
        }
        Button {
            text: qsTr("Replace with diagram")
            onClicked: bar.replaceRequested()
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Overwrite the block with the code of this diagram.")
            ToolTip.delay: 600
        }
        Button {
            text: qsTr("Keep text")
            flat: true
            onClicked: bar._dismissed = true
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Keep the text; the diagram does not write into the block until you replace it.")
            ToolTip.delay: 600
        }
    }
}
