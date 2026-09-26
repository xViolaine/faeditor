import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import FAEditor

ApplicationWindow {
    id: root
    width: 1440
    height: 900
    visible: true
    title: "FA Editor — " + (App.fantomDevice ? (App.scene.name || "Scene") : (App.studioSet.name || "Studio Set"))
    color: LogicTheme.windowBg

    readonly property bool _editingText: {
        const f = activeFocusItem
        if (!f)
            return false
        if (f instanceof TextInput || f instanceof TextEdit)
            return true
        if (f.contentItem && (f.contentItem instanceof TextInput || f.contentItem instanceof TextEdit))
            return true
        return false
    }

    function clearTextFocus() {
        // StackLayout keeps hidden pages alive — search fields can keep focus and
        // block digit tab shortcuts. Park focus on the tab bar instead.
        tabs.forceActiveFocus()
    }

    property bool mixerTonePickerVisible: false

    Connections {
        target: App
        function onMainTabChanged() {
            root.clearTextFocus()
            if (App.mainTab !== 1)
                root.mixerTonePickerVisible = false
        }
    }

    menuBar: MenuBar {
        Menu {
            title: "File"
            Action { text: "Save Local Copy"; shortcut: "Ctrl+S"; onTriggered: App.saveToLibrary() }
            Action { text: "Save As…"; onTriggered: App.library.saveAs(App.studioSet.name) }
            Action { text: "Refresh Library"; onTriggered: App.library.refresh() }
            MenuSeparator {}
            Action { text: "Import Roland SVD…"; onTriggered: svdFileDialog.open() }
            Action { text: "Reorder User Studio Sets (SVD Backup)…"; onTriggered: studioSetReorder.open() }
            MenuSeparator {}
            Action { text: "Export Current Studio Set as MIDI…"; onTriggered: studioSetMidiDialog.open() }
            Action { text: "Export FA Tone Names (.midnam)…"; onTriggered: midnamDialog.open() }
            MenuSeparator {}
            Action { text: "Quit"; shortcut: "Ctrl+Q"; onTriggered: Qt.quit() }
        }
        Menu {
            title: "MIDI"
            Action { text: "Connect…"; onTriggered: App.openMidiDialog() }
            Action { text: "Auto-connect instrument"; onTriggered: App.startupConnect() }
            Action { text: "Disconnect"; onTriggered: App.midi.disconnectDevice() }
            MenuSeparator {}
            Action { text: "Pull Temporary"; shortcut: "Ctrl+R"; onTriggered: App.pull() }
            Action { text: "Push Temporary"; shortcut: "Ctrl+P"; onTriggered: App.push() }
        }
        Menu {
            title: "Help"
            Action { text: "FA Editor Help"; shortcut: "Ctrl+/"; onTriggered: helpDialog.open() }
            MenuSeparator {}
            Action { text: "About FA Editor"; onTriggered: aboutDialog.open() }
        }
    }

    header: Toolbar {
        onConnectClicked: App.openMidiDialog()
        onPullClicked: App.pull()
        onPushClicked: App.push()
    }

    footer: StatusBar {}

    FileDialog {
        id: svdFileDialog
        title: "Import Roland FA Backup"
        nameFilters: ["Roland backup (*.SVD *.svd)", "All files (*)"]
        fileMode: FileDialog.OpenFile
        onAccepted: {
            if (App.svdImport.loadFile(selectedFile)) {
                changeToneView.svdImportVisible = true
                App.mainTab = 0
            } else
                svdImportError.open()
        }
    }

    FileDialog {
        id: studioSetMidiDialog
        title: "Export Current Studio Set as MIDI"
        nameFilters: ["Standard MIDI file (*.mid)"]
        fileMode: FileDialog.SaveFile
        defaultSuffix: "mid"
        currentFile: App.studioSet.name + ".mid"
        onAccepted: App.exportStudioSetMidi(selectedFile)
    }

    FileDialog {
        id: midnamDialog
        title: "Export Roland FA Tone Names"
        nameFilters: ["MIDI Name Document (*.midnam)"]
        fileMode: FileDialog.SaveFile
        defaultSuffix: "midnam"
        currentFile: "Roland_FA.midnam"
        onAccepted: App.exportToneNamesMidnam(selectedFile)
    }

    StudioSetReorderDialog {
        id: studioSetReorder
    }

    MessageDialog {
        id: svdImportError
        title: "Could not import SVD"
        text: App.svdImport.lastError
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            height: 28
            color: LogicTheme.panelBgRaised
            Label {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                verticalAlignment: Text.AlignVCenter
                text: App.workflowHint
                color: LogicTheme.textSecondary
                font.pixelSize: LogicTheme.fontSizeSmall
                elide: Text.ElideRight
            }
        }

        TabBar {
            id: tabs
            visible: !App.fantomDevice
            Layout.fillWidth: true
            focusPolicy: Qt.StrongFocus
            TabButton { text: "1. Sets & Tones"; width: implicitWidth }
            TabButton { text: "2. Mixer"; width: implicitWidth }
            TabButton { text: "3. Effects"; width: implicitWidth }
            TabButton { text: "4. Tone"; width: implicitWidth }
            currentIndex: App.mainTab
            onCurrentIndexChanged: {
                App.mainTab = currentIndex
                root.clearTextFocus()
            }
        }

        // Digits 1–4 switch tabs (skipped while a text field has focus)
        Shortcut { sequence: "1"; enabled: !root._editingText; onActivated: { App.mainTab = 0; root.clearTextFocus() } }
        Shortcut { sequence: "2"; enabled: !root._editingText; onActivated: { App.mainTab = 1; root.clearTextFocus() } }
        Shortcut { sequence: "3"; enabled: !root._editingText; onActivated: { App.mainTab = 2; root.clearTextFocus() } }
        Shortcut { sequence: "4"; enabled: !root._editingText; onActivated: { App.mainTab = 3; root.clearTextFocus() } }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
            visible: !App.fantomDevice

            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: App.mainTab

                ChangeToneView {
                    id: changeToneView
                    onBrowseSvdRequested: svdFileDialog.open()
                }
                MixerView {}
                EffectsEditView {}
                ToneEditView {}
            }

            Rectangle {
                Layout.preferredWidth: LogicTheme.inspectorWidth
                Layout.fillHeight: true
                color: LogicTheme.panelBg
                // Part inspector on Mixer only — Sets & Tones uses full width (toolbar + parts + tones)
                visible: App.mainTab === 1
                clip: true

                // Tone selector sits underneath; part header / Tone row slides away to reveal it
                ToneBrowser {
                    id: mixerToneBrowser
                    anchors.fill: parent
                    compact: true
                    opacity: root.mixerTonePickerVisible ? 1 : 0
                    enabled: root.mixerTonePickerVisible
                    z: 0
                    onCloseRequested: root.mixerTonePickerVisible = false

                    Behavior on opacity {
                        NumberAnimation { duration: 160; easing.type: Easing.OutCubic }
                    }
                }

                // Part N + Tone row + details — leave upward when picker opens
                ColumnLayout {
                    id: mixerPartDetails
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 8
                    z: 1
                    enabled: !root.mixerTonePickerVisible

                    property real leaveY: root.mixerTonePickerVisible ? -Math.max(height, 1) : 0
                    opacity: root.mixerTonePickerVisible ? 0 : 1
                    transform: Translate { y: mixerPartDetails.leaveY }

                    Behavior on leaveY {
                        NumberAnimation { duration: 220; easing.type: Easing.InOutCubic }
                    }
                    Behavior on opacity {
                        NumberAnimation { duration: 160; easing.type: Easing.OutCubic }
                    }

                    Label {
                        text: "Part " + (App.studioSet.selectedPart + 1)
                        color: LogicTheme.textPrimary
                        font.pixelSize: LogicTheme.fontSizeTitle
                        font.bold: true
                    }

                    PartInspector {
                        id: partInspector
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        tonePickerOpen: root.mixerTonePickerVisible
                        onTonePickerRequested: root.mixerTonePickerVisible = !root.mixerTonePickerVisible
                    }

                    ZoneKeyboard {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 90
                    }
                }
            }
        }

        FantomSceneView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: App.fantomDevice
        }
    }

    MidiConnectDialog {
        id: midiDialog
    }

    HelpDialog {
        id: helpDialog
    }

    AboutDialog {
        id: aboutDialog
        appVersion: Qt.application.version
        githubUrl: "https://github.com/teholapp/faeditor"
    }
}
