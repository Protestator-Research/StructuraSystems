import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import StructuraSystems

// Banner above the editor while an older commit is shown instead of the head of the branch.
Pane {
    id: banner

    property bool shown: false
    property string commitId
    property string branchName
    property bool busy: false
    property date commitDate

    signal returnRequested()

    function _refreshDate() {
        commitDate = AppController.commitHistory.createdOf(commitId)
    }

    onCommitIdChanged: _refreshDate()
    Component.onCompleted: _refreshDate()

    Connections {
        target: AppController.commitHistory

        function onModelReset() {
            banner._refreshDate()
        }
    }

    readonly property string dateText: Theme.formatCommitDate(commitDate)

    implicitHeight: shown ? 56 : 0
    visible: implicitHeight > 0
    clip: true
    padding: 0
    leftPadding: 16
    rightPadding: 12
    background: Rectangle {
        color: Qt.alpha(Theme.warning, Theme.dark ? 0.16 : 0.12)

        Rectangle {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: Qt.alpha(Theme.warning, 0.5)
        }
    }

    Behavior on implicitHeight {
        NumberAnimation { duration: Theme.animationDuration; easing.type: Easing.OutCubic }
    }

    contentItem: RowLayout {
        spacing: 12

        Icon {
            size: 22
            source: Theme.iconHistory
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
            color: Theme.textPrimary
            text: banner.dateText.length > 0
                  ? qsTr("Viewing commit %1 from %2 — read-only").arg(banner.commitId.substring(0, 8)).arg(banner.dateText)
                  : qsTr("Viewing commit %1 — read-only").arg(banner.commitId.substring(0, 8))
        }
        Button {
            text: banner.branchName.length > 0 ? qsTr("Back to %1 head").arg(banner.branchName) : qsTr("Back to branch head")
            highlighted: true
            enabled: !banner.busy
            icon.source: Theme.iconBack
            icon.color: "transparent"
            icon.width: 18
            icon.height: 18
            onClicked: banner.returnRequested()
        }
    }

}
