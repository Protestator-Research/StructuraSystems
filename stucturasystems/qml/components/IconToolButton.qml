import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import StructuraSystems

// Flat icon button with a tooltip that is also used as accessible name.
ToolButton {
    id: control

    property url iconSource
    property string tip
    property int iconSize: Theme.iconSize

    icon.source: iconSource
    icon.width: iconSize
    icon.height: iconSize
    icon.color: "transparent"
    hoverEnabled: true
    opacity: enabled ? 1 : 0.4

    Accessible.name: tip
    ToolTip.visible: hovered && tip.length > 0
    ToolTip.text: tip
    ToolTip.delay: 600
}
