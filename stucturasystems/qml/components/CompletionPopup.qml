pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import StructuraSystems

// Completion list of the code editor (IDE style). It never takes the focus: the editor hands the keys to handleKey(), and the list
// follows the text and the cursor of the editor.
Popup {
    id: popup

    /** The editor the suggestions are for. */
    required property TextArea editor
    /** The document whose last parse supplies the names of the libraries. */
    property DocumentModel document
    /** "SysMLv2" or "KerML"; the popup only works for these. */
    property string language
    /** True while no suggestions are wanted (locked card, text that is not code). */
    property bool blocked: false

    readonly property int maxVisibleRows: 8
    readonly property int rowHeight: 26

    property var _items: []
    property int _start: -1
    property int _end: -1
    // Set while the accepted suggestion is written, so the change does not open the list again.
    property bool _applying: false
    property int _lastLength: 0
    property string _lastText: ""
    property int _lastCursor: -1

    /** Opens the list for the cursor of the editor. mode: "auto" (typing, only with enough input), "manual" (Ctrl+Space) or "update" (list is open). */
    function refresh(mode) {
        if (blocked || !editor.activeFocus || editor.preeditText !== "") {
            close();
            return;
        }
        const text = editor.text;
        const cursor = editor.cursorPosition;
        const result = controller.complete(text, cursor);
        _lastText = text;
        _lastCursor = cursor;
        if (!result.valid || result.items.length === 0) {
            close();
            return;
        }
        if (mode === "auto") {
            const before = cursor > 0 ? text.charAt(cursor - 1) : "";
            const afterOperator = (result.kind === "member" || result.kind === "type" || result.kind === "import") && (before === ":" || before === ">" || before === "~");
            if (result.prefix.length < 2 && !afterOperator)
                return;
        } else if (mode === "update") {
            // The word was deleted: nothing to complete any more.
            if (result.prefix.length === 0 && (result.kind === "keyword" || result.kind === "anything")) {
                close();
                return;
            }
            if (result.replaceStart !== _start && _cursorOnly) {
                close();
                return;
            }
        }
        _items = result.items;
        _start = result.replaceStart;
        _end = result.replaceEnd;
        list.currentIndex = 0;
        list.positionViewAtBeginning();
        if (!visible)
            open();
    }

    // True while refresh() is called because the cursor moved without a change of the text.
    property bool _cursorOnly: false

    function accept(index) {
        const item = _items[index];
        if (!item)
            return;
        const start = _start;
        const end = _end;
        _applying = true;
        // remove/insert (like the editing help), so the change stays in the undo stack. If the suggestion only continues what
        // was typed, just the rest is inserted: one undo step, merged with the typing.
        const existing = editor.getText(start, end);
        if (item.insertText.startsWith(existing)) {
            editor.insert(end, item.insertText.slice(existing.length));
        } else {
            if (end > start)
                editor.remove(start, end);
            editor.insert(start, item.insertText);
        }
        const position = item.cursorOffset >= 0 ? start + item.cursorOffset : start + item.insertText.length;
        if (item.selectionLength > 0)
            editor.select(position, position + item.selectionLength);
        else
            editor.cursorPosition = position;
        _applying = false;
        _lastLength = editor.length;
        _lastText = editor.text;
        close();
        // A package that was completed with the two colons goes on with its members.
        if (item.insertText.endsWith("::"))
            Qt.callLater(() => popup.refresh("auto"));
    }

    /** Called first by the key handler of the editor. Returns true if the key was consumed. */
    function handleKey(event) {
        if (blocked)
            return false;
        // Ctrl+Space (also Ctrl on macOS, which Qt reports as Meta); AltGr is Ctrl+Alt on Windows and stays a character.
        if (event.key === Qt.Key_Space && (event.modifiers & (Qt.ControlModifier | Qt.MetaModifier)) !== 0 && (event.modifiers & Qt.AltModifier) === 0) {
            refresh(visible ? "update" : "manual");
            return true;
        }
        if (!visible)
            return false;
        if ((event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier | Qt.ShiftModifier)) !== 0)
            return false;
        const count = _items.length;
        switch (event.key) {
        case Qt.Key_Up:
            list.currentIndex = list.currentIndex <= 0 ? count - 1 : list.currentIndex - 1;
            return true;
        case Qt.Key_Down:
            list.currentIndex = list.currentIndex >= count - 1 ? 0 : list.currentIndex + 1;
            return true;
        case Qt.Key_PageUp:
            list.currentIndex = Math.max(0, list.currentIndex - maxVisibleRows);
            return true;
        case Qt.Key_PageDown:
            list.currentIndex = Math.min(count - 1, list.currentIndex + maxVisibleRows);
            return true;
        case Qt.Key_Return:
        case Qt.Key_Enter:
        case Qt.Key_Tab:
            accept(list.currentIndex);
            return true;
        case Qt.Key_Escape:
            close();
            return true;
        default:
            return false;
        }
    }

    // Only a letter of a name (or the operators before a type or member) can open the list while typing: a cheap test that saves
    // asking the engine after every space or bracket.
    function _mayOpenAfter(character) {
        return character !== "" && (/[\w:>~']/.test(character) || character > "\u007f");
    }

    function _kindColor(kind) {
        switch (kind) {
        case "snippet":
            return Theme.warning;
        case "definition":
            return Theme.accent;
        case "usage":
            return Theme.info;
        case "package":
            return Theme.success;
        default:
            return Theme.textSecondary;
        }
    }

    function _kindLetter(kind) {
        switch (kind) {
        case "keyword":
            return "K";
        case "snippet":
            return "S";
        case "definition":
            return "D";
        case "usage":
            return "U";
        case "package":
            return "P";
        default:
            return "·";
        }
    }

    // Below the cursor, above it if the list does not fit into the window.
    readonly property rect _cursor: editor.cursorRectangle

    parent: editor
    x: Math.max(0, Math.min(_cursor.x, editor.width - width))
    y: {
        const below = _cursor.y + _cursor.height + 2;
        const globalY = editor.mapToItem(null, 0, below).y;
        return globalY + height > editor.Window.height ? Math.max(0, _cursor.y - height - 2) : below;
    }
    width: Math.min(460, editor.width)
    padding: 2
    modal: false
    focus: false
    closePolicy: Popup.CloseOnPressOutside
    Material.elevation: 6

    onBlockedChanged: {
        if (blocked)
            close();
    }

    Component.onCompleted: _lastLength = editor.length

    CompletionController {
        id: controller
        document: popup.document
        language: popup.language
    }

    Connections {
        target: popup.editor

        function onTextChanged() {
            if (popup._applying || popup.blocked)
                return;
            const grew = popup.editor.length > popup._lastLength;
            popup._lastLength = popup.editor.length;
            popup._cursorOnly = false;
            if (popup.visible)
                popup.refresh("update");
            else if (grew && popup.editor.activeFocus && popup._mayOpenAfter(popup.editor.text.charAt(popup.editor.cursorPosition - 1)))
                popup.refresh("auto");
        }

        function onCursorPositionChanged() {
            // Typing changes the cursor, too: the text handler does that case (and the list is not computed twice).
            if (!popup.visible || popup._applying || popup.editor.text !== popup._lastText || popup.editor.cursorPosition === popup._lastCursor)
                return;
            popup._cursorOnly = true;
            popup.refresh("update");
            popup._cursorOnly = false;
        }

        function onActiveFocusChanged() {
            if (!popup.editor.activeFocus)
                popup.close();
        }
    }

    background: Rectangle {
        color: Theme.card
        radius: 6
        border.width: 1
        border.color: Theme.outline
    }

    contentItem: ListView {
        id: list

        implicitHeight: Math.min(contentHeight, popup.maxVisibleRows * popup.rowHeight)
        model: popup._items
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        highlightMoveDuration: 0
        ScrollBar.vertical: ScrollBar {}

        delegate: ItemDelegate {
            id: row

            required property int index
            required property var modelData

            width: ListView.view.width
            height: popup.rowHeight
            padding: 0
            leftPadding: 8
            rightPadding: 8
            topPadding: 0
            bottomPadding: 0
            focusPolicy: Qt.NoFocus
            highlighted: ListView.isCurrentItem
            onClicked: popup.accept(row.index)

            contentItem: RowLayout {
                spacing: 8

                Rectangle {
                    Layout.preferredWidth: 18
                    Layout.preferredHeight: 18
                    radius: 4
                    color: Qt.alpha(popup._kindColor(row.modelData.kind), Theme.dark ? 0.28 : 0.16)

                    Label {
                        anchors.centerIn: parent
                        text: popup._kindLetter(row.modelData.kind)
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                        color: popup._kindColor(row.modelData.kind)
                    }
                }
                Label {
                    text: row.modelData.label
                    font.family: Theme.monoFamily
                    font.pixelSize: 13
                    color: Theme.textPrimary
                    elide: Text.ElideRight
                }
                Item {
                    Layout.fillWidth: true
                }
                Label {
                    Layout.maximumWidth: popup.width * 0.5
                    text: row.modelData.detail
                    font.family: Theme.monoFamily
                    font.pixelSize: 11
                    color: Theme.textSecondary
                    elide: Text.ElideMiddle
                }
            }
        }
    }
}
