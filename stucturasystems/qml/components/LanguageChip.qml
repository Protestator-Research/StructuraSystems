import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import StructuraSystems

// Small coloured label that shows the language of an element.
Rectangle {
    id: control

    property string language

    implicitWidth: label.implicitWidth + 16
    implicitHeight: 22
    radius: height / 2
    color: Qt.alpha(Theme.languageColor(language), Theme.dark ? 0.22 : 0.14)
    border.width: 1
    border.color: Qt.alpha(Theme.languageColor(language), 0.45)

    Label {
        id: label
        anchors.centerIn: parent
        text: control.language
        font.pixelSize: 11
        font.weight: Font.Medium
        color: Theme.languageColor(control.language)
    }
}
