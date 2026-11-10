pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import StructuraSystems

// One side of the (optionally split) editor area: detached banner and the element list of one document.
FocusScope {
    id: pane

    property DocumentModel document
    /** Whether this side holds the current document of the application. */
    property bool active: true
    /** Whether the editor is split; only then the active side is marked. */
    property bool split: false

    /** Emitted when the user works in this side, so it becomes the active one. */
    signal activationRequested()
    signal returnRequested()
    signal newBranchRequested()

    function scrollToRow(row) {
        editor.scrollToRow(row)
    }

    onActiveFocusChanged: {
        if (activeFocus)
            pane.activationRequested()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 2
            visible: pane.split
            color: pane.active ? Theme.accent : "transparent"
        }

        DetachedBanner {
            Layout.fillWidth: true
            shown: pane.document !== null && pane.document.detached
            commitId: pane.document ? pane.document.commitId : ""
            branchName: pane.document ? pane.document.branchName : ""
            busy: AppController.busy
            onReturnRequested: {
                pane.activationRequested()
                pane.returnRequested()
            }
            onNewBranchRequested: {
                pane.activationRequested()
                pane.newBranchRequested()
            }
        }

        EditorView {
            id: editor

            Layout.fillWidth: true
            Layout.fillHeight: true
            document: pane.document
        }
    }

    // Passive handler: clicks without a focus change still activate the side, the element cards keep receiving them.
    TapHandler {
        gesturePolicy: TapHandler.DragThreshold
        grabPermissions: PointerHandler.TakeOverForbidden
        onTapped: pane.activationRequested()
    }
}
