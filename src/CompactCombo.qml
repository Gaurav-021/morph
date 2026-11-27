import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

ComboBox {
    id: control
    implicitHeight: 32
    leftPadding: 10
    rightPadding: 26
    topPadding: 0
    bottomPadding: 0
    font.pixelSize: 12
    Material.roundedScale: theme.radius > 0 ? Material.SmallScale : Material.NotRounded

    background: Rectangle {
        radius: theme.radius
        color: control.hovered || control.down ? "#202028" : "#18181e"
        border.color: control.activeFocus || control.popup.visible ? theme.accent : "#2c2c35"
        border.width: 1
    }

    contentItem: Label {
        text: control.displayText
        font.pixelSize: control.font.pixelSize
        color: control.enabled ? "#e4e4e7" : "#71717a"
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    indicator: Label {
        x: control.mirrored ? 10 : control.width - width - 10
        y: (control.height - height) / 2
        text: "▾"
        font.pixelSize: 11
        color: control.enabled ? "#a1a1aa" : "#52525b"
    }
}
