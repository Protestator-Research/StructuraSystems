pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import StructuraSystems

// Side panel with the merged outline of all cards of a document (packages of several cards are one node).
// Expansion state: kept while the model is only updated in place (cards moved), reset to three levels when the structure
// changes (new parse, cards added or removed) and when another document is shown.
Pane {
    id: panel

    /** The merged tree of the document (DocumentModel.outline); empty before the first parse. */
    property ElementTreeModel outline

    /** Emitted with the row of the first card that contributes to the activated node. */
    signal nodeActivated(int row)
    signal closeRequested

    // Node 0 is the unnamed document root: the tree starts below it (see tree._reroot).
    onOutlineChanged: Qt.callLater(tree._reroot)

    function focusTree() {
        tree.forceActiveFocus()
    }

    function _activate(index) {
        if (!index || !index.valid || !outline)
            return
        const row = outline.data(index, ElementTreeModel.CardRowRole)
        if (row !== undefined && row >= 0)
            panel.nodeActivated(row)
    }

    padding: 0
    Material.background: Theme.panel
    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Document outline")

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: Theme.outline
    }

    Keys.onEscapePressed: panel.closeRequested()

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 1
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            Layout.leftMargin: 14
            Layout.rightMargin: 4
            spacing: 4

            Label {
                Layout.fillWidth: true
                text: qsTr("Outline")
                elide: Text.ElideRight
                font.pixelSize: 15
                font.weight: Font.Medium
                color: Theme.textPrimary
            }
            IconToolButton {
                iconSource: Theme.iconClose
                iconSize: 18
                tip: qsTr("Close outline")
                onClicked: panel.closeRequested()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.outline
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            TreeView {
                id: tree

                anchors.fill: parent
                anchors.margins: 6
                clip: true
                model: panel.outline
                boundsBehavior: Flickable.StopAtBounds
                columnWidthProvider: () => tree.width
                selectionModel: ItemSelectionModel { }
                activeFocusOnTab: true
                Accessible.role: Accessible.Tree
                Accessible.name: qsTr("Document outline")
                ScrollBar.vertical: ScrollBar { }

                // The root index is assigned directly (never bound): after a reset the same index value would not
                // re-apply through a binding, while the TreeView has dropped its own.
                function _reroot() {
                    if (panel.outline && panel.outline.rowCount() > 0)
                        tree.rootIndex = panel.outline.index(0, 0)
                    else
                        tree.rootIndex = undefined
                    Qt.callLater(tree._expandInitially)
                }

                // Expands the nodes down to the third level, walking the model (independent of the rows the view has).
                function _expandInitially() {
                    const m = panel.outline
                    if (!m || m.rowCount() === 0)
                        return
                    const walk = (parent, depth) => {
                        const n = m.rowCount(parent)
                        for (let i = 0; i < n; i++) {
                            const child = m.index(i, 0, parent)
                            if (depth < 3 && m.rowCount(child) > 0) {
                                tree.expandToIndex(m.index(0, 0, child))
                                walk(child, depth + 1)
                            }
                        }
                    }
                    walk(m.index(0, 0), 1)
                }

                Keys.onReturnPressed: panel._activate(tree.selectionModel.currentIndex)
                Keys.onEnterPressed: panel._activate(tree.selectionModel.currentIndex)

                Component.onCompleted: Qt.callLater(tree._reroot)

                // A new structure (new parse, cards added or removed) replaces the nodes. Moving cards only updates
                // the nodes in place, so the expansion state stays.
                Connections {
                    target: panel.outline

                    function onModelReset() {
                        Qt.callLater(tree._reroot)
                    }
                }

                delegate: TreeViewDelegate {
                    id: node

                    required property string elementType
                    required property string name
                    required property string qualifiedName
                    required property int cardRow
                    required property var cardRows

                    readonly property int cardCount: cardRows ? cardRows.length : 0
                    readonly property string label: name.length > 0 ? name : elementType
                    readonly property string cardList: {
                        const numbers = []
                        for (let i = 0; i < cardCount; i++)
                            numbers.push(cardRows[i] + 1)
                        return numbers.join(", ")
                    }

                    implicitHeight: 30
                    indentation: 16
                    // A node without a card is no jump target: it keeps expanding and keyboard selection, but shows no
                    // hover, pressed or pointing-hand feedback of a link.
                    hoverEnabled: cardRow >= 0
                    down: cardRow >= 0 && pressed

                    Accessible.role: Accessible.TreeItem
                    Accessible.name: cardCount > 1 ? qsTr("%1 %2, declared in %3 cards").arg(elementType).arg(label).arg(cardCount)
                                                   : qsTr("%1 %2").arg(elementType).arg(label)

                    HoverHandler {
                        id: nodeHover
                        cursorShape: node.cardRow >= 0 ? Qt.PointingHandCursor : Qt.ArrowCursor
                    }

                    TapHandler {
                        enabled: node.cardRow >= 0
                        onTapped: {
                            const index = node.treeView.index(node.row, 0)
                            node.treeView.selectionModel.setCurrentIndex(index, ItemSelectionModel.ClearAndSelect)
                            panel._activate(index)
                        }
                    }

                    ToolTip.visible: nodeHover.hovered
                    ToolTip.delay: 500
                    ToolTip.text: {
                        let text = node.qualifiedName.length > 0 ? node.qualifiedName : node.label
                        if (node.name.length > 0)
                            text += "\n" + node.elementType
                        if (node.cardCount > 1)
                            text += "\n" + qsTr("Declared in cards %1").arg(node.cardList)
                        else if (node.cardCount === 1)
                            text += "\n" + qsTr("Card %1").arg(node.cardRow + 1)
                        return text
                    }

                    contentItem: RowLayout {
                        spacing: 8

                        Label {
                            text: node.elementType
                            color: Theme.textSecondary
                            font.pixelSize: 11
                            Layout.maximumWidth: 110
                            elide: Text.ElideRight
                        }
                        Label {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 20
                            text: node.label
                            elide: Text.ElideRight
                            color: Theme.textPrimary
                            font.italic: node.name.length === 0
                        }
                        // Subtle marker: several cards contribute to this node.
                        Rectangle {
                            visible: node.cardCount > 1
                            implicitWidth: countLabel.implicitWidth + 10
                            implicitHeight: 16
                            radius: 8
                            color: Qt.alpha(Theme.accent, Theme.dark ? 0.22 : 0.12)
                            border.width: 1
                            border.color: Qt.alpha(Theme.accent, 0.45)

                            Label {
                                id: countLabel
                                anchors.centerIn: parent
                                text: qsTr("%1 cards").arg(node.cardCount)
                                font.pixelSize: 10
                                color: Theme.accent
                            }
                        }
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                width: parent.width - 32
                visible: tree.rows === 0
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                text: qsTr("No outline yet. Parse the document (F5) to see its packages and elements.")
                color: Theme.textSecondary
            }
        }
    }
}
