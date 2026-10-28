import QtQuick

// Multi-colour PNG icon, always rendered in its original colours.
Item {
    id: control

    property int size: 20
    property alias source: image.source

    implicitWidth: size
    implicitHeight: size

    Image {
        id: image
        anchors.fill: parent
        sourceSize: Qt.size(control.size * 2, control.size * 2)
        fillMode: Image.PreserveAspectFit
        smooth: true
        mipmap: true
    }
}
