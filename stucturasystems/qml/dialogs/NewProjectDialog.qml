import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import StructuraSystems

// Creates a local project or, when connected, a project on the server.
Dialog {
    id: dialog

    readonly property bool online: kind.currentIndex === 1

    function openNew(preferOnline) {
        kind.currentIndex = preferOnline && AppController.connected ? 1 : 0
        open()
    }

    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay
    modal: true
    title: qsTr("New project")
    width: 460

    onAboutToShow: {
        nameField.text = ""
        descriptionField.text = ""
        visibility.currentIndex = 0
    }
    onOpened: nameField.forceActiveFocus()

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        SegmentedControl {
            id: kind
            Layout.alignment: Qt.AlignHCenter
            model: [qsTr("Local"), qsTr("Online")]
            disabledIndexes: AppController.connected ? [] : [1]
        }
        Label {
            Layout.fillWidth: true
            visible: !AppController.connected
            text: qsTr("Connect to the server to create online projects.")
            color: Theme.textSecondary
            font.pixelSize: 12
            horizontalAlignment: Text.AlignHCenter
        }
        TextField {
            id: nameField
            Layout.fillWidth: true
            placeholderText: qsTr("Name")
            selectByMouse: true
            onAccepted: if (create.enabled) create.clicked()
        }
        TextField {
            id: descriptionField
            Layout.fillWidth: true
            placeholderText: qsTr("Description (optional)")
            selectByMouse: true
            onAccepted: if (create.enabled) create.clicked()
        }
        ColumnLayout {
            Layout.fillWidth: true
            visible: dialog.online
            spacing: 6
            Label {
                text: qsTr("Visibility")
                color: Theme.textSecondary
            }
            SegmentedControl {
                id: visibility
                model: [qsTr("Private"), qsTr("Internal"), qsTr("Public")]
            }
        }
    }

    footer: DialogButtonBox {
        Button {
            text: qsTr("Cancel")
            flat: true
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
        Button {
            id: create
            text: qsTr("Create")
            highlighted: true
            enabled: nameField.text.trim().length > 0
            icon.source: Theme.iconNewProject
            icon.color: "transparent"
            icon.width: 18
            icon.height: 18
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
        }
    }

    onAccepted: {
        const name = nameField.text.trim()
        const description = descriptionField.text.trim()
        if (online)
            AppController.createOnlineProject(name, description, ["Private", "Internal", "Public"][visibility.currentIndex])
        else
            AppController.createLocalProject(name, description)
    }
}
