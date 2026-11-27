import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Rectangle {
    id: overlay
    anchors.fill: parent
    color: "#cc09090b"
    z: 50

    property var shortcutsList: [
        { keys: "O / Ctrl+O", label: qsTr("Add files") },
        { keys: "D", label: qsTr("Choose output folder") },
        { keys: "Space", label: qsTr("Convert or cancel") },
        { keys: "↑ / ↓", label: qsTr("Select file") },
        { keys: "← / →", label: qsTr("Cycle target format") },
        { keys: "Del", label: qsTr("Remove selected") },
        { keys: "Ctrl+Z / Ctrl+Shift+Z", label: qsTr("Undo / Redo") },
        { keys: "C", label: qsTr("Clear queue") },
        { keys: "?", label: qsTr("Show shortcuts") },
        { keys: "Q", label: qsTr("Quit") }
    ]

    MouseArea {
        anchors.fill: parent
        onClicked: overlay.visible = false
    }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 480)
        implicitHeight: cardCol.implicitHeight + 48
        color: "#18181b"
        border.color: "#2e2e36"
        border.width: 1
        radius: theme.radius

        MouseArea { anchors.fill: parent }

        ColumnLayout {
            id: cardCol
            anchors.fill: parent
            anchors.margins: 24
            spacing: 14

            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: qsTr("Shortcuts")
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                    color: "#f4f4f5"
                    Layout.fillWidth: true
                }
                Label {
                    text: qsTr("Press ? or Esc to close")
                    font.pixelSize: 12
                    color: "#a1a1aa"
                }
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: "#27272a"
            }

            Repeater {
                model: overlay.shortcutsList
                delegate: RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    Label {
                        text: modelData.label
                        color: "#e4e4e7"
                        font.pixelSize: 13
                        Layout.fillWidth: true
                    }
                    Rectangle {
                        color: "#27272a"
                        border.color: "#3f3f46"
                        border.width: 1
                        radius: Math.min(4, theme.radius)
                        implicitWidth: keyText.implicitWidth + 14
                        implicitHeight: 24
                        Label {
                            id: keyText
                            anchors.centerIn: parent
                            text: modelData.keys
                            font.family: "monospace"
                            font.pixelSize: 11
                            color: theme.accent
                        }
                    }
                }
            }
        }
    }
}
