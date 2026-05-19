import QtQuick
import QtQuick.Controls

Item {
    id: volumeControlItem
    width: 60
    height: 40

    // 内部状态跟踪，确保平滑过渡
    property bool _buttonHovered: false
    property bool _sliderAreaHovered: false
    property bool volumeHovered: _buttonHovered || _sliderAreaHovered
    property Timer _hoverDelayTimer: Timer {
        interval: 50  // 50ms延迟，给扩展区域的onEntered和onPositionChanged足够时间触发
        onTriggered: {
            // 延迟后检查：如果扩展区域也没有被悬停，才清除按钮悬停状态
            if (!volumeControlItem._sliderAreaHovered && !extendedVolumeDragArea.containsMouse) {
                volumeControlItem._buttonHovered = false
            }
        }
    }

    // Extended drag area - for hover detection on button area
    // 用于检测按钮区域的悬停（当音量条显示时会被volumeSliderArea遮挡）
    MouseArea {
        id: extendedVolumeDragArea
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        width: 60
        height: 36
        hoverEnabled: true
        visible: controlPanel.opacity > 0
        acceptedButtons: Qt.NoButton  // 不处理点击，只检测悬停

        property bool isDragging: false
        property real currentVolume: 100

        // 初始化音量显示
        Component.onCompleted: {
            if (player && typeof player.volume !== "undefined") {
                currentVolume = player.volume * 100
            } else {
                currentVolume = 100
            }
        }

        // 监听音量变化，更新显示
        Connections {
            target: player
            function onVolumeChanged() {
                if (player && typeof player.volume !== "undefined") {
                    try {
                        extendedVolumeDragArea.currentVolume = Math.round(player.volume * 100)
                        volumeSliderArea.currentVolume = Math.round(player.volume * 100)
                    } catch (e) {
                        console.error("[VolumeControl] 更新音量显示失败:", e)
                    }
                }
            }
        }

        onEntered: {
            // 当鼠标进入按钮区域时，显示音量条
            volumeControlItem._hoverDelayTimer.stop()
            volumeControlItem._buttonHovered = true
            if (player && player.currentFile) {
                controlPanel.show()
            }
        }

        onPositionChanged: function(mouse) {
            if (containsMouse) {
                volumeControlItem._hoverDelayTimer.stop()
                volumeControlItem._buttonHovered = true
                if (player && player.currentFile) {
                    controlPanel.show()
                }
            }
        }

        onExited: {
            // 鼠标离开按钮区域时，延迟隐藏音量条
            if (!volumeButton.hovered) {
                volumeControlItem._hoverDelayTimer.restart()
            }
        }
    }

    // Vertical volume slider (appears on hover) - handles both display and interaction
    MouseArea {
        id: volumeSliderArea
        anchors.bottom: volumeButton.top
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: 0
        width: 60
        height: 205 + 17  // 增加高度以覆盖进度条（进度条高度7 + topMargin 5 + 间距5）
        hoverEnabled: true
        visible: volumeControlItem.volumeHovered
        acceptedButtons: Qt.LeftButton  // 接受鼠标左键事件
        propagateComposedEvents: false  // 阻止事件传播到视频区域

        property real currentVolume: 100
        property bool isDragging: false

        // 初始化音量显示
        Component.onCompleted: {
            if (player && typeof player.volume !== "undefined") {
                currentVolume = player.volume * 100
            } else {
                currentVolume = 100
            }
        }

        // 监听音量变化，更新显示（仅在非拖动状态）
        Connections {
            target: player
            function onVolumeChanged() {
                if (player && typeof player.volume !== "undefined" && !volumeSliderArea.isDragging) {
                    try {
                        currentVolume = Math.round(player.volume * 100)
                    } catch (e) {
                        console.error("[VolumeControl] 更新音量显示失败:", e)
                    }
                }
            }
        }

        onPressed: function(mouse) {
            // 检查是否在进度条区域（顶部27px）
            if (mouse.y < 27) {
                // 在进度条区域，不处理
                mouse.accepted = false
                return
            }

            // 开始拖动
            isDragging = true
            mouse.accepted = true
            updateVolumeFromMouse(mouse.y)
        }

        onPositionChanged: function(mouse) {
            // 处理拖动（无论鼠标是否在区域内，只要正在拖动就继续响应）
            if (isDragging) {
                updateVolumeFromMouse(mouse.y)
                // 拖动时保持控制面板显示
                volumeControlItem._hoverDelayTimer.stop()
                volumeControlItem._sliderAreaHovered = true
                if (player && player.currentFile) {
                    controlPanel.show()
                }
            } else if (containsMouse) {
                // 非拖动状态下，只在鼠标在区域内时保持悬停状态
                volumeControlItem._hoverDelayTimer.stop()
                volumeControlItem._sliderAreaHovered = true
                if (player && player.currentFile) {
                    controlPanel.show()
                }
            }
        }

        onReleased: function(mouse) {
            isDragging = false
            mouse.accepted = true

            // 释放后，如果鼠标不在区域内，清除悬停状态
            if (!containsMouse) {
                volumeControlItem._sliderAreaHovered = false
                if (!volumeButton.hovered) {
                    volumeControlItem._buttonHovered = false
                }
            }
        }

        onClicked: function(mouse) {
            // 拦截点击事件，防止传播到视频区域
            mouse.accepted = true
        }

        onEntered: {
            // 立即停止延迟定时器，防止状态被清除
            volumeControlItem._hoverDelayTimer.stop()
            volumeControlItem._sliderAreaHovered = true
            // 当鼠标进入音量展开栏时，保持控制面板显示
            if (player && player.currentFile) {
                controlPanel.show()
            }
        }

        onExited: {
            // 只有当鼠标真正离开且不在拖动状态时才清除悬停状态
            if (!isDragging) {
                volumeControlItem._sliderAreaHovered = false
                if (!volumeButton.hovered) {
                    volumeControlItem._buttonHovered = false
                }
            }
        }

        function updateVolumeFromMouse(mouseY) {
            // 可用滑动区域：从顶部边距27px到底部（总高度222px）
            // 有效滑动区域高度：222 - 27 = 195px
            var topMargin = 27
            var effectiveHeight = height - topMargin

            // 计算相对位置（允许超出边界，会被音量范围限制）
            var relativeY = mouseY - topMargin

            // 计算音量：顶部对应100%，底部对应0%
            // 即使鼠标超出边界也能计算，最终会被限制在0-100范围内
            var normalizedPos = 1.0 - (relativeY / effectiveHeight)
            var newVolume = Math.round(normalizedPos * 100)

            // 确保音量在0-100范围内（这里处理超出边界的情况）
            newVolume = Math.max(0, Math.min(100, newVolume))

            currentVolume = newVolume

            if (player) {
                player.volume = newVolume / 100.0
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

            // Volume percentage text (displayed at the top, outside slider)
            Text {
                id: volumePercentageText
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                anchors.topMargin: 8  // 减小顶部边距
                text: Math.round(volumeSliderArea.currentVolume)
                color: "#FFFFFF"
                font.pixelSize: 11
                font.bold: true
            }

            // Vertical slider with integrated percentage
            Item {
                anchors.fill: parent
                anchors.margins: 8
                anchors.topMargin: 28  // 减小顶部边距，让滑动条更长
                anchors.bottomMargin: 6  // 减小底部边距，让滑动条更长

                // 滑块背景轨道
                Item {
                    anchors.fill: parent

                    Rectangle {
                        id: trackBg
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.verticalCenter: parent.verticalCenter
                        width: 4
                        height: parent.height
                        radius: 2
                        color: "#404040"

                        Rectangle {
                            width: parent.width
                            height: (volumeSliderArea.currentVolume / 100.0) * parent.height
                            color: "#00A1D6"
                            radius: 2
                            anchors.bottom: parent.bottom
                        }
                    }

                    // 滑块手柄
                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: (1.0 - volumeSliderArea.currentVolume / 100.0) * parent.height - height / 2
                        implicitWidth: 12
                        implicitHeight: 12
                        radius: 6
                        color: volumeSliderArea.isDragging ? "#FFFFFF" : "#00A1D6"
                        border.color: volumeSliderArea.isDragging ? "#00A1D6" : "#FFFFFF"
                        border.width: 2
                    }
                }
            }
        }
    }

    // Volume button
    Button {
        id: volumeButton
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        width: 40
        height: 40
        flat: true

        icon.source: (player && player.volume === 0) ? "../../resources/mute.svg" : "../../resources/volume.svg"
        icon.color: "#FFFFFF"
        icon.width: 28
        icon.height: 28

        background: Rectangle {
            radius: 4
            // 使用按钮自身的 pressed/hovered 状态，而不是 controlPanel 的状态
            // 否则只要控制面板还在 hovered，就会导致按钮看起来“永远悬停”，进而影响音量条显示/隐藏。
            color: parent.pressed ? "#40FFFFFF" : (parent.hovered ? "#20FFFFFF" : "transparent")
        }

        onClicked: {
            if (player) {
                // 切换静音状态：如果当前有音量则静音，如果已静音则恢复音量
                if (player.volume > 0) {
                    player.volume = 0 // MediaPlayer 的 setVolume 会自动保存当前音量到 previousVolume
                    volumeSliderArea.currentVolume = 0 // 立即更新音量条显示
                } else {
                    // 恢复音量：使用之前保存的音量，如果没有则使用 100%
                    var restoreVolume = player.previousVolume > 0 ? player.previousVolume : 1.0
                    player.volume = restoreVolume

                    volumeSliderArea.currentVolume = Math.round(restoreVolume * 100) // 立即更新音量条显示
                }
            }
        }

        onHoveredChanged: {
            if (hovered) {
                // 鼠标进入按钮，立即设置状态并停止延迟定时器
                volumeControlItem._hoverDelayTimer.stop()
                volumeControlItem._buttonHovered = true
            } else {
                // 鼠标离开按钮时，总是延迟清除状态
                // 不立即检查扩展区域，因为按钮的z值高于扩展区域，可能遮挡检测
                // 延迟清除给扩展区域的onEntered和onPositionChanged时间触发
                volumeControlItem._hoverDelayTimer.restart()
            }
        }
    }
}

