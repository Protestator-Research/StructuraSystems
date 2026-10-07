import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import StructuraSystems

// Thin gap between two cards. A small "+" button appears on hover and opens the language menu.
Item {
    id: control

    property bool locked: false

    signal insertRequested(string language)

    implicitHeight: 28

    HoverHandler {
        id: hover
        enabled: !control.locked
    }

    readonly property bool active: !locked && (hover.hovered || menu.visible || button.activeFocus)

    Rectangle {
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.right: parent.right
        height: 1
        color: Theme.accent
        opacity: control.active ? 0.5 : 0
        Behavior on opacity { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
    }

    ToolButton {
        id: button
        anchors.centerIn: parent
        width: 24
        height: 24
        padding: 0
        enabled: !control.locked
        opacity: control.active ? 1 : 0
        icon.source: Theme.iconAdd
        icon.width: 14
        icon.height: 14
        icon.color: "transparent"
        Material.background: Theme.card
        Accessible.name: qsTr("Insert element here")
        ToolTip.visible: hovered
        ToolTip.text: qsTr("Insert element here")
        ToolTip.delay: 500
        Behavior on opacity { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }

        background: Rectangle {
            radius: 12
            color: Theme.card
            border.width: 1
            border.color: Theme.outline
        }
        onClicked: menu.popup(button, 0, button.height)
    }

    LanguageMenu {
        id: menu
        onLanguageSelected: language => control.insertRequested(language)
    }
}
