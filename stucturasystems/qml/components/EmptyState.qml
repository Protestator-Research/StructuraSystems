import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import StructuraSystems

// Friendly placeholder with an icon, a headline, an explanation and optional action buttons.
Item {
    id: control

    property url iconSource
    property int iconSize: 64
    property string title
    property string text
    default property alias actions: actionRow.data

    implicitWidth: column.implicitWidth
    implicitHeight: column.implicitHeight

    ColumnLayout {
        id: column
        anchors.centerIn: parent
        width: Math.min(parent.width - 32, 360)
        spacing: 10

        Icon {
            Layout.alignment: Qt.AlignHCenter
            source: control.iconSource
            size: control.iconSize
            visible: control.iconSource.toString().length > 0
        }
        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            text: control.title
            font.pixelSize: 17
            font.weight: Font.Medium
            color: Theme.textPrimary
        }
        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            text: control.text
            visible: text.length > 0
            color: Theme.textSecondary
        }
        Flow {
            id: actionRow
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 6
            Layout.maximumWidth: column.width
            spacing: 8
        }
    }
}
