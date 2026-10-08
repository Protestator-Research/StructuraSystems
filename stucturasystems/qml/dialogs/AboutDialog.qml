import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import StructuraSystems

// Application name, version, organisation, license and icon credit.
Dialog {
    id: dialog

    component LinkLabel: Label {
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        textFormat: Text.StyledText
        linkColor: Theme.dark ? "#7fd1ff" : "#0b5c8c"
        color: Theme.textPrimary
        onLinkActivated: link => Qt.openUrlExternally(link)

        HoverHandler {
            cursorShape: parent.hoveredLink.length > 0 ? Qt.PointingHandCursor : Qt.ArrowCursor
        }
    }

    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay
    modal: true
    title: qsTr("About")
    width: 460

    ColumnLayout {
        anchors.fill: parent
        spacing: 16

        RowLayout {
            Layout.fillWidth: true
            spacing: 16

            Rectangle {
                implicitWidth: 64
                implicitHeight: 64
                radius: 16
                color: Theme.indicator

                Icon {
                    anchors.centerIn: parent
                    size: 40
                    source: Theme.iconTwin
                }
            }
            ColumnLayout {
                spacing: 2

                Label {
                    text: Qt.application.name
                    font.pixelSize: 22
                    font.weight: Font.Medium
                    color: Theme.textPrimary
                }
                Label {
                    text: qsTr("Version %1").arg(Qt.application.version)
                    color: Theme.textSecondary
                }
            }
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.textSecondary
            text: qsTr("A test platform for the SysML v2 library and for developing digital twins.")
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.outline
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 16
            rowSpacing: 8

            Label {
                Layout.alignment: Qt.AlignTop
                text: qsTr("Developed by")
                color: Theme.textSecondary
            }
            LinkLabel {
                text: qsTr("Protestator-Research<br>"
                           + "<a href=\"mailto:info@protestator-research.com\">info@protestator-research.com</a><br>"
                           + "<a href=\"https://protestator-research.com/\">https://protestator-research.com/</a>")
            }

            Label {
                Layout.alignment: Qt.AlignTop
                text: qsTr("License")
                color: Theme.textSecondary
            }
            LinkLabel {
                text: qsTr("GNU General Public License v3 "
                           + "(<a href=\"https://www.gnu.org/licenses/gpl-3.0.html\">gnu.org/licenses/gpl-3.0</a>)")
            }

            Label {
                Layout.alignment: Qt.AlignTop
                text: qsTr("Icons")
                color: Theme.textSecondary
            }
            LinkLabel {
                text: qsTr("Icons by <a href=\"https://www.flaticon.com/de/autoren/flatart-icons\">flatart_icons</a> "
                           + "from flaticon.com")
            }
        }
    }

    footer: DialogFooter {
        Button {
            text: qsTr("Close")
            highlighted: true
            onClicked: dialog.close()
        }
    }
}
