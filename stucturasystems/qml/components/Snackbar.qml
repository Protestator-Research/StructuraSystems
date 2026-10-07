import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import StructuraSystems

// Bottom-center message bar. Messages are queued and shown one after the other.
Item {
    id: control

    /** Time a message stays visible in milliseconds. */
    property int interval: 4000
    readonly property bool shown: pane.opened

    signal detailsRequested(string details)

    property var _queue: []
    property int _level: 0
    property string _message
    property string _details

    implicitWidth: pane.width
    implicitHeight: pane.implicitHeight

    /** @param level 0 info, 1 success, 2 warning, 3 error */
    function show(level, message, details) {
        _queue.push({ level: level, message: message, details: details })
        if (!pane.opened && !gap.running)
            _showNext()
    }

    function dismiss() {
        if (!pane.opened)
            return
        pane.opened = false
        timer.stop()
        gap.start()
    }

    function _showNext() {
        if (_queue.length === 0)
            return
        const next = _queue.shift()
        _level = next.level
        _message = next.message
        _details = next.details
        pane.opened = true
        timer.restart()
    }

    Timer {
        id: timer
        interval: control.interval
        running: false
        onTriggered: control.dismiss()
    }

    // Short pause so that the fade out of one message finishes before the next one appears.
    Timer {
        id: gap
        interval: Theme.animationDuration + 60
        onTriggered: control._showNext()
    }

    Pane {
        id: pane

        property bool opened: false

        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(Math.max(implicitWidth, 320), 640)
        opacity: opened ? 1 : 0
        visible: opacity > 0
        y: opened ? 0 : 16
        padding: 0
        leftPadding: 16
        rightPadding: 6
        Material.elevation: 6
        Material.roundedScale: Material.SmallScale
        Material.background: control._level === 3 ? "#8c2f2f"
                             : control._level === 2 ? "#8a5a00"
                             : control._level === 1 ? "#2e6b47" : "#2f4858"
        Material.foreground: "white"

        Behavior on opacity { NumberAnimation { duration: Theme.animationDuration; easing.type: Easing.OutCubic } }
        Behavior on y { NumberAnimation { duration: Theme.animationDuration; easing.type: Easing.OutCubic } }

        HoverHandler {
            id: hover
            onHoveredChanged: {
                if (pane.opened && !hovered)
                    timer.restart()
                else if (hovered)
                    timer.stop()
            }
        }

        contentItem: RowLayout {
            spacing: 10

            Icon {
                size: 20
                source: control._level === 3 ? Theme.iconError
                        : control._level === 2 ? Theme.iconWarning
                        : control._level === 1 ? Theme.iconCheck : Theme.iconInfo
            }
            Label {
                Layout.fillWidth: true
                Layout.topMargin: 10
                Layout.bottomMargin: 10
                text: control._message
                wrapMode: Text.Wrap
                maximumLineCount: 3
                elide: Text.ElideRight
                color: "white"
            }
            Button {
                visible: control._details.length > 0
                flat: true
                text: qsTr("Details")
                Material.foreground: "#cfe9ff"
                onClicked: {
                    control.detailsRequested(control._details)
                    control.dismiss()
                }
            }
            IconToolButton {
                iconSource: Theme.iconClose
                iconSize: 16
                tip: qsTr("Dismiss")
                onClicked: control.dismiss()
            }
        }
    }
}
