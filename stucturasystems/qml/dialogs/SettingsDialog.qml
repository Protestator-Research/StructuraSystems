import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts
import StructuraSystems

// Application settings: working directory, server connection and appearance.
Dialog {
    id: dialog

    readonly property var settings: AppController.settings

    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay
    modal: true
    title: qsTr("Settings")
    width: 520

    onAboutToShow: {
        directory.text = settings.workingDirectory
        server.text = settings.serverPath
        username.text = settings.username
        password.text = settings.password
        theme.currentIndex = settings.themeMode
    }

    FolderDialog {
        id: folderDialog
        title: qsTr("Choose the working directory")
        currentFolder: dialog.settings.workingDirectoryUrl
        onAccepted: {
            const path = decodeURIComponent(selectedFolder.toString())
            directory.text = path.replace(Qt.platform.os === "windows" ? /^file:\/\/\// : /^file:\/\//, "")
        }
    }

    component SectionHeader: RowLayout {
        property alias text: label.text
        property url iconSource

        Layout.fillWidth: true
        Layout.topMargin: 4
        spacing: 8

        Icon { size: 20; source: parent.iconSource }
        Label {
            id: label
            font.pixelSize: 15
            font.weight: Font.Medium
            color: Theme.textPrimary
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.outline
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        SectionHeader { text: qsTr("General"); iconSource: Theme.iconFolder }
        RowLayout {
            Layout.fillWidth: true
            TextField {
                id: directory
                Layout.fillWidth: true
                placeholderText: qsTr("Working directory")
                selectByMouse: true
            }
            IconToolButton {
                iconSource: Theme.iconFolder
                tip: qsTr("Browse...")
                onClicked: folderDialog.open()
            }
        }

        SectionHeader { text: qsTr("Connection"); iconSource: Theme.iconConnection }
        TextField {
            id: server
            Layout.fillWidth: true
            placeholderText: qsTr("Server address")
            selectByMouse: true
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            TextField {
                id: username
                Layout.fillWidth: true
                placeholderText: qsTr("Username")
                selectByMouse: true
            }
            TextField {
                id: password
                Layout.fillWidth: true
                placeholderText: qsTr("Password")
                echoMode: TextInput.Password
                selectByMouse: true
            }
        }

        SectionHeader { text: qsTr("Appearance"); iconSource: Theme.iconTheme }
        SegmentedControl {
            id: theme
            model: [qsTr("System"), qsTr("Light"), qsTr("Dark")]
        }
    }

    footer: DialogFooter {
        Button {
            text: qsTr("Cancel")
            flat: true
            onClicked: dialog.reject()
        }
        Button {
            text: qsTr("Save")
            highlighted: true
            icon.source: Theme.iconSave
            icon.color: "transparent"
            icon.width: 18
            icon.height: 18
            onClicked: dialog.accept()
        }
    }

    onAccepted: {
        settings.workingDirectory = directory.text.trim()
        settings.serverPath = server.text.trim()
        settings.username = username.text.trim()
        settings.password = password.text
        settings.themeMode = theme.currentIndex
        settings.save()
    }
}
