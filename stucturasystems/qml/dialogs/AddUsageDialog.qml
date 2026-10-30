import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import StructuraSystems

// Adds a usage of another online project to the current online document (SysML v2 ProjectUsage): a new commit on the branch of
// the document that pins the head commit of the chosen branch of the used project. The usage is stored on the server.
// openFor(projectId, name) is used when the project is known (context menu of a project), openPicker() lets the user choose it.
// There is one instance of this dialog (in Main.qml); the other views ask Main to open it.
// The project and the branch are passed by their ids, never by a row: the lists can change while the dialog is open.
Dialog {
    id: dialog

    /** Id of the used project if it is fixed, empty if the user picks it. */
    property string fixedId
    property string fixedName
    /** True from a started commit until it has finished. */
    property bool waiting: false

    readonly property string projectId: fixedId.length > 0 ? fixedId : (projectBox.currentValue !== undefined ? String(projectBox.currentValue) : "")
    readonly property string branchId: branchBox.currentIndex >= 0 && branchBox.currentIndex < AppController.usageBranchIds.length
                                       ? AppController.usageBranchIds[branchBox.currentIndex] : ""
    readonly property var document: AppController.currentDocument
    readonly property bool canAdd: document !== null && document.isOnline && projectId.length > 0 && branchId.length > 0
                                   && !AppController.busy && !waiting

    function openFor(id, name) {
        fixedId = id
        fixedName = name
        _prepare()
        open()
    }

    function openPicker() {
        fixedId = ""
        fixedName = ""
        _prepare()
        open()
    }

    /** Adds the usage. @return true if the commit was started; false if it was refused (the dialog stays open, the reason is notified). */
    function submit() {
        if (!canAdd)
            return false
        if (!AppController.addRemoteUsage(projectId, branchId))
            return false
        waiting = true
        return true
    }

    function _prepare() {
        waiting = false
        AppController.loadUsageBranches(projectId)
    }

    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay
    modal: true
    title: qsTr("Add project usage")
    width: 480

    // The default branch is preselected when the branches of the project arrive.
    Connections {
        target: AppController

        function onUsageBranchNamesChanged() {
            branchBox.currentIndex = AppController.usageDefaultBranchIndex
        }

        function onBusyChanged() {
            // The commit has finished (the result, or the error, is reported by the notifications).
            if (dialog.waiting && !AppController.busy) {
                dialog.waiting = false
                dialog.close()
            }
        }
    }

    // Enter adds the usage (not while a ComboBox list is open: there it chooses the entry).
    Shortcut {
        sequences: ["Return", "Enter"]
        enabled: dialog.visible && dialog.canAdd && !projectBox.popup.visible && !branchBox.popup.visible && !cancelButton.activeFocus
        onActivated: dialog.submit()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.textSecondary
            text: dialog.document
                  ? qsTr("The chosen project is added to \"%1\" as a usage, pinned to the head commit of the chosen branch. "
                         + "This creates a commit on the branch of the document.").arg(dialog.document.title)
                  : qsTr("Open an online project first: the usage is added to it.")
        }

        Label {
            visible: dialog.fixedId.length > 0
            Layout.fillWidth: true
            elide: Text.ElideRight
            font.weight: Font.Medium
            color: Theme.textPrimary
            text: qsTr("Used project: %1").arg(dialog.fixedName)
        }
        ComboBox {
            id: projectBox

            visible: dialog.fixedId.length === 0
            Layout.fillWidth: true
            enabled: !dialog.waiting
            model: AppController.onlineProjects
            textRole: "name"
            valueRole: "projectId"
            Accessible.name: qsTr("Used project")
            onActivated: AppController.loadUsageBranches(dialog.projectId)
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            ComboBox {
                id: branchBox

                Layout.fillWidth: true
                enabled: !dialog.waiting && count > 0
                model: AppController.usageBranchNames
                displayText: count > 0 ? currentText : (AppController.usageBranchesLoading ? qsTr("Loading the branches...") : qsTr("No branch"))
                Accessible.name: qsTr("Branch of the used project")
            }
            BusyIndicator {
                running: AppController.usageBranchesLoading || dialog.waiting
                visible: running
                implicitWidth: 28
                implicitHeight: 28
            }
        }
    }

    footer: DialogFooter {
        Button {
            id: cancelButton
            text: qsTr("Cancel")
            flat: true
            enabled: !dialog.waiting
            onClicked: dialog.reject()
        }
        Button {
            text: dialog.waiting ? qsTr("Adding...") : qsTr("Add usage")
            highlighted: true
            enabled: dialog.canAdd
            onClicked: dialog.submit()
        }
    }
}
