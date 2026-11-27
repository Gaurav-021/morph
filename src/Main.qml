import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 960
    height: 680
    minimumWidth: 680
    minimumHeight: 480
    visible: true
    title: "Morph"
    color: "#0e0e10"

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius > 0 ? Material.SmallScale : Material.NotRounded

    property bool helpVisible: false
    property bool quitPromptVisible: false
    property bool dropActive: false

    function formatSize(bytes) {
        // Read translator.language so bindings re-evaluate on live language switch.
        translator.language
        return backend.formatBytes(bytes)
    }

    function requestQuit() {
        if (backend.busy)
            win.quitPromptVisible = true
        else
            Qt.quit()
    }

    onClosing: function(close) {
        if (backend.busy) {
            close.accepted = false
            win.quitPromptVisible = true
        }
    }

    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut {
        sequence: "Escape"
        context: Qt.ApplicationShortcut
        onActivated: {
            if (win.quitPromptVisible) win.quitPromptVisible = false
            else if (win.helpVisible) win.helpVisible = false
            else if (backend.errorMessage.length > 0) backend.dismissError()
        }
    }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; onActivated: win.requestQuit() }
    Shortcut { sequence: "Ctrl+Q"; context: Qt.ApplicationShortcut; onActivated: win.requestQuit() }
    Shortcut { sequence: "O"; context: Qt.ApplicationShortcut; onActivated: backend.pickFiles() }
    Shortcut { sequence: "Ctrl+O"; context: Qt.ApplicationShortcut; onActivated: backend.pickFiles() }
    Shortcut { sequence: "D"; context: Qt.ApplicationShortcut; onActivated: backend.pickOutputDirectory() }
    Shortcut { sequence: "Space"; context: Qt.ApplicationShortcut; onActivated: backend.primaryAction() }
    Shortcut { sequence: "Return"; context: Qt.ApplicationShortcut; onActivated: if (backend.count > 0) backend.primaryAction() }
    Shortcut { sequence: "Delete"; context: Qt.ApplicationShortcut; onActivated: backend.removeAt(backend.selectedIndex) }
    Shortcut { sequence: "Backspace"; context: Qt.ApplicationShortcut; onActivated: backend.removeAt(backend.selectedIndex) }
    Shortcut { sequence: "Up"; context: Qt.ApplicationShortcut; onActivated: backend.selectedIndex = Math.max(0, backend.selectedIndex - 1) }
    Shortcut { sequence: "Down"; context: Qt.ApplicationShortcut; onActivated: backend.selectedIndex = Math.min(backend.count - 1, backend.selectedIndex + 1) }
    Shortcut { sequence: "Left"; context: Qt.ApplicationShortcut; onActivated: backend.cycleTargetFormat(backend.selectedIndex, -1) }
    Shortcut { sequence: "Right"; context: Qt.ApplicationShortcut; onActivated: backend.cycleTargetFormat(backend.selectedIndex, 1) }
    Shortcut { sequence: "C"; context: Qt.ApplicationShortcut; onActivated: if (!backend.busy) backend.clearAll() }
    Shortcut { sequences: [StandardKey.Undo]; context: Qt.ApplicationShortcut; onActivated: backend.undo() }
    Shortcut { sequences: [StandardKey.Redo]; context: Qt.ApplicationShortcut; onActivated: backend.redo() }

    Item {
        id: rootMirror
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        DropArea {
            anchors.fill: parent
            onEntered: function(drag) {
                if (drag.hasUrls) {
                    win.dropActive = true
                    drag.acceptProposedAction()
                }
            }
            onExited: win.dropActive = false
            onDropped: function(drop) {
                win.dropActive = false
                if (drop.hasUrls)
                    backend.addUrls(drop.urls)
            }
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 14

            // Header bar
            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                Rectangle {
                    width: 36
                    height: 36
                    radius: Math.min(6, Math.max(0, theme.radius))
                    color: theme.accent
                    Label {
                        anchors.centerIn: parent
                        text: "M"
                        font.pixelSize: 18
                        font.weight: Font.Bold
                        color: theme.accentForeground
                    }
                }

                ColumnLayout {
                    spacing: 1
                    Label {
                        text: "Morph"
                        font.pixelSize: 18
                        font.weight: Font.DemiBold
                        color: "#f4f4f5"
                    }
                    Label {
                        text: qsTr("Local file converter")
                        font.pixelSize: 12
                        color: "#8e8e98"
                    }
                }

                Item { Layout.fillWidth: true }

                Button {
                    id: addFilesBtn
                    text: qsTr("Add files")
                    implicitHeight: 36
                    leftPadding: 14
                    rightPadding: 14
                    onClicked: backend.pickFiles()
                    background: Rectangle {
                        radius: theme.radius
                        color: addFilesBtn.down ? "#272730" : (addFilesBtn.hovered ? "#22222a" : "#1a1a20")
                        border.color: "#32323d"
                        border.width: 1
                    }
                    contentItem: Label {
                        text: addFilesBtn.text
                        font.pixelSize: 13
                        font.weight: Font.Medium
                        color: "#f4f4f5"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                Button {
                    id: convertBtn
                    text: backend.busy ? qsTr("Cancel") : qsTr("Convert")
                    enabled: backend.busy || backend.hasPending
                    implicitHeight: 36
                    leftPadding: 16
                    rightPadding: 16
                    onClicked: backend.primaryAction()
                    background: Rectangle {
                        radius: theme.radius
                        color: {
                            if (!convertBtn.enabled) return "#23232a"
                            if (backend.busy) return convertBtn.hovered ? "#471c25" : "#3b1820"
                            return convertBtn.down ? Qt.darker(theme.accent, 1.15) : theme.accent
                        }
                        border.color: backend.busy ? "#f38ba8" : "transparent"
                        border.width: backend.busy ? 1 : 0
                    }
                    contentItem: Label {
                        text: convertBtn.text
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                        color: {
                            if (!convertBtn.enabled) return "#71717a"
                            if (backend.busy) return "#fca5a5"
                            return theme.accentForeground
                        }
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                Button {
                    id: helpBtn
                    text: "?"
                    implicitWidth: 36
                    implicitHeight: 36
                    onClicked: win.helpVisible = !win.helpVisible
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Shortcuts")
                    background: Rectangle {
                        radius: theme.radius
                        color: helpBtn.hovered ? "#22222a" : "#16161b"
                        border.color: "#2c2c35"
                        border.width: 1
                    }
                    contentItem: Label {
                        text: helpBtn.text
                        font.pixelSize: 14
                        font.weight: Font.Bold
                        color: "#d4d4d8"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }

            // Missing-tools or error banner (never fail silently)
            Rectangle {
                visible: backend.missingTools.length > 0 || backend.errorMessage.length > 0
                Layout.fillWidth: true
                implicitHeight: bannerRow.implicitHeight + 16
                color: backend.errorMessage.length > 0 ? "#2c1418" : "#262112"
                border.color: backend.errorMessage.length > 0 ? "#f38ba8" : "#f9e2af"
                border.width: 1
                radius: Math.min(4, theme.radius)

                RowLayout {
                    id: bannerRow
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 10
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        font.pixelSize: 12
                        color: backend.errorMessage.length > 0 ? "#fca5a5" : "#fde68a"
                        text: backend.errorMessage.length > 0
                              ? backend.errorMessage
                              : qsTr("Missing tools: %1").arg(backend.missingTools.join(", "))
                    }
                    Button {
                        visible: backend.errorMessage.length > 0
                        text: qsTr("Dismiss")
                        flat: true
                        Material.roundedScale: theme.radius > 0 ? Material.SmallScale : Material.NotRounded
                        onClicked: backend.dismissError()
                    }
                }
            }

            // Options strip: Quality, Size, Output folder
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: optsRow.implicitHeight + 16
                color: "#151518"
                border.color: "#25252b"
                border.width: 1
                radius: theme.radius

                RowLayout {
                    id: optsRow
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 14

                    Label {
                        text: qsTr("Quality")
                        font.pixelSize: 12
                        color: "#a1a1aa"
                    }

                    ComboBox {
                        id: qualityBox
                        Layout.preferredWidth: 156
                        Material.roundedScale: theme.radius > 0 ? Material.SmallScale : Material.NotRounded
                        enabled: !backend.busy
                        model: [
                            { value: "high", label: qsTr("High") },
                            { value: "balanced", label: qsTr("Balanced") },
                            { value: "compact", label: qsTr("Compact") }
                        ]
                        textRole: "label"
                        currentIndex: backend.quality === "high" ? 0 : (backend.quality === "compact" ? 2 : 1)
                        onActivated: backend.setQuality(model[currentIndex].value)
                    }

                    Label {
                        text: qsTr("Size")
                        font.pixelSize: 12
                        color: "#a1a1aa"
                    }

                    ComboBox {
                        id: scaleBox
                        Layout.preferredWidth: 155
                        Material.roundedScale: theme.radius > 0 ? Material.SmallScale : Material.NotRounded
                        enabled: !backend.busy
                        model: [
                            { value: "original", label: qsTr("Original") },
                            { value: "large", label: qsTr("1080p / 1920px") },
                            { value: "medium", label: qsTr("720p / 1280px") },
                            { value: "small", label: qsTr("480p / 800px") }
                        ]
                        textRole: "label"
                        currentIndex: {
                            if (backend.scale === "large") return 1
                            if (backend.scale === "medium") return 2
                            if (backend.scale === "small") return 3
                            return 0
                        }
                        onActivated: backend.setScale(model[currentIndex].value)
                    }

                    Item { Layout.fillWidth: true }

                    Label {
                        text: qsTr("Output")
                        font.pixelSize: 12
                        color: "#a1a1aa"
                    }

                    Button {
                        id: outDirBtn
                        text: backend.outputDirectory.length > 0
                              ? backend.outputDirectory.split("/").pop() || backend.outputDirectory
                              : qsTr("Same folder")
                        implicitHeight: 34
                        leftPadding: 12
                        rightPadding: 12
                        enabled: !backend.busy
                        onClicked: backend.pickOutputDirectory()
                        ToolTip.visible: hovered
                        ToolTip.text: backend.outputDirectory.length > 0
                                      ? backend.outputDirectory
                                      : qsTr("Choose folder")
                        background: Rectangle {
                            radius: theme.radius
                            color: outDirBtn.hovered ? "#202028" : "#18181e"
                            border.color: "#2c2c35"
                            border.width: 1
                        }
                        contentItem: Label {
                            text: outDirBtn.text
                            font.pixelSize: 12
                            color: "#e4e4e7"
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }

                    ToolButton {
                        visible: backend.outputDirectory.length > 0
                        text: "×"
                        enabled: !backend.busy
                        onClicked: backend.clearOutputDirectory()
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Reset to same folder")
                    }
                }
            }

            // Main queue / empty drop zone
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: win.dropActive ? "#191b22" : "#121215"
                border.color: win.dropActive ? theme.accent : "#25252b"
                border.width: win.dropActive ? 2 : 1
                radius: theme.radius
                clip: true

                // Empty state
                ColumnLayout {
                    visible: backend.count === 0
                    anchors.centerIn: parent
                    width: Math.min(parent.width - 64, 480)
                    spacing: 12

                    Rectangle {
                        Layout.alignment: Qt.AlignHCenter
                        width: 56
                        height: 56
                        radius: theme.radius
                        color: "#1b1b20"
                        border.color: theme.accent
                        border.width: 1
                        Label {
                            anchors.centerIn: parent
                            text: "+"
                            font.pixelSize: 28
                            color: theme.accent
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        text: qsTr("Drop files here or press O to add")
                        font.pixelSize: 16
                        font.weight: Font.Medium
                        color: "#f4f4f5"
                    }

                    Label {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        text: qsTr("Videos, audio, images, documents, PDFs, and archives — converted locally")
                        font.pixelSize: 13
                        color: "#8e8e98"
                    }

                    Button {
                        id: emptyAddBtn
                        Layout.alignment: Qt.AlignHCenter
                        text: qsTr("Add files")
                        implicitHeight: 38
                        leftPadding: 18
                        rightPadding: 18
                        onClicked: backend.pickFiles()
                        background: Rectangle {
                            radius: theme.radius
                            color: emptyAddBtn.down ? Qt.darker(theme.accent, 1.15) : theme.accent
                        }
                        contentItem: Label {
                            text: emptyAddBtn.text
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                            color: theme.accentForeground
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }

                // Queue list
                ListView {
                    id: queueList
                    visible: backend.count > 0
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 6
                    model: backend.queue
                    currentIndex: backend.selectedIndex
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                    delegate: Item {
                        id: delegateRoot
                        width: queueList.width
                        height: rowCard.implicitHeight

                        // Capture row roles here so inner ComboBox controls do not shadow `model`.
                        readonly property int rowIndex: index
                        readonly property var rowTargets: model.availableTargets
                        readonly property string rowTargetFormat: model.targetFormat
                        readonly property string rowStatus: model.status
                        readonly property string rowCategory: model.category

                        Rectangle {
                            id: rowCard
                            width: parent.width
                            implicitHeight: rowLayout.implicitHeight + 16
                            color: delegateRoot.rowIndex === backend.selectedIndex ? "#1d1d24" : "#16161a"
                            border.color: {
                                if (delegateRoot.rowStatus === "error") return "#f38ba8"
                                if (delegateRoot.rowIndex === backend.selectedIndex) return theme.accent
                                return "#26262e"
                            }
                            border.width: 1
                            radius: Math.min(6, Math.max(0, theme.radius))

                            MouseArea {
                                anchors.fill: parent
                                preventStealing: true
                                onClicked: backend.selectedIndex = delegateRoot.rowIndex
                            }

                            ColumnLayout {
                                id: rowLayout
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 6

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 10

                                    // Category pill
                                    Rectangle {
                                        color: "#23232b"
                                        radius: Math.min(4, theme.radius)
                                        implicitWidth: catText.implicitWidth + 12
                                        implicitHeight: 22
                                        Label {
                                            id: catText
                                            anchors.centerIn: parent
                                            text: (translator.language, backend.categoryLabel(delegateRoot.rowCategory))
                                            font.pixelSize: 11
                                            font.weight: Font.Medium
                                            color: theme.accent
                                        }
                                    }

                                    // File name + meta
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 1
                                        Label {
                                            Layout.fillWidth: true
                                            elide: Text.ElideMiddle
                                            text: model.fileName
                                            font.pixelSize: 13
                                            font.weight: Font.Medium
                                            color: "#f4f4f5"
                                        }
                                        Label {
                                            Layout.fillWidth: true
                                            elide: Text.ElideMiddle
                                            font.pixelSize: 11
                                            color: delegateRoot.rowStatus === "error" ? "#fca5a5" : "#8e8e98"
                                            text: {
                                                if (delegateRoot.rowStatus === "error")
                                                    return model.error
                                                if (delegateRoot.rowStatus === "done") {
                                                    var outName = model.outputPath.split("/").pop()
                                                    return model.note.length > 0
                                                           ? outName + " · " + model.note
                                                           : outName
                                                }
                                                return "." + model.sourceExt.toUpperCase() + " · " + win.formatSize(model.fileSize)
                                            }
                                        }
                                    }

                                    Label {
                                        text: Qt.application.layoutDirection === Qt.RightToLeft ? "←" : "→"
                                        color: "#71717a"
                                        font.pixelSize: 14
                                    }

                                    ComboBox {
                                        id: targetCombo
                                        Layout.preferredWidth: 132
                                        Material.roundedScale: theme.radius > 0 ? Material.SmallScale : Material.NotRounded
                                        enabled: !backend.busy && delegateRoot.rowStatus !== "running"
                                        model: delegateRoot.rowTargets
                                        currentIndex: Math.max(0, delegateRoot.rowTargets.indexOf(delegateRoot.rowTargetFormat))
                                        displayText: "." + delegateRoot.rowTargetFormat.toUpperCase()
                                        delegate: ItemDelegate {
                                            width: targetCombo.width
                                            text: "." + modelData.toUpperCase()
                                            highlighted: targetCombo.highlightedIndex === index
                                        }
                                        onActivated: backend.setTargetFormat(delegateRoot.rowIndex, delegateRoot.rowTargets[currentIndex])
                                    }

                                    // Batch-apply format to same category
                                    ToolButton {
                                        visible: backend.count > 1 && !backend.busy
                                        text: "≡"
                                        onClicked: backend.applyFormatToCategory(delegateRoot.rowIndex)
                                        ToolTip.visible: hovered
                                        ToolTip.text: qsTr("Apply .%1 to all %2")
                                                      .arg(delegateRoot.rowTargetFormat.toUpperCase())
                                                      .arg(backend.categoryLabel(delegateRoot.rowCategory))
                                    }

                                    // Status badge / open folder
                                    Rectangle {
                                        visible: delegateRoot.rowStatus !== "pending"
                                        radius: Math.min(4, theme.radius)
                                        implicitWidth: statusLbl.implicitWidth + 12
                                        implicitHeight: 22
                                        color: {
                                            if (delegateRoot.rowStatus === "done") return "#173222"
                                            if (delegateRoot.rowStatus === "error") return "#3b1820"
                                            if (delegateRoot.rowStatus === "running") return "#292313"
                                            return "#27272a"
                                        }
                                        Label {
                                            id: statusLbl
                                            anchors.centerIn: parent
                                            font.pixelSize: 11
                                            color: {
                                                if (delegateRoot.rowStatus === "done") return "#86efac"
                                                if (delegateRoot.rowStatus === "error") return "#fca5a5"
                                                if (delegateRoot.rowStatus === "running") return theme.accent
                                                return "#a1a1aa"
                                            }
                                            text: {
                                                if (delegateRoot.rowStatus === "done") return qsTr("Done")
                                                if (delegateRoot.rowStatus === "error") return qsTr("Error")
                                                if (delegateRoot.rowStatus === "running") return qsTr("Converting")
                                                if (delegateRoot.rowStatus === "cancelled") return qsTr("Cancelled")
                                                return qsTr("Pending")
                                            }
                                        }
                                    }

                                    ToolButton {
                                        visible: delegateRoot.rowStatus === "done"
                                        text: "↗"
                                        onClicked: backend.openOutputFolder(delegateRoot.rowIndex)
                                        ToolTip.visible: hovered
                                        ToolTip.text: qsTr("Open folder")
                                    }

                                    ToolButton {
                                        enabled: !(backend.busy && delegateRoot.rowStatus === "running")
                                        text: "×"
                                        onClicked: backend.removeAt(delegateRoot.rowIndex)
                                        ToolTip.visible: hovered
                                        ToolTip.text: qsTr("Remove")
                                    }
                                }

                                ProgressBar {
                                    visible: delegateRoot.rowStatus === "running"
                                    Layout.fillWidth: true
                                    from: 0
                                    to: 1
                                    value: model.progress
                                    indeterminate: model.progress <= 0.06
                                }
                            }
                        }
                    }
                }

                // Drag overlay badge
                Rectangle {
                    visible: win.dropActive
                    anchors.centerIn: parent
                    color: "#18181b"
                    border.color: theme.accent
                    border.width: 1
                    radius: theme.radius
                    implicitWidth: dropLbl.implicitWidth + 32
                    implicitHeight: 44
                    Label {
                        id: dropLbl
                        anchors.centerIn: parent
                        text: qsTr("Drop files to add")
                        color: theme.accent
                        font.pixelSize: 14
                        font.weight: Font.Medium
                    }
                }
            }

            // Footer status & quick actions
            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Label {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    font.pixelSize: 12
                    color: "#a1a1aa"
                    text: {
                        translator.language
                        if (backend.statusMessage.length > 0)
                            return backend.statusMessage
                        return qsTr("%n files", "", backend.count)
                    }
                }

                Button {
                    visible: backend.failedCount > 0 && !backend.busy
                    text: qsTr("Retry failed")
                    flat: true
                    Material.roundedScale: theme.radius > 0 ? Material.SmallScale : Material.NotRounded
                    onClicked: backend.retryFailed()
                }

                Button {
                    visible: backend.completedCount > 0 && !backend.busy
                    text: qsTr("Clear done")
                    flat: true
                    Material.roundedScale: theme.radius > 0 ? Material.SmallScale : Material.NotRounded
                    onClicked: backend.clearCompleted()
                }

                Button {
                    visible: backend.count > 0 && !backend.busy
                    text: qsTr("Clear")
                    flat: true
                    Material.roundedScale: theme.radius > 0 ? Material.SmallScale : Material.NotRounded
                    onClicked: backend.clearAll()
                }
            }
        }

        // Quit confirmation overlay
        Rectangle {
            visible: win.quitPromptVisible
            anchors.fill: parent
            color: "#cc09090b"
            z: 60

            MouseArea { anchors.fill: parent }

            Rectangle {
                anchors.centerIn: parent
                width: Math.min(parent.width - 48, 380)
                implicitHeight: quitCol.implicitHeight + 40
                color: "#18181b"
                border.color: "#2e2e36"
                border.width: 1
                radius: theme.radius

                ColumnLayout {
                    id: quitCol
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 12

                    Label {
                        text: qsTr("Conversion in progress")
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                        color: "#f4f4f5"
                    }

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: qsTr("Stop the active conversion and quit?")
                        font.pixelSize: 13
                        color: "#a1a1aa"
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        Item { Layout.fillWidth: true }
                        Button {
                            text: qsTr("Keep running")
                            flat: true
                            Material.roundedScale: theme.radius > 0 ? Material.SmallScale : Material.NotRounded
                            onClicked: win.quitPromptVisible = false
                        }
                        Button {
                            id: confirmQuitBtn
                            text: qsTr("Quit")
                            implicitHeight: 34
                            leftPadding: 14
                            rightPadding: 14
                            onClicked: {
                                backend.cancelConversion()
                                Qt.quit()
                            }
                            background: Rectangle {
                                radius: theme.radius
                                color: "#3b1820"
                                border.color: "#f38ba8"
                                border.width: 1
                            }
                            contentItem: Label {
                                text: confirmQuitBtn.text
                                font.pixelSize: 13
                                font.weight: Font.Medium
                                color: "#fca5a5"
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }
                }
            }
        }

        ShortcutOverlay {
            id: shortcutOverlay
            visible: win.helpVisible
        }
    }
}
