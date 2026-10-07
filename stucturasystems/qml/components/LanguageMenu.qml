pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import StructuraSystems

// Menu that offers the four element languages.
Menu {
    id: control

    signal languageSelected(string language)

    Repeater {
        model: Theme.languages

        MenuItem {
            required property string modelData

            text: modelData
            onTriggered: control.languageSelected(modelData)
        }
    }
}
