import QtQuick
import QtQuick.Layouts

// Footer of a dialog with explicit button order: put Buttons inside, the last one is the primary (filled) action.
// Buttons are right aligned; with spread set the content is laid out from edge to edge, so an Item with
// Layout.fillWidth between the buttons can push the dismissing one to the left.
// (A DialogButtonBox cannot be used: the Material style makes all its buttons flat and orders them by role.)
Item {
    id: footer

    default property alias buttons: row.data
    property bool spread: false

    implicitWidth: row.implicitWidth + 16
    implicitHeight: 52

    RowLayout {
        id: row

        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        spacing: 8

        Item {
            Layout.fillWidth: true
            visible: !footer.spread
        }
    }
}
