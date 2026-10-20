import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import StructuraSystems

// Input of a type with suggestions (the fitting definitions of the model and library types) and, for a name that is
// not defined anywhere, the offer to create its definition ("port def FuelPort").
ColumnLayout {
    id: typeInput

    /** text -> [{text, detail}]: the suggestions for the input. */
    property var suggest: function (text) { return [] }
    /** text -> {known, name, keyword}: whether the input is a known type. */
    property var check: function (text) { return { known: true } }
    property alias text: field.text
    property alias placeholderText: field.placeholderText
    property string accessibleName: qsTr("Type")
    /** Create the definition of an unknown type (the tick below the field). */
    property bool createType: true

    readonly property var status: check(field.text)
    readonly property bool unknown: field.text.trim().length > 0 && status.known === false
    /** Create a definition on apply. */
    readonly property bool createsType: unknown && createType

    /** Enter in the field (without a suggestion chosen). */
    signal accepted

    function choose(index) {
        if (index < 0 || index >= list.count)
            return
        field.text = list.model[index].text
        popup.close()
        field.forceActiveFocus()
        field.cursorPosition = field.text.length
    }
    function showSuggestions() {
        list.model = typeInput.suggest(field.text)
        // Enter takes the first suggestion while typing (Esc closes the list to keep the input as it is)
        list.currentIndex = field.text.trim().length > 0 && list.count > 0 ? 0 : -1
        if (list.count > 0 && field.activeFocus)
            popup.open()
        else
            popup.close()
    }
    function reset(text) {
        field.text = text
        createType = true
        popup.close()
    }

    spacing: 2

    TextField {
        id: field

        Layout.fillWidth: true
        Accessible.name: typeInput.accessibleName
        onTextEdited: typeInput.showSuggestions()
        onActiveFocusChanged: if (!activeFocus) popup.close()
        onAccepted: typeInput.accepted()

        Keys.onDownPressed: event => {
            if (!popup.opened) {
                typeInput.showSuggestions()
            } else if (list.count > 0) {
                list.currentIndex = Math.min(list.count - 1, list.currentIndex + 1)
            }
            event.accepted = true
        }
        Keys.onUpPressed: event => {
            if (popup.opened && list.currentIndex > 0)
                list.currentIndex = list.currentIndex - 1
            event.accepted = true
        }
        Keys.onReturnPressed: event => {
            if (popup.opened && list.currentIndex >= 0) {
                typeInput.choose(list.currentIndex)
                event.accepted = true
            } else {
                popup.close()
                event.accepted = false
            }
        }
        Keys.onEnterPressed: event => {
            if (popup.opened && list.currentIndex >= 0) {
                typeInput.choose(list.currentIndex)
                event.accepted = true
            } else {
                popup.close()
                event.accepted = false
            }
        }
        Keys.onEscapePressed: event => {
            if (popup.opened) {
                popup.close()
                event.accepted = true
            } else {
                event.accepted = false
            }
        }

        Popup {
            id: popup

            y: field.height
            width: field.width
            padding: 2
            closePolicy: Popup.CloseOnPressOutsideParent
            implicitHeight: Math.min(list.contentHeight, 260) + 2 * padding

            contentItem: ListView {
                id: list

                objectName: "typeSuggestions"
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                highlightMoveDuration: 0
                currentIndex: -1
                ScrollIndicator.vertical: ScrollIndicator {}

                delegate: ItemDelegate {
                    required property var modelData
                    required property int index

                    width: ListView.view.width
                    height: 30
                    highlighted: ListView.isCurrentItem
                    onClicked: typeInput.choose(index)

                    contentItem: RowLayout {
                        spacing: 8
                        Label {
                            Layout.fillWidth: true
                            text: modelData.text
                            elide: Text.ElideRight
                            color: Theme.textPrimary
                        }
                        Label {
                            text: modelData.detail
                            font.pixelSize: 11
                            color: Theme.textSecondary
                        }
                    }
                }
            }
        }
    }

    // not defined anywhere: create its definition (or correct a typo)
    Label {
        visible: typeInput.unknown
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        font.pixelSize: 12
        color: Theme.textSecondary
        text: typeInput.unknown ? qsTr("'%1' is not defined in this block.").arg(typeInput.status.name) : ""
    }
    CheckBox {
        id: createCheck

        visible: typeInput.unknown
        padding: 0
        checked: typeInput.createType
        onToggled: typeInput.createType = checked
        text: typeInput.unknown ? qsTr("Create %1 %2").arg(typeInput.status.keyword).arg(typeInput.status.name) : ""
    }
}
