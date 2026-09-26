import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import FAEditor

// Reorder User Studio Sets inside an FA SD-card backup (.SVD).
// The FA cannot store User slots over USB, so the new order is written to a
// new backup file which is then restored on the instrument.
Dialog {
    id: root
    modal: true
    title: "Reorder User Studio Sets"
    width: parent ? Math.min(parent.width - 40, 760) : 760
    height: parent ? parent.height - 40 : 640
    parent: Overlay.overlay
    anchors.centerIn: parent
    standardButtons: Dialog.Close
    padding: 12

    readonly property var order: App.studioSetOrder
    property int selectedRow: -1

    function select(row) {
        if (row < 0 || row >= list.count)
            return
        selectedRow = row
        moveTarget.value = row + 1
        list.positionViewAtIndex(row, ListView.Contain)
    }

    function moveSelected(toRow) {
        if (selectedRow < 0 || toRow < 0 || toRow >= list.count || toRow === selectedRow)
            return
        if (order.move(selectedRow, toRow))
            select(toRow)
    }

    function openBackup() { openDialog.open() }

    FileDialog {
        id: openDialog
        title: "Open FA Backup"
        nameFilters: ["Roland FA backup (*.SVD *.svd)", "All files (*)"]
        fileMode: FileDialog.OpenFile
        onAccepted: {
            if (root.order.loadFile(selectedFile))
                root.select(0)
        }
    }

    FileDialog {
        id: saveDialog
        title: "Save Reordered Backup"
        nameFilters: ["Roland FA backup (*.SVD)"]
        fileMode: FileDialog.SaveFile
        defaultSuffix: "SVD"
        currentFile: root.order.suggestedSaveUrl
        onAccepted: {
            if (root.order.saveFile(selectedFile))
                savedInfo.open()
        }
    }

    MessageDialog {
        id: savedInfo
        title: "Reordered backup saved"
        text: root.order.statusText
        informativeText: "To apply it on the FA:\n"
            + "1. Copy the new file into the same backup folder on the SD card as your original backup.\n"
            + "2. On the FA, open the Restore function in the Utility menu and choose the new file.\n"
            + "3. Keep your original backup file: restoring it puts everything back as it was."
    }

    contentItem: ColumnLayout {
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label {
                Layout.fillWidth: true
                text: root.order.loaded
                      ? root.order.sourceName + " · " + root.order.usedCount + " of 512 User slots in use"
                      : "Open an FA backup file (.SVD) to rearrange your User Studio Sets."
                color: LogicTheme.textSecondary
                font.pixelSize: LogicTheme.fontSizeSmall
                elide: Text.ElideMiddle
            }
            Button {
                text: root.order.loaded ? "Open Another…" : "Open Backup…"
                highlighted: !root.order.loaded
                onClicked: root.openBackup()
            }
        }

        Label {
            Layout.fillWidth: true
            visible: !root.order.loaded
            wrapMode: Text.WordWrap
            color: LogicTheme.textMuted
            font.pixelSize: LogicTheme.fontSizeSmall
            text: "The FA only saves User Studio Sets from its own WRITE button, so the new order is "
                + "written to a copy of your backup. Make a fresh backup on the FA to its SD card first, "
                + "open it here, rearrange, save the copy, then restore it on the FA."
        }

        // Selected-set controls
        RowLayout {
            Layout.fillWidth: true
            visible: root.order.loaded
            spacing: 6

            Label {
                Layout.fillWidth: true
                text: root.selectedRow >= 0
                      ? "Selected: U" + String(root.selectedRow + 1).padStart(3, "0") + "  "
                        + (root.order.changedCount >= 0 ? root.order.nameAt(root.selectedRow) : "")
                      : "Select a set, or drag it by its ≡ handle."
                color: LogicTheme.textPrimary
                font.pixelSize: LogicTheme.fontSizeSmall
                elide: Text.ElideRight
            }
            Button {
                text: "▲"
                enabled: root.selectedRow > 0
                Accessible.name: "Move selected set up"
                ToolTip.visible: hovered
                ToolTip.text: "Move up (Alt+Up)"
                onClicked: root.moveSelected(root.selectedRow - 1)
            }
            Button {
                text: "▼"
                enabled: root.selectedRow >= 0 && root.selectedRow < list.count - 1
                Accessible.name: "Move selected set down"
                ToolTip.visible: hovered
                ToolTip.text: "Move down (Alt+Down)"
                onClicked: root.moveSelected(root.selectedRow + 1)
            }
            Label {
                text: "to slot"
                color: LogicTheme.textSecondary
                font.pixelSize: LogicTheme.fontSizeSmall
            }
            SpinBox {
                id: moveTarget
                from: 1
                to: 512
                editable: true
                enabled: root.selectedRow >= 0
                Layout.preferredWidth: 110
            }
            Button {
                text: "Move"
                enabled: root.selectedRow >= 0 && moveTarget.value - 1 !== root.selectedRow
                onClicked: root.moveSelected(moveTarget.value - 1)
            }
            Button {
                text: "Swap"
                enabled: root.selectedRow >= 0 && moveTarget.value - 1 !== root.selectedRow
                ToolTip.visible: hovered
                ToolTip.text: "Swap the selected set with the set in that slot"
                onClicked: {
                    const target = moveTarget.value - 1
                    if (root.order.swap(root.selectedRow, target))
                        root.select(target)
                }
            }
        }

        ListView {
            id: list
            objectName: "reorderList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.order.loaded
            clip: true
            spacing: 1
            model: root.order
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AlwaysOn }

            property int dragFrom: -1
            property int dropRow: -1
            property real autoScroll: 0

            Timer {
                interval: 30
                repeat: true
                running: list.dragFrom >= 0 && list.autoScroll !== 0
                onTriggered: {
                    list.contentY = Math.max(0, Math.min(list.contentHeight - list.height,
                                                         list.contentY + list.autoScroll))
                }
            }

            delegate: Rectangle {
                id: row
                required property int index
                required property int slot
                required property string name
                required property int originalSlot
                required property bool empty
                required property bool moved

                width: ListView.view.width - 12
                height: 28
                radius: 3
                color: index === root.selectedRow ? LogicTheme.selectedBg : LogicTheme.panelBg
                border.color: LogicTheme.hairline

                Rectangle {
                    // Drop indicator
                    visible: list.dragFrom >= 0 && list.dropRow === row.index && list.dropRow !== list.dragFrom
                    width: parent.width
                    height: 3
                    color: LogicTheme.accent
                    y: list.dropRow < list.dragFrom ? 0 : parent.height - height
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: root.select(row.index)
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 4
                    anchors.rightMargin: 8
                    spacing: 6

                    Label {
                        text: "≡"
                        color: LogicTheme.textMuted
                        font.pixelSize: LogicTheme.fontSize + 2
                        Layout.preferredWidth: 22
                        horizontalAlignment: Text.AlignHCenter

                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -4
                            cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                            preventStealing: true
                            onPressed: {
                                root.select(row.index)
                                list.dragFrom = row.index
                                list.dropRow = row.index
                            }
                            onPositionChanged: (mouse) => {
                                const inList = mapToItem(list, mouse.x, mouse.y)
                                list.autoScroll = inList.y < 32 ? -12 : (inList.y > list.height - 32 ? 12 : 0)
                                const inContent = mapToItem(list.contentItem, mouse.x, mouse.y)
                                const target = list.indexAt(10, inContent.y)
                                if (target >= 0)
                                    list.dropRow = target
                            }
                            onReleased: {
                                const from = list.dragFrom
                                const to = list.dropRow
                                list.dragFrom = -1
                                list.dropRow = -1
                                list.autoScroll = 0
                                if (from >= 0 && to >= 0 && from !== to && root.order.move(from, to))
                                    root.select(to)
                            }
                            onCanceled: {
                                list.dragFrom = -1
                                list.dropRow = -1
                                list.autoScroll = 0
                            }
                        }
                    }
                    Label {
                        text: "U" + String(row.slot).padStart(3, "0")
                        color: LogicTheme.textSecondary
                        font.pixelSize: LogicTheme.fontSizeSmall
                        font.family: "monospace"
                        Layout.preferredWidth: 44
                    }
                    Label {
                        text: row.name
                        color: row.empty ? LogicTheme.textMuted : LogicTheme.textPrimary
                        font.pixelSize: LogicTheme.fontSizeSmall
                        font.italic: row.empty
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }
                    Label {
                        visible: row.moved
                        text: "was U" + String(row.originalSlot).padStart(3, "0")
                        color: LogicTheme.accent
                        font.pixelSize: LogicTheme.fontSizeSmall
                    }
                }
            }
        }

        Label {
            Layout.fillWidth: true
            visible: root.order.lastError.length > 0
            text: root.order.lastError
            color: LogicTheme.danger
            wrapMode: Text.WordWrap
            font.pixelSize: LogicTheme.fontSizeSmall
        }

        RowLayout {
            Layout.fillWidth: true
            visible: root.order.loaded
            spacing: 6
            Button {
                text: "Used Sets to Top"
                ToolTip.visible: hovered
                ToolTip.text: "Close the gaps: move every non-empty set up, keeping their order"
                onClicked: { root.order.packUsedToTop(); root.select(0) }
            }
            Button {
                text: "Undo All"
                enabled: root.order.changedCount > 0
                onClicked: { root.order.reset(); root.select(0) }
            }
            Label {
                Layout.fillWidth: true
                text: root.order.changedCount > 0
                      ? root.order.changedCount + " slot(s) will change"
                      : root.order.statusText
                color: LogicTheme.textSecondary
                font.pixelSize: LogicTheme.fontSizeSmall
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignRight
            }
            Button {
                text: "Save Reordered Backup…"
                highlighted: true
                enabled: root.order.changedCount > 0
                onClicked: saveDialog.open()
            }
        }
    }

    Shortcut {
        sequence: "Alt+Up"
        enabled: root.visible
        onActivated: root.moveSelected(root.selectedRow - 1)
    }
    Shortcut {
        sequence: "Alt+Down"
        enabled: root.visible
        onActivated: root.moveSelected(root.selectedRow + 1)
    }
    Shortcut {
        sequence: "Up"
        enabled: root.visible && !moveTarget.activeFocus
        onActivated: root.select(root.selectedRow - 1)
    }
    Shortcut {
        sequence: "Down"
        enabled: root.visible && !moveTarget.activeFocus
        onActivated: root.select(root.selectedRow + 1)
    }
}
