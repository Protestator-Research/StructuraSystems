pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import StructuraSystems

// Compact hint "continues A, B::C" of a card that continues packages declared first in other cards.
// Every package name is a link to the card that declares it first; further packages are reachable through a menu.
//
// Two forms, so no content is ever cut off or unreachable:
//  - full: "continues", up to maxVisible package links and "+N" for the rest;
//  - compact: one small control (arrow icon and the number of packages) that opens a menu with ALL packages.
// Three stages as the granted width shrinks: (a) full form with the complete names, (b) full form with the names
// elided (label, links at their minimum width of about four characters plus the ellipsis, and "+N" stay visible and
// clickable), (c) compact form, only below the
// minimum width of the full form. The sizes the layout works with (preferred/maximum = full form, minimum = compact
// form) and the threshold between the forms are computed from form-independent quantities, so there is no binding loop.
// The item itself keeps the width the layout grants, also in the compact form (the pill is drawn at its own width):
// shrinking the item to the pill would need a form-dependent maximumWidth, i.e. a feedback loop (compact -> narrow
// maximum -> the layout never offers enough width to leave the compact form again).
Item {
    id: control

    /** List of {name, row}: the package and the row of the card that declares it first. */
    property var packages: []
    /** Number of package names shown directly in the full form, the rest is behind the "+N" link. */
    property int maxVisible: 2

    readonly property var _shown: packages.slice(0, maxVisible)
    readonly property var _hidden: packages.slice(maxVisible)

    // Width of the full form (independent of the form that is shown) and of the compact form.
    readonly property real _fullWidth: Math.min(fullRow.implicitWidth + 14, 420)
    readonly property real _compactWidth: compactIcon.width + compactLabel.implicitWidth + 4 + 14
    // Minimum width of the full form (stage b): label, every link at its minimum width, "+N", spacings, margins.
    // Only depends on the data and the fonts, not on the current width or form.
    readonly property real _minFullWidth: {
        const items = 1 + _shown.length + (_hidden.length > 0 ? 1 : 0)
        const sum = continuesLabel.implicitWidth + _shown.length * _linkMinWidth + (_hidden.length > 0 ? moreLink.implicitWidth : 0)
        return Math.min(_fullWidth, Math.max(_compactWidth, sum + (items - 1) * 4 + 14))
    }
    // Minimum width of an elided link: about four wide characters, the ellipsis and the link padding.
    // Measured with the font of the links, independent of the names, the width and the form.
    readonly property real _linkMinWidth: Math.ceil(linkMetrics.advanceWidth) + 4
    readonly property bool _compact: width < _minFullWidth - 0.5
    // Entries of the menu: all packages in the compact form, the hidden ones in the full form.
    readonly property var _menuEntries: _compact ? packages : _hidden

    signal packageActivated(int row)

    visible: packages.length > 0
    implicitWidth: _fullWidth
    implicitHeight: 22
    // In a layout the chip gives way to the buttons next to it, down to the compact form.
    Layout.fillWidth: true
    Layout.preferredWidth: _fullWidth
    Layout.maximumWidth: _fullWidth
    Layout.minimumWidth: _compactWidth

    function _target(entry) {
        return qsTr("Go to card %1, where \"%2\" is declared first").arg(entry.row + 1).arg(entry.name)
    }

    function _openMenu(anchor) {
        moreMenu.popup(anchor, 0, anchor.height)
    }

    // The menu must not stay open (and empty or attached to the wrong control) when its content or the form changes.
    on_MenuEntriesChanged: {
        if (_menuEntries.length === 0)
            moreMenu.close()
    }
    on_CompactChanged: {
        moreMenu.close()
        // A control of the old form may have the keyboard focus although it is no longer shown: hand it over.
        Qt.callLater(control._moveFocusToCurrentForm)
    }

    // Focus is only moved when it is on a control of this chip that is not part of the shown form any more;
    // a chip without focus never grabs it.
    function _moveFocusToCurrentForm() {
        const f = Window.window ? Window.window.activeFocusItem : null
        let inside = false
        for (let it = f; it; it = it.parent) {
            if (it === control) {
                inside = true
                break
            }
        }
        if (!inside || f === control)
            return
        // Still on a visible control of the current form: nothing to do.
        if (f.visible && (f === compactLink ? _compact : !_compact))
            return
        if (_compact) {
            compactLink.forceActiveFocus()
            return
        }
        // First link that can take focus (not visible-only: a link without a card is disabled), else "+N".
        for (let i = 0; i < linkRepeater.count; i++) {
            const l = linkRepeater.itemAt(i)
            if (l && l.visible && l.enabled) {
                l.forceActiveFocus()
                return
            }
        }
        if (moreLink.visible && moreLink.enabled) {
            moreLink.forceActiveFocus()
            return
        }
        // Nothing focusable in the full form: do not leave the focus on a hidden control.
        const next = f.nextItemInFocusChain(true)
        let nextInside = false
        for (let it = next; it; it = it.parent) {
            if (it === control)
                nextInside = true
        }
        if (next && !nextInside && next.visible)
            next.forceActiveFocus()
        else
            f.focus = false
    }

    TextMetrics {
        id: linkMetrics

        text: "wwww…"
        font.pixelSize: 11
        font.weight: Font.Medium
    }

    Rectangle {
        id: background

        width: control._compact ? control._compactWidth : control.width
        height: control.height
        radius: 11
        color: Qt.alpha(Theme.info, Theme.dark ? 0.22 : 0.12)
        border.width: 1
        border.color: Qt.alpha(Theme.info, 0.45)
    }

    // ---------------------------------------------------------------- full form
    RowLayout {
        id: fullRow

        x: 7
        width: background.width - 14
        height: control.height
        visible: !control._compact
        spacing: 4

        Label {
            id: continuesLabel

            Layout.minimumWidth: implicitWidth
            text: qsTr("continues")
            font.pixelSize: 11
            font.weight: Font.Medium
            color: Theme.info
        }

        Repeater {
            id: linkRepeater

            model: control._shown

            // One focusable link per package.
            Item {
                id: link

                required property var modelData
                required property int index

                Layout.maximumWidth: 160
                Layout.fillWidth: true
                Layout.preferredWidth: linkLabel.implicitWidth + 4
                Layout.minimumWidth: Math.min(control._linkMinWidth, linkLabel.implicitWidth + 4)
                implicitHeight: 20
                activeFocusOnTab: true
                enabled: modelData.row >= 0

                Accessible.role: Accessible.Button
                Accessible.name: control._target(modelData)
                Accessible.onPressAction: control.packageActivated(modelData.row)
                Keys.onReturnPressed: control.packageActivated(modelData.row)
                Keys.onEnterPressed: control.packageActivated(modelData.row)
                Keys.onSpacePressed: control.packageActivated(modelData.row)

                Rectangle {
                    anchors.fill: parent
                    radius: 4
                    color: "transparent"
                    border.width: link.activeFocus ? 1 : 0
                    border.color: Theme.accent
                }

                Label {
                    id: linkLabel
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    horizontalAlignment: Text.AlignLeft
                    text: link.modelData.name + (link.index < control._shown.length - 1 || control._hidden.length > 0 ? "," : "")
                    elide: Text.ElideRight
                    font.pixelSize: 11
                    font.weight: Font.Medium
                    font.underline: linkHover.hovered || link.activeFocus
                    color: Theme.info
                }
                HoverHandler {
                    id: linkHover
                    cursorShape: Qt.PointingHandCursor
                }
                TapHandler {
                    onTapped: control.packageActivated(link.modelData.row)
                }
                ToolTip.visible: linkHover.hovered
                ToolTip.text: control._target(link.modelData)
                ToolTip.delay: 500
            }
        }

        Item {
            id: moreLink

            visible: control._hidden.length > 0
            Layout.minimumWidth: implicitWidth
            implicitWidth: moreLabel.implicitWidth + 4
            implicitHeight: 20
            activeFocusOnTab: true

            Accessible.role: Accessible.Button
            Accessible.name: qsTr("Show %1 more continued packages").arg(control._hidden.length)
            Accessible.onPressAction: control._openMenu(moreLink)
            Keys.onReturnPressed: control._openMenu(moreLink)
            Keys.onEnterPressed: control._openMenu(moreLink)
            Keys.onSpacePressed: control._openMenu(moreLink)

            Rectangle {
                anchors.fill: parent
                radius: 4
                color: "transparent"
                border.width: moreLink.activeFocus ? 1 : 0
                border.color: Theme.accent
            }
            Label {
                id: moreLabel
                anchors.centerIn: parent
                text: qsTr("+%1").arg(control._hidden.length)
                font.pixelSize: 11
                font.weight: Font.Medium
                font.underline: moreHover.hovered || moreLink.activeFocus
                color: Theme.info
            }
            HoverHandler {
                id: moreHover
                cursorShape: Qt.PointingHandCursor
            }
            TapHandler {
                onTapped: control._openMenu(moreLink)
            }
            ToolTip.visible: moreHover.hovered && !moreMenu.visible
            ToolTip.text: qsTr("Further continued packages")
            ToolTip.delay: 500
        }
    }

    // ---------------------------------------------------------------- compact form
    // One control that opens the list of all packages. Its size is measured in both forms (see _compactWidth).
    Item {
        id: compactLink

        x: 4
        y: 1
        width: control._compactWidth - 8
        height: control.height - 2
        visible: control._compact
        activeFocusOnTab: true

        Accessible.role: Accessible.Button
        Accessible.name: qsTr("Show the %1 continued packages").arg(control.packages.length)
        Accessible.onPressAction: control._openMenu(compactLink)
        Keys.onReturnPressed: control._openMenu(compactLink)
        Keys.onEnterPressed: control._openMenu(compactLink)
        Keys.onSpacePressed: control._openMenu(compactLink)

        Rectangle {
            anchors.fill: parent
            radius: 4
            color: "transparent"
            border.width: compactLink.activeFocus ? 1 : 0
            border.color: Theme.accent
        }

        Row {
            anchors.centerIn: parent
            spacing: 4

            Icon {
                id: compactIcon
                size: 14
                source: Theme.iconBranch
                anchors.verticalCenter: parent.verticalCenter
            }
            Label {
                id: compactLabel
                text: control.packages.length
                font.pixelSize: 11
                font.weight: Font.Medium
                font.underline: compactHover.hovered || compactLink.activeFocus
                color: Theme.info
                anchors.verticalCenter: parent.verticalCenter
            }
        }
        HoverHandler {
            id: compactHover
            cursorShape: Qt.PointingHandCursor
        }
        TapHandler {
            onTapped: control._openMenu(compactLink)
        }
        ToolTip.visible: compactHover.hovered && !moreMenu.visible
        ToolTip.text: control.packages.length === 1 ? qsTr("This card continues a package declared in another card")
                                                    : qsTr("This card continues %1 packages declared in other cards").arg(control.packages.length)
        ToolTip.delay: 500
    }

    Menu {
        id: moreMenu

        Repeater {
            model: control._menuEntries

            MenuItem {
                required property var modelData
                text: qsTr("%1 (card %2)").arg(modelData.name).arg(modelData.row + 1)
                enabled: modelData.row >= 0
                onTriggered: control.packageActivated(modelData.row)
            }
        }

        // The form may have changed while the menu was open; the focus then returns to the (hidden) control that
        // opened it, so it has to be handed over once the menu is gone.
        onClosed: Qt.callLater(control._moveFocusToCurrentForm)
    }
}
