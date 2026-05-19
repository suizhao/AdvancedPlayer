import QtQuick
import QtQuick.Controls
import "../../src/utils/Utils.js" as Utils

Rectangle {
    id: progressBar
    height: 6
    color: "transparent"

    property bool seeking: false
    property real seekTargetTime: -1  // 保存seek的目标时间，用于检测位置是否已更新
    // 始终绑定到player.position，确保进度条实时跟随播放进度
    property real position: player ? player.position : 0
    property real duration: player ? player.duration : 0

    property real previewPosition: -1 // 拖动时的预览位置（仅在拖动时使用）
    
    Rectangle {
        id: progressBackground
        anchors.fill: parent
        radius: 3
        color: "#404040"
        
        // Played portion
        Rectangle {
            id: progressFill
            // 在拖动或seek时显示预览位置，否则显示实际播放位置
            width: duration > 0 ? (((progressMouseArea.pressed || seeking) && previewPosition >= 0) ? 
                                   (previewPosition / duration) * parent.width : 
                                   (position / duration) * parent.width) : 0
            height: parent.height
            radius: 3
            color: "#00A1D6"
            
            Behavior on width {
                enabled: !seeking && !progressMouseArea.pressed
                NumberAnimation { duration: 50 }
            }
        }
        
        // Drag handle
        Rectangle {
            id: seekHandle
            width: 16
            height: 16
            radius: 8
            color: progressMouseArea.pressed ? "#FFFFFF" : "#00A1D6"
            border.color: "#FFFFFF"
            border.width: 2
            x: Math.max(0, Math.min(progressBackground.width - width, progressFill.width - width / 2))
            y: (parent.height - height) / 2
            visible: progressMouseArea.containsMouse || progressMouseArea.pressed
            
            Behavior on x {
                enabled: !seeking && !progressMouseArea.pressed
                NumberAnimation { duration: 50 }
            }
            
            // Scale effect when dragging
            scale: progressMouseArea.pressed ? 1.2 : 1.0
            Behavior on scale {
                NumberAnimation { duration: 100 }
            }
        }
        
        // Time tooltip
        Rectangle {
            id: timeTooltip
            x: Math.max(10, Math.min(progressBackground.width - width - 10,
                                      progressMouseArea.mouseX - width / 2))
            y: -height - 10
            width: timeText.width + 20
            height: 30
            radius: 5
            color: "#E0000000"
            visible: progressMouseArea.containsMouse
            
            Text {
                id: timeText
                anchors.centerIn: parent
                // 在拖动时显示预览位置，否则显示鼠标位置或当前播放位置
                text: Utils.formatTime(progressMouseArea.pressed && previewPosition >= 0 ? 
                               previewPosition : 
                               (progressMouseArea.containsMouse ? getTimeAtPosition(progressMouseArea.mouseX) : position))
                color: "#FFFFFF"
                font.pixelSize: 12
            }
        }
        
        // Mouse interaction
        MouseArea {
            id: progressMouseArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            
            property bool wasDragging: false
            
            onPressed: (mouse) => {
                // 停止可能正在运行的Timer
                seekAnimationDelayTimer.stop()
                seeking = true
                wasDragging = false
                // 清除之前的seek目标时间
                seekTargetTime = -1
                // 设置预览位置，用于拖动时显示
                previewPosition = getTimeAtPosition(mouse.x)
            }
            
            onPositionChanged: (mouse) => {
                if (pressed) {
                    wasDragging = true
                    // 更新预览位置，用于拖动时显示
                    previewPosition = getTimeAtPosition(mouse.x)
                }
            }
            
            onReleased: (mouse) => {
                const targetTime = getTimeAtPosition(mouse.x)
                if (player && duration > 0) {
                    // 记录目标时间，用于等待 position 回调对齐后再退出 seeking 动画态
                    seekTargetTime = targetTime
                    player.position = targetTime
                    seekAnimationDelayTimer.restart()
                }
                seeking = false
                previewPosition = -1
                wasDragging = false
            }
            
            // 点击事件已由onReleased处理，不需要单独的onClicked
            // 这样避免了双重seek调用
        }
    }
    
    // Functions
    function getTimeAtPosition(x) {
        let percentage = Math.max(0, Math.min(1, x / progressBackground.width))
        return percentage * duration
    }

    // Timer用于在seek后延迟恢复动画，确保位置立即更新
    Timer {
        id: seekAnimationDelayTimer
        interval: 150  // 增加延迟时间，确保position更新完成
        onTriggered: {
            // 如果position已经更新到接近目标位置，清除预览位置
            if (seekTargetTime >= 0 && Math.abs(position - seekTargetTime) < 0.5) {
                previewPosition = -1
                seekTargetTime = -1
                seeking = false
            } else if (seekTargetTime >= 0) {
                // 如果position还没更新，也清除预览位置（可能seek失败或需要更长时间）
                previewPosition = -1
                seekTargetTime = -1
                seeking = false
            } else {
                // 如果没有目标时间，直接清除seeking状态
                seeking = false
            }
        }
    }

    // 监听播放位置变化，确保进度条实时跟随
    Connections {
        target: player
        function onPositionChanged() {
            // 如果正在seek，且position已经更新到接近目标位置，清除预览位置
            if (seeking && seekTargetTime >= 0) {
                if (Math.abs(position - seekTargetTime) < 0.5) {
                    previewPosition = -1
                    seekTargetTime = -1
                    // 延迟清除seeking状态，确保进度条平滑过渡
                    seekAnimationDelayTimer.restart()
                }
            } else if (!progressMouseArea.pressed && previewPosition >= 0) {
                // 当播放位置更新时，如果不在拖动状态，清除预览位置
                previewPosition = -1
            }
        }
    }

    // Increase height on mouse hover
    states: [
        State {
            name: "hovered"
            when: progressMouseArea.containsMouse || progressMouseArea.pressed
            PropertyChanges {
                target: progressBar
                height: 10
            }
            PropertyChanges {
                target: progressBackground
                radius: 5
            }
            PropertyChanges {
                target: progressFill
                radius: 5
            }
        }
    ]
    
    transitions: [
        Transition {
            NumberAnimation {
                properties: "height,radius"
                duration: 150
            }
        }
    ]
    
}

