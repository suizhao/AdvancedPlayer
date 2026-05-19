import QtQuick
import QtQuick.Controls
import "../../src/utils/Utils.js" as Utils

Item {
    id: speedControlItem
    width: 60
    height: 40

    property var speedOptions: ["0.5x", "0.75x", "1.0x", "1.25x", "1.5x", "2.0x"]
    property int currentSpeedIndex: 2  // 默认1.0x

    // 内部状态跟踪，确保平滑过渡
    property bool _buttonHovered: false
    property bool _expandAreaHovered: false
    property bool speedHovered: _buttonHovered || _expandAreaHovered

    property Timer _hoverDelayTimer: Timer {
        interval: 50  // 50ms延迟，给扩展区域的onEntered时间触发
        onTriggered: {
            // 延迟后检查：如果扩展区域也没有被悬停，才清除按钮悬停状态
            if (!_expandAreaHovered) {
                _buttonHovered = false
            }
        }
    }

    // Speed button
    Button {
        id: speedButton
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        width: 40
        height: 40
        flat: true
        text: speedOptions[currentSpeedIndex]

        contentItem: Text {
            width: parent.width
            height: parent.height
            text: parent.text
            color: "#FFFFFF"
            font.pixelSize: 14
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter

        }

        background: Rectangle {
            radius: 4
            color: "transparent"
        }

        onHoveredChanged: {
            // 用 hovered（按钮自身悬停状态）而不是 controlPanel.hovered。
            // 否则鼠标离开按钮但仍在 controlPanel 内时，会误判为“仍在按钮上”，导致展开栏不消失。
            if (hovered) {
                // 鼠标进入按钮，立即设置状态并停止延迟定时器
                _hoverDelayTimer.stop()
                _buttonHovered = true
            } else {
                // 鼠标离开按钮，延迟清除状态，给扩展区域的 onEntered 时间触发
                _hoverDelayTimer.restart()
            }
        }
    }

    // Speed expand panel (appears on hover)
    MouseArea {
        id: speedExpandArea
        anchors.bottom: speedButton.top
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: 0  // 底部与按钮顶部重合
        width: 75
        height: 170 + 17  // 增加高度以覆盖进度条（进度条高度7 + topMargin 5 + 间距5）
        hoverEnabled: true  // 始终启用悬停检测，即使不可见也能检测
        visible: speedHovered && controlPanel.opacity > 0
        acceptedButtons: Qt.LeftButton

        onEntered: {
            // 鼠标进入展开区域：立即“续命”，避免从按钮滑入面板的瞬间被 timer 收起
            _hoverDelayTimer.stop()
            _expandAreaHovered = true
            _buttonHovered = true
        }

        onExited: {
            // 只有当鼠标真正离开整个扩展区域时才清除状态
            // 检查按钮是否真的被悬停，如果没有则清除所有状态
            _expandAreaHovered = false
            if (!speedButton.hovered) {
                // 不立刻清掉按钮态，交给 timer 统一收口，避免抖动
                _hoverDelayTimer.restart()
            }
        }

        Rectangle {
            anchors.top: parent.top
            anchors.topMargin: 27  // 顶部留出空间以覆盖进度条（17像素延伸 + 10像素原间距）
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 0  // 底部与按钮顶部重合
            color: "#2A2A2A"
            radius: 6
            border.color: "#404040"
            border.width: 1

            Column {
                id: speedOptionsColumn
                anchors.fill: parent
                anchors.leftMargin: 4
                anchors.rightMargin: 4
                anchors.topMargin: 6
                anchors.bottomMargin: 1  // 最小化底部边距
                spacing: 5  // 进一步减小间距

                Repeater {
                    model: speedOptions

                    Rectangle {
                        id: speedOptionRect
                        width: speedOptionsColumn.width
                        height: 20  // 进一步减小高度
                        radius: 4
                        border.width: 1

                        // 默认状态
                        color: "transparent"
                        border.color: "transparent"

                        // 添加颜色过渡动画
                        Behavior on color {
                            ColorAnimation { duration: 150 }
                        }
                        Behavior on border.color {
                            ColorAnimation { duration: 150 }
                        }

                        // 状态管理
                        states: [
                            State {
                                name: "hovered"
                                when: speedOptionMouseArea.containsMouse && index !== speedControlItem.currentSpeedIndex
                                PropertyChanges {
                                    target: speedOptionRect
                                    color: "#404040"
                                    border.color: "transparent"
                                }
                            },
                            State {
                                name: "selected"
                                when: index === speedControlItem.currentSpeedIndex && !speedOptionMouseArea.containsMouse
                                PropertyChanges {
                                    target: speedOptionRect
                                    color: "#00A1D640"
                                    border.color: "#00A1D6"
                                }
                            },
                            State {
                                name: "selectedHovered"
                                when: speedOptionMouseArea.containsMouse && index === speedControlItem.currentSpeedIndex
                                PropertyChanges {
                                    target: speedOptionRect
                                    color: "#00A1D640"
                                    border.color: "#00A1D6"
                                }
                            }
                        ]

                        Text {
                            anchors.centerIn: parent
                            text: modelData
                            color: index === speedControlItem.currentSpeedIndex ? "#00A1D6" : "#FFFFFF"
                            font.pixelSize: 12
                            font.bold: index === speedControlItem.currentSpeedIndex
                        }

                        MouseArea {
                            id: speedOptionMouseArea
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                currentSpeedIndex = index
                                let speed = parseFloat(modelData)
                                if (player) {
                                    player.playbackSpeed = speed
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // 初始化当前倍速显示
    // 设置中的默认播放速度只影响倍速按钮的显示，不影响实际播放速度
    Component.onCompleted: {
        // 优先从player读取实际播放速度
        var initialSpeed = 1.0
        if (player && typeof player.playbackSpeed !== "undefined" && player.playbackSpeed > 0) {
            // 如果player已初始化，使用实际的播放速度
            initialSpeed = player.playbackSpeed
        } else if (settingsManager && settingsManager.defaultPlaybackSpeed > 0) {
            // 如果player未初始化，使用设置中的默认播放速度作为显示值
            // 注意：这里只更新显示，不实际设置播放速度
            initialSpeed = settingsManager.defaultPlaybackSpeed
        }
        currentSpeedIndex = Utils.playbackSpeedToIndex(initialSpeed)
    }

    // 监听播放器倍速变化（实际播放速度由倍速按钮控制）
    Connections {
        target: player
        function onPlaybackSpeedChanged() {
            if (player && typeof player.playbackSpeed !== "undefined") {
                // 播放速度改变时，同步更新显示
                currentSpeedIndex = Utils.playbackSpeedToIndex(player.playbackSpeed)
            }
        }
    }

    // 监听SettingsManager的默认播放速度变化
    // 只更新显示，不改变实际播放速度
    Connections {
        target: settingsManager
        function onSettingsChanged() {
            if (settingsManager) {
                var newDefaultSpeed = settingsManager.defaultPlaybackSpeed
                // 如果player未初始化或播放速度为0，更新显示为新的默认速度
                // 但不实际设置播放速度（实际速度由倍速按钮控制）
                if (!player || typeof player.playbackSpeed === "undefined" || player.playbackSpeed <= 0) {
                    currentSpeedIndex = Utils.playbackSpeedToIndex(newDefaultSpeed)
                }
            }
        }
    }

}

