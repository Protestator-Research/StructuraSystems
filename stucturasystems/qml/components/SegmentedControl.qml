pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import StructuraSystems

// Material segmented button group. Single selection, individual segments can be disabled.
Item {
    id: control

    property var model: []
    property int currentIndex: 0
    /** Indexes of segments that cannot be selected. */
    property var disabledIndexes: []

    signal activated(int index)

    implicitWidth: row.implicitWidth
    implicitHeight: 34

    Row {
        id: row
        height: parent.height

        Repeater {
            model: control.model

            AbstractButton {
                id: segment

                required property int index
                required property var modelData
                readonly property bool selected: control.currentIndex === index

                height: row.height
                implicitWidth: Math.max(96, label.implicitWidth + 32)
                enabled: control.disabledIndexes.indexOf(index) < 0
                hoverEnabled: true
                text: modelData
                onClicked: {
                    control.currentIndex = index
                    control.activated(index)
                }

                background: Rectangle {
                    color: segment.selected ? Theme.indicator
                                            : segment.hovered ? Qt.alpha(Theme.textPrimary, 0.06) : "transparent"
                    border.width: 1
                    border.color: Theme.outline
                    topLeftRadius: segment.index === 0 ? height / 2 : 0
                    bottomLeftRadius: segment.index === 0 ? height / 2 : 0
                    topRightRadius: segment.index === control.model.length - 1 ? height / 2 : 0
                    bottomRightRadius: segment.index === control.model.length - 1 ? height / 2 : 0
                    Behavior on color { ColorAnimation { duration: 150; easing.type: Easing.OutCubic } }
                }
                contentItem: Label {
                    id: label
                    text: segment.text
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    font.weight: segment.selected ? Font.DemiBold : Font.Normal
                    color: Theme.textPrimary
                    opacity: segment.enabled ? 1 : 0.4
                }
            }
        }
    }
}
