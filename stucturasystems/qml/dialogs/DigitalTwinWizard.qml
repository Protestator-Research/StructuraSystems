pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import StructuraSystems

// Three step wizard: name, element selection, summary.
Dialog {
    id: wizard

    readonly property var document: AppController.currentDocument
    property int step: 0
    property int selectedCount: 0
    readonly property var stepTitles: [qsTr("Name"), qsTr("Elements"), qsTr("Summary")]
    readonly property bool canAdvance: step === 0 ? nameField.text.trim().length > 0
                                                  : selectedCount > 0 && nameField.text.trim().length > 0

    function openWizard() {
        if (document)
            document.clearSelection()
        step = 0
        selectedCount = 0
        nameField.text = ""
        open()
    }

    function _updateSelection() {
        selectedCount = document ? document.selectedElementIds().length : 0
    }

    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay
    modal: true
    title: qsTr("Create digital twin")
    width: 560
    height: 460

    onClosed: {
        if (document)
            document.clearSelection()
    }
    onStepChanged: _updateSelection()

    ColumnLayout {
        anchors.fill: parent
        spacing: 14

        // ---------------------------------------------------------------- stepper
        RowLayout {
            Layout.fillWidth: true
            spacing: 0

            Repeater {
                model: wizard.stepTitles

                RowLayout {
                    id: stepItem

                    required property int index
                    required property string modelData
                    readonly property bool reached: wizard.step >= index

                    Layout.fillWidth: index < wizard.stepTitles.length - 1
                    spacing: 8

                    Rectangle {
                        implicitWidth: 26
                        implicitHeight: 26
                        radius: 13
                        color: stepItem.reached ? Theme.accent : "transparent"
                        border.width: 1
                        border.color: stepItem.reached ? Theme.accent : Theme.outline
                        Behavior on color { ColorAnimation { duration: 150; easing.type: Easing.OutCubic } }

                        Label {
                            anchors.centerIn: parent
                            text: stepItem.index + 1
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                            color: stepItem.reached ? (Theme.dark ? "#00363a" : "white") : Theme.textSecondary
                        }
                    }
                    Label {
                        text: stepItem.modelData
                        font.weight: wizard.step === stepItem.index ? Font.DemiBold : Font.Normal
                        color: wizard.step === stepItem.index ? Theme.textPrimary : Theme.textSecondary
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.leftMargin: 6
                        Layout.rightMargin: 10
                        Layout.preferredHeight: 1
                        visible: stepItem.index < wizard.stepTitles.length - 1
                        color: wizard.step > stepItem.index ? Theme.accent : Theme.outline
                    }
                }
            }
        }

        // ---------------------------------------------------------------- pages
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: wizard.step

            ColumnLayout {
                spacing: 10

                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: Theme.textSecondary
                    text: qsTr("Give the digital twin a name. It is created from the current project.")
                }
                TextField {
                    id: nameField
                    Layout.fillWidth: true
                    placeholderText: qsTr("Name of the digital twin")
                    selectByMouse: true
                    onAccepted: if (wizard.canAdvance) wizard.step = 1
                }
                Item { Layout.fillHeight: true }
            }

            ColumnLayout {
                spacing: 8

                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: Theme.textSecondary
                    text: qsTr("Select the elements that belong to the digital twin (%1 selected).").arg(wizard.selectedCount)
                }
                Frame {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    padding: 0

                    ListView {
                        id: elements
                        anchors.fill: parent
                        clip: true
                        model: wizard.document
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar { }

                        delegate: ItemDelegate {
                            id: entry

                            required property var model
                            required property int index
                            required property string language
                            required property string body
                            required property string headerTitle
                            required property bool selected

                            width: ListView.view.width
                            height: 44
                            onClicked: box.toggle()

                            contentItem: RowLayout {
                                spacing: 10

                                CheckBox {
                                    id: box
                                    checked: entry.selected
                                    onToggled: {
                                        entry.model.selected = checked
                                        wizard._updateSelection()
                                    }
                                }
                                LanguageChip { language: entry.language }
                                Label {
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                    color: Theme.textPrimary
                                    text: {
                                        if (entry.language === "YAML" && entry.headerTitle.length > 0)
                                            return entry.headerTitle
                                        const line = entry.body.split("\n").find(l => l.trim().length > 0)
                                        return line !== undefined ? line.trim() : qsTr("(empty)")
                                    }
                                }
                            }
                        }
                    }
                }
            }

            ColumnLayout {
                spacing: 12

                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: Theme.textSecondary
                    text: qsTr("Check your input. The digital twin is created on the server when you continue.")
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 16
                    rowSpacing: 8

                    Label { text: qsTr("Name"); color: Theme.textSecondary }
                    Label { Layout.fillWidth: true; text: nameField.text.trim(); font.weight: Font.Medium; elide: Text.ElideRight }
                    Label { text: qsTr("Project"); color: Theme.textSecondary }
                    Label { Layout.fillWidth: true; text: wizard.document ? wizard.document.title : ""; elide: Text.ElideRight }
                    Label { text: qsTr("Elements"); color: Theme.textSecondary }
                    Label { text: wizard.selectedCount }
                }
                Item { Layout.fillHeight: true }
            }
        }
    }

    footer: DialogFooter {
        spread: true

        Button {
            text: qsTr("Cancel")
            flat: true
            onClicked: wizard.reject()
        }
        Item { Layout.fillWidth: true }
        Button {
            text: qsTr("Back")
            flat: true
            visible: wizard.step > 0
            icon.source: Theme.iconBack
            icon.color: "transparent"
            icon.width: 18
            icon.height: 18
            onClicked: wizard.step -= 1
        }
        Button {
            text: qsTr("Next")
            highlighted: true
            flat: false
            visible: wizard.step < 2
            enabled: wizard.canAdvance
            icon.source: Theme.iconForward
            icon.color: "transparent"
            icon.width: 18
            icon.height: 18
            onClicked: wizard.step += 1
        }
        Button {
            text: qsTr("Create")
            highlighted: true
            flat: false
            visible: wizard.step === 2
            enabled: wizard.canAdvance && !AppController.busy
            icon.source: Theme.iconTwin
            icon.color: "transparent"
            icon.width: 18
            icon.height: 18
            onClicked: wizard.accept()
        }
    }

    onAccepted: {
        if (document)
            AppController.createDigitalTwin(nameField.text.trim(), document.selectedElementIds())
    }
}
