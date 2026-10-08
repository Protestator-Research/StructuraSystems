import QtQuick

// Right-aligned line numbers for a TextEdit/TextArea. Only logical lines (separated by '\n') are numbered; with
// word wrap each number sits next to the first visual line of its paragraph.
Item {
    id: gutter

    /** The TextEdit or TextArea (same parent and y-origin as the gutter) to number. */
    required property Item target

    readonly property var _lineStarts: {
        const text = target.text;
        const starts = [0];
        for (let i = text.indexOf("\n"); i >= 0; i = text.indexOf("\n", i + 1))
            starts.push(i + 1);
        return starts;
    }

    implicitWidth: metrics.advanceWidth("0") * Math.max(2, String(_lineStarts.length).length) + 12
    clip: true

    FontMetrics {
        id: metrics
        font: gutter.target.font
    }

    Repeater {
        model: gutter._lineStarts.length

        Text {
            required property int index

            width: gutter.width - 12
            // contentHeight and width are read so the position is re-evaluated whenever the wrapping changes.
            y: (gutter.target.contentHeight, gutter.target.width, gutter.target.positionToRectangle(gutter._lineStarts[index]).y)
            horizontalAlignment: Text.AlignRight
            text: index + 1
            font: gutter.target.font
            color: Theme.textSecondary
            opacity: 0.7
        }
    }
}
