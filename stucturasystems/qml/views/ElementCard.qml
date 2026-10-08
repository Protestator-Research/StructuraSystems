pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import StructuraSystems

// Card for one textual element (row) of a document: preview by default, inline editor on demand.
Pane {
    id: card

    required property int row
    required property string language
    required property string body
    required property string headerTitle
    required property string headerAuthor
    required property int problemCount
    property bool first: false
    property bool last: false
    /** True while no editing is allowed (background work in progress). */
    property bool locked: false
    /** Briefly true after the user navigated here from the problem list. */
    property bool flash: false
    property bool editing: false

    signal commitRequested(string text, string language)
    signal moveRequested(int target)
    signal insertRequested(int targetRow, string language)
    signal deleteRequested

    function startEdit() {
        if (!locked)
            editing = true;
    }

    function _closeEditor() {
        editing = false;
    }

    onEditingChanged: {
        heightTimer.restart();
    }

    padding: 0
    Material.elevation: 1
    Material.roundedScale: Theme.cardRadius
    Material.background: Theme.card

    HoverHandler {
        id: hover
    }

    readonly property bool actionsVisible: (!locked || editing) && (hover.hovered || editing || card.activeFocus || insertButton.activeFocus || actionMenu.visible)

    // The height change is animated only while switching between preview and editor, not while typing.
    Timer {
        id: heightTimer
        interval: 320
    }

    contentItem: Item {
        implicitWidth: content.implicitWidth
        implicitHeight: content.implicitHeight

        // Hairline border (shadows are invisible on dark themes). Doubles as highlight after a jump from the problem list.
        Rectangle {
            z: 1
            anchors.fill: parent
            radius: card.Material.roundedScale
            color: "transparent"
            border.width: card.flash || card.editing ? 2 : 1
            border.color: card.flash || card.editing ? Theme.accent : Theme.outline
            Behavior on border.color {
                ColorAnimation {
                    duration: 250
                    easing.type: Easing.OutCubic
                }
            }
        }

        ColumnLayout {
            id: content
            anchors.fill: parent
            spacing: 0

            // ---------------------------------------------------------------- header
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 14
                Layout.rightMargin: 6
                Layout.topMargin: 6
                Layout.preferredHeight: 38
                spacing: 8

                LanguageChip {
                    language: card.language
                }

                Rectangle {
                    visible: card.problemCount > 0
                    implicitWidth: problemRow.implicitWidth + 12
                    implicitHeight: 22
                    radius: 11
                    color: Qt.alpha(Theme.danger, Theme.dark ? 0.22 : 0.12)
                    border.width: 1
                    border.color: Qt.alpha(Theme.danger, 0.45)

                    Row {
                        id: problemRow
                        anchors.centerIn: parent
                        spacing: 4
                        Icon {
                            size: 14
                            source: Theme.iconError
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Label {
                            text: card.problemCount === 1 ? qsTr("1 problem") : qsTr("%1 problems").arg(card.problemCount)
                            font.pixelSize: 11
                            font.weight: Font.Medium
                            color: Theme.danger
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                    HoverHandler {
                        id: badgeHover
                    }
                    ToolTip.visible: badgeHover.hovered
                    ToolTip.text: qsTr("This element has problems. Open the problem list for details.")
                    ToolTip.delay: 500
                }

                Item {
                    Layout.fillWidth: true
                }

                RowLayout {
                    spacing: 0
                    opacity: card.actionsVisible ? 1 : 0
                    Behavior on opacity {
                        NumberAnimation {
                            duration: 150
                            easing.type: Easing.OutCubic
                        }
                    }

                    IconToolButton {
                        iconSource: card.editing ? Theme.iconPreview : Theme.iconEdit
                        iconSize: 20
                        tip: card.editing ? qsTr("Preview") : qsTr("Edit")
                        enabled: !card.locked
                        onClicked: card.editing ? card._closeEditor() : card.startEdit()
                    }
                    IconToolButton {
                        iconSource: Theme.iconMoveUp
                        iconSize: 20
                        tip: qsTr("Move up")
                        enabled: !card.locked && !card.first
                        onClicked: card.moveRequested(card.row - 1)
                    }
                    IconToolButton {
                        iconSource: Theme.iconMoveDown
                        iconSize: 20
                        tip: qsTr("Move down")
                        enabled: !card.locked && !card.last
                        onClicked: card.moveRequested(card.row + 1)
                    }
                    IconToolButton {
                        id: insertButton
                        iconSource: Theme.iconAdd
                        iconSize: 20
                        tip: qsTr("Insert element")
                        enabled: !card.locked
                        onClicked: actionMenu.popup(insertButton, 0, insertButton.height)
                    }
                    IconToolButton {
                        iconSource: Theme.iconTrash
                        iconSize: 20
                        tip: qsTr("Delete element")
                        enabled: !card.locked
                        onClicked: card.deleteRequested()
                    }
                }
            }

            // ---------------------------------------------------------------- body
            Item {
                id: bodyClip

                Layout.fillWidth: true
                implicitHeight: bodyLoader.item ? (bodyLoader.item as Item).implicitHeight + 16 : 16
                clip: true

                Behavior on implicitHeight {
                    enabled: heightTimer.running
                    NumberAnimation {
                        duration: Theme.animationDuration
                        easing.type: Easing.OutCubic
                    }
                }

                Loader {
                    id: bodyLoader
                    x: 14
                    y: 0
                    width: parent.width - 28
                    sourceComponent: card.editing ? editorComponent : card.language === "YAML" && card.headerTitle.length > 0 ? yamlComponent : card.language === "Markdown" ? markdownComponent : codeComponent
                }
            }
        }
    }

    Menu {
        id: actionMenu

        Menu {
            title: qsTr("Insert above")
            Repeater {
                model: Theme.languages
                MenuItem {
                    required property string modelData
                    text: modelData
                    onTriggered: card.insertRequested(card.row, modelData)
                }
            }
        }
        Menu {
            title: qsTr("Insert below")
            Repeater {
                model: Theme.languages
                MenuItem {
                    required property string modelData
                    text: modelData
                    onTriggered: card.insertRequested(card.row + 1, modelData)
                }
            }
        }
    }

    // ---------------------------------------------------------------- preview components
    Component {
        id: yamlComponent

        ColumnLayout {
            spacing: 2

            Label {
                Layout.fillWidth: true
                text: card.headerTitle
                wrapMode: Text.Wrap
                font.pixelSize: 24
                font.weight: Font.Medium
                color: Theme.textPrimary
            }
            Label {
                Layout.fillWidth: true
                visible: card.headerAuthor.length > 0
                text: qsTr("Author: %1").arg(card.headerAuthor)
                color: Theme.textSecondary
            }
            TapHandler {
                onDoubleTapped: card.startEdit()
            }
        }
    }

    Component {
        id: markdownComponent

        TextEdit {
            id: markdownText
            readOnly: true
            selectByMouse: true
            wrapMode: TextEdit.Wrap
            textFormat: card.body.length > 0 ? TextEdit.MarkdownText : TextEdit.PlainText
            text: card.body.length > 0 ? card.body : qsTr("Empty element. Double-click to edit.")
            color: card.body.length > 0 ? Theme.textPrimary : Theme.textSecondary
            font.italic: card.body.length === 0
            font.pixelSize: 14
            selectionColor: Material.accentColor
            onLinkActivated: link => Qt.openUrlExternally(link)
            TapHandler {
                onDoubleTapped: card.startEdit()
            }
        }
    }

    Component {
        id: codeComponent

        RowLayout {
            spacing: 8

            LineNumberGutter {
                Layout.fillHeight: true
                Layout.preferredWidth: implicitWidth
                visible: card.body.length > 0
                target: codeText
            }

            TextEdit {
                id: codeText
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.Wrap
                textFormat: TextEdit.PlainText
                text: card.body.length > 0 ? card.body : qsTr("Empty element. Double-click to edit.")
                color: card.body.length > 0 ? Theme.textPrimary : Theme.textSecondary
                font.family: card.body.length > 0 ? Theme.monoFamily : Qt.application.font.family
                font.italic: card.body.length === 0
                font.pixelSize: 13
                selectionColor: Material.accentColor
                padding: 0

                SysMLHighlighter {
                    textDocument: Theme.isCode(card.language) && card.body.length > 0 ? codeText.textDocument : null
                    darkTheme: Theme.dark
                }
                TapHandler {
                    onDoubleTapped: card.startEdit()
                }
            }
        }
    }

    // ---------------------------------------------------------------- editor
    Component {
        id: editorComponent

        ColumnLayout {
            id: editor

            function commit() {
                card.commitRequested(area.text, languageBox.currentText);
                card._closeEditor();
            }

            spacing: 8

            Component.onCompleted: {
                area.forceActiveFocus();
                area.cursorPosition = area.length;
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                LineNumberGutter {
                    Layout.fillHeight: true
                    Layout.preferredWidth: implicitWidth
                    visible: languageBox.currentText !== "Markdown"
                    target: area
                }

                TextArea {
                    id: area

                    Layout.fillWidth: true
                    text: card.body
                    wrapMode: TextEdit.Wrap
                    selectByMouse: true
                    font.family: languageBox.currentText === "Markdown" ? Qt.application.font.family : Theme.monoFamily
                    font.pixelSize: languageBox.currentText === "Markdown" ? 14 : 13
                    placeholderText: qsTr("Write something...")
                    persistentSelection: true
                    // Kept open but read-only while a background job works on the elements, so no typed text is lost.
                    readOnly: card.locked

                    SysMLHighlighter {
                        textDocument: Theme.isCode(languageBox.currentText) ? area.textDocument : null
                        darkTheme: Theme.dark
                    }

                    Keys.onPressed: event => {
                        if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && (event.modifiers & Qt.ControlModifier) && !card.locked) {
                            editor.commit();
                            event.accepted = true;
                        } else if (event.key === Qt.Key_Escape) {
                            card._closeEditor();
                            event.accepted = true;
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Label {
                    text: qsTr("Language")
                    color: Theme.textSecondary
                }
                ComboBox {
                    id: languageBox
                    Layout.preferredWidth: 140
                    model: Theme.languages
                    currentIndex: Math.max(0, Theme.languages.indexOf(card.language))
                    Accessible.name: qsTr("Language")
                }
                Item {
                    Layout.fillWidth: true
                }
                Label {
                    text: qsTr("Ctrl+Enter to apply, Esc to cancel")
                    color: Theme.textSecondary
                    font.pixelSize: 11
                }
                Button {
                    text: qsTr("Cancel")
                    flat: true
                    onClicked: card._closeEditor()
                }
                Button {
                    text: qsTr("Done")
                    highlighted: true
                    enabled: !card.locked
                    icon.source: Theme.iconCommit
                    icon.color: "transparent"
                    icon.width: 18
                    icon.height: 18
                    onClicked: editor.commit()
                }
            }
        }
    }
}
