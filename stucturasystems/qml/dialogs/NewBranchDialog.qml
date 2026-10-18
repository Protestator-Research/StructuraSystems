import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import StructuraSystems

// Creates a branch on the server, that starts at a commit of the current online project.
Dialog {
    id: dialog

    /** Commit the branch starts from, empty for the commit that is loaded in the document. */
    property string fromCommitId
    /** Incremented when branches or commits are reloaded, so that the texts below follow the lists. */
    property int _revision: 0

    readonly property var doc: AppController.currentDocument
    readonly property string startCommitId: fromCommitId.length > 0 ? fromCommitId : (doc ? doc.commitId : "")
    readonly property string trimmedName: nameField.text.trim()
    readonly property bool nameEmpty: trimmedName.length === 0
    readonly property bool nameValid: {
        _revision
        return AppController.branchNameAvailable(trimmedName)
    }
    readonly property string problem: nameEmpty ? qsTr("Enter a name for the branch.")
                                                : nameValid ? "" : qsTr("A branch with this name already exists, or the name is not valid.")
    readonly property string startDescription: {
        _revision
        return AppController.commitHistory.descriptionOf(startCommitId)
    }
    readonly property string startDate: {
        _revision
        return Theme.formatCommitDate(AppController.commitHistory.createdOf(startCommitId))
    }
    readonly property string startInfo: {
        const parts = [startCommitId.substring(0, 8)]
        parts.push(startDescription.length > 0 ? startDescription : qsTr("(no description)"))
        if (startDate.length > 0)
            parts.push(startDate)
        return qsTr("Starts from: %1").arg(parts.join(" · "))
    }
    /** Unsaved changes of the editor are replaced when the new branch starts at another commit than the loaded one. */
    readonly property bool discardsChanges: switchBox.checked && doc !== null && doc.modified
                                            && startCommitId !== doc.commitId
    readonly property bool canCreate: nameValid && !AppController.busy && AppController.connected

    /** @param commitId commit to start from, leave empty to start from the loaded commit */
    function openFor(commitId) {
        fromCommitId = commitId ? commitId : ""
        open()
    }

    function submit() {
        if (canCreate)
            accept()
    }

    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay
    modal: true
    title: qsTr("New branch")
    width: 460

    onAboutToShow: {
        nameField.text = ""
        switchBox.checked = true
        _revision++
    }
    onOpened: nameField.forceActiveFocus()

    Connections {
        target: AppController.commitHistory

        function onModelReset() {
            dialog._revision++
        }
    }

    Connections {
        target: AppController.branches

        function onModelReset() {
            dialog._revision++
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        Label {
            Layout.fillWidth: true
            text: dialog.startInfo
            wrapMode: Text.Wrap
            color: Theme.textSecondary
        }
        TextField {
            id: nameField
            Layout.fillWidth: true
            placeholderText: qsTr("Branch name")
            selectByMouse: true
            onAccepted: dialog.submit()
        }
        Label {
            Layout.fillWidth: true
            // The empty-name hint is shown quietly, an invalid name in the error color.
            text: dialog.problem
            visible: text.length > 0
            wrapMode: Text.Wrap
            font.pixelSize: 12
            color: dialog.nameEmpty ? Theme.textSecondary : Theme.danger
        }
        CheckBox {
            id: switchBox
            Layout.fillWidth: true
            checked: true
            text: qsTr("Switch to the new branch")
        }
        Label {
            Layout.fillWidth: true
            visible: dialog.discardsChanges
            text: qsTr("The unsaved changes of this document will be discarded, because the commit is shown instead.")
            wrapMode: Text.Wrap
            font.pixelSize: 12
            color: Theme.warning
        }
    }

    footer: DialogFooter {
        Button {
            text: qsTr("Cancel")
            flat: true
            onClicked: dialog.reject()
        }
        Button {
            text: qsTr("Create")
            highlighted: true
            enabled: dialog.canCreate
            icon.source: Theme.iconBranch
            icon.color: "transparent"
            icon.width: 18
            icon.height: 18
            onClicked: dialog.submit()
        }
    }

    onAccepted: AppController.createBranch(dialog.trimmedName, dialog.fromCommitId, switchBox.checked)
}
