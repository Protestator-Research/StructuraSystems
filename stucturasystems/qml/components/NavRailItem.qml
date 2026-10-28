import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import StructuraSystems

// One destination of the navigation rail: icon on a Material "pill" indicator with a label below.
AbstractButton {
    id: control

    property url iconSource
    property bool selected: false

    implicitWidth: Theme.railWidth
    implicitHeight: 66
    hoverEnabled: true
    focusPolicy: Qt.TabFocus

    Accessible.name: text
    Accessible.role: Accessible.PageTab
    ToolTip.visible: hovered
    ToolTip.text: text
    ToolTip.delay: 700

    background: Item {
        Rectangle {
            anchors.fill: parent
            anchors.margins: 2
            radius: 8
            color: "transparent"
            border.width: control.visualFocus ? 2 : 0
            border.color: Material.accentColor
        }
    }

    contentItem: Column {
        spacing: 4
        topPadding: 6

        Item {
            width: parent.width
            height: 32

            Rectangle {
                id: pill
                anchors.centerIn: parent
                width: 56
                height: 32
                radius: 16
                color: control.selected ? Theme.indicator
                                        : control.hovered ? Qt.alpha(Theme.textPrimary, 0.08) : "transparent"
                Behavior on color { ColorAnimation { duration: 150; easing.type: Easing.OutCubic } }
            }
            Icon {
                anchors.centerIn: parent
                source: control.iconSource
                size: 24
                opacity: control.enabled ? 1 : 0.4
            }
        }
        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: control.text
            font.pixelSize: 12
            font.weight: control.selected ? Font.DemiBold : Font.Normal
            color: control.selected ? Theme.textPrimary : Theme.textSecondary
        }
    }
}
