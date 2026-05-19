import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import AdvancedPlayer 1.0
import "components"

ApplicationWindow {
    id: mainWindow
    width: 1280
    height: 720
    visible: true
    title: (player && player.currentFile) ? player.currentFile + " - Advanced Media Player" : "Advanced Media Player"
    color: "#121212"
    

    // Main layout - 视频显示和控制面板集成在主窗口中
    // 视频显示区域（使用VideoOutput组件进行OpenGL渲染）
    Rectangle {
        id: videoArea
        anchors.fill: parent
        color: "#000000"

        // VideoOutput组件 - OpenGL渲染
        VideoOutput {
            id: videoOutput
            anchors.fill: parent
            fillMode: VideoOutput.PreserveAspectFit

            // 标记是否已设置VideoOutput，避免重复设置
            property bool videoOutputSet: false

            Component.onCompleted: {
                // 将VideoOutput传递给MediaPlayer
                // 只在首次创建时设置一次
                if (!videoOutputSet && videoOutput && player) {
                    player.setVideoOutput(videoOutput)
                    videoOutputSet = true
                    console.log("[Main.qml] VideoOutput已创建并设置到MediaPlayer")
                } else if (!player || !videoOutput) {
                    console.error("[Main.qml] 错误：player或videoOutput为null")
                }
            }
        }

        // 无视频时的提示信息
        Column {
            anchors.centerIn: parent
            spacing: 20
            // 确保 isLoading 始终是有效的布尔值，避免 undefined
            visible: (!player || !player.currentFile)
                     && (!player || (typeof player.isLoading !== "undefined" ? !player.isLoading : true))

            Image {
                anchors.horizontalCenter: parent.horizontalCenter
                source: "../resources/play.svg"
                width: 100
                height: 100
                opacity: 0.3
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("请打开媒体文件开始播放")
                color: "#808080"
                font.pixelSize: 16
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("使用菜单打开文件")
                color: "#606060"
                font.pixelSize: 12
            }
        }

        // 视频信息覆盖层 (按I键显示)
        Rectangle {
            id: videoInfoOverlay
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 5
            height: infoColumn.height + 20
            color: "#000000"
            radius: 6
            visible: false

            Column {
                id: infoColumn
                anchors.centerIn: parent
                spacing: 8
                anchors.horizontalCenter: parent.horizontalCenter

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("视频信息")
                    color: "#FFFFFF"
                    font.pixelSize: 14
                    font.bold: true
                }

                Row {
                    spacing: 20
                    anchors.horizontalCenter: parent.horizontalCenter

                    Text {
                        text: qsTr("文件: ") + (player ? player.currentFile : "")
                        color: "#B0B0B0"
                        font.pixelSize: 11
                    }
                }

                Row {
                    spacing: 20
                    anchors.horizontalCenter: parent.horizontalCenter

                    Text {
                        text: qsTr("分辨率: ") + (player ? player.videoWidth : 0) + "x" + (player ? player.videoHeight : 0)
                        color: "#B0B0B0"
                        font.pixelSize: 11
                        visible: player && player.videoWidth > 0
                    }

                    Text {
                        text: qsTr("编码: ") + (player ? player.videoCodec : "")
                        color: "#B0B0B0"
                        font.pixelSize: 11
                        visible: player && player.videoCodec
                    }

                    Text {
                        text: qsTr("帧率: ") + (player ? player.frameRate.toFixed(2) : "0.00") + " fps"
                        color: "#B0B0B0"
                        font.pixelSize: 11
                        visible: player && player.frameRate > 0
                    }
                }

                Row {
                    spacing: 20
                    anchors.horizontalCenter: parent.horizontalCenter

                    Text {
                        text: qsTr("音频: ") + (player ? player.audioCodec : "")
                        color: "#B0B0B0"
                        font.pixelSize: 11
                        visible: player && player.audioCodec
                    }
                }
            }

            // 自动隐藏定时器
            Timer {
                id: infoOverlayTimer
                interval: 3000
                onTriggered: videoInfoOverlay.visible
            }
        }

        // 鼠标交互区域（用于显示/隐藏控制面板）
        MouseArea {
            id: videoMouseArea
            anchors.fill: parent
            hoverEnabled: true
            // propagateComposedEvents: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton

            property bool isDoubleClick: false

            onClicked: function(mouse) {
                // 延迟处理单击事件，等待可能的双击事件
                if (mouse.button === Qt.LeftButton && player && player.currentFile) {
                    // 重置双击标志
                    isDoubleClick = false
                    // 启动定时器，延迟执行暂停操作
                    singleClickTimer.restart()
                }
            }

            onDoubleClicked: {
                // 标记这是双击，取消定时器，阻止单击事件触发暂停
                singleClickTimer.stop()
                isDoubleClick = true
                mainWindow.toggleFullscreen()
            }

            onPositionChanged: {
                // 鼠标移动时显示控制面板（仅在存在媒体文件时）
                if (player && player.currentFile) {
                    controlPanel.show()
                    // 如果不在控制面板上且正在播放，启动隐藏定时器（鼠标停止移动1秒后隐藏）
                    if (!controlPanel.hovered && player.isPlaying && mainWindow.visibility !== Window.FullScreen) {
                        autoHideControlsTimer.restart()
                    } else {
                        autoHideControlsTimer.stop()
                    }
                }
            }

            // 延迟处理单击事件的定时器
            Timer {
                id: singleClickTimer
                interval: 150  // 双击检测时间窗口
                onTriggered: {
                    // 如果这不是双击，则执行暂停操作
                    if (!videoMouseArea.isDoubleClick && player && player.currentFile) {
                        player.togglePlayPause()
                    }
                    // 重置双击标志
                    videoMouseArea.isDoubleClick = false
                }
            }
        }

        // 控制面板 - 覆盖在视频画面上
        ControlPanel {
            id: controlPanel
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 60
            opacity: 0
            // 确保 currentFile 检查不会返回 undefined
            visible: player && player.currentFile && player.currentFile !== "" && opacity > 0

            // 传递settingsDialog引用
            settingsDialogRef: settingsDialog

            // 当鼠标在控制面板上移动时，停止隐藏定时器
            onHoveredChanged: {
                if (hovered) {
                    autoHideControlsTimer.stop()
                }else if (player && player.currentFile && player.isPlaying
                          && mainWindow.visibility !== Window.FullScreen) {
                   autoHideControlsTimer.restart()
                }
            }
        }

        // 播放列表切换按钮 - 右边界垂直居中
        Button {
            id: playlistToggleButton
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            // 使用Drawer的position属性实现平滑动画（position从0到1）
            anchors.rightMargin: playlistDrawer.position * 350
            width: 40
            height: 40
            flat: true

            // 根据播放列表状态切换图标：隐藏时显示左尖括号（展开），显示时显示右尖括号（折叠）
            icon.source: playlistDrawer.position > 0.5 ? "../resources/chevron-right.svg" : "../resources/chevron-left.svg"
            icon.color: "#FFFFFF"
            icon.width: 24
            icon.height: 24

            background: Rectangle {
                radius: 4
                color: parent.pressed ? "#40FFFFFF" : (parent.hovered ? "#20FFFFFF" : "#20000000")
                border.color: playlistDrawer.position > 0.5 ? "#00A1D6" : "#404040"
                border.width: 1
            }

            onClicked: {
                if (playlistDrawer.position > 0) {
                    playlistDrawer.close()
                } else {
                    playlistDrawer.open()
                }
            }

            ToolTip.visible: hovered
            ToolTip.text: playlistDrawer.position > 0.5 ? qsTr("隐藏播放列表") : qsTr("显示播放列表")
        }
    }

    // Playlist sidebar
    Drawer {
        id: playlistDrawer
        width: 350
        height: parent.height
        edge: Qt.RightEdge
        modal: false  // 不使用模态，避免遮挡视频区域
        interactive: false  // 禁用拖动手势，只通过按钮控制

        // 自定义动画，与按钮动画保持一致
        enter: Transition {
            NumberAnimation {
                property: "position"
                from: 0.0
                to: 1.0
                duration: 200
                easing.type: Easing.OutCubic
            }
        }
        exit: Transition {
            NumberAnimation {
                property: "position"
                from: 1.0
                to: 0.0
                duration: 200
                easing.type: Easing.OutCubic
            }
        }

        // 背景设为透明，避免影响视频显示
        background: Rectangle {
            color: "transparent"
        }

        PlaylistPanel {
            anchors.fill: parent
        }
    }
    
    // File dialog
    FileDialog {
        id: fileDialog
        title: qsTr("打开媒体文件")
        nameFilters: [
            qsTr("所有文件 (*.*)"),
            qsTr("视频文件 (*.mp4 *.mkv *.avi *.mov *.flv *.wmv *.webm)"),
            qsTr("音频文件 (*.mp3 *.flac *.aac *.wav *.ogg)")
        ]
        onAccepted: {
            if (playlistModel) {
                playlistModel.addMedia(selectedFile.toString())
            }
            if (player) {
                player.openFile(selectedFile)
            }
        }
    }
    
    // Settings dialog
    SettingsDialog {
        id: settingsDialog
    }
    
    // Notification toast
    NotificationToast {
        id: notificationToast
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 100
        width: parent.width * 0.8
    }
    
    // ===================JS Functions======================

    // 切换全屏
    function toggleFullscreen() {
        if (mainWindow.visibility === Window.FullScreen) {
            mainWindow.showNormal()
        } else {
            mainWindow.showFullScreen()
        }
    }
    
    function takeScreenshot() {
        if (!player) {
            return
        }
        const configuredFormat = (settingsManager.screenshotFormat || "png").toLowerCase()
        const fileExtension = configuredFormat === "jpeg"
                            ? "jpg"
                            : configuredFormat
        let filename = settingsManager.screenshotDirectory + "/" +
                      (player.currentFile || "screenshot") + "_" +
                      Qt.formatDateTime(new Date(), "yyyyMMdd_HHmmss") + "." + fileExtension

        // 截图流程为异步，结果通过信号回调通知。
        player.captureScreenshot(filename)
    }

    // ============================信号等处理==========================

    // Component completed
    Component.onCompleted: {
        console.log("Advanced Media Player started")
        console.log("Using OpenGL VideoOutput rendering")
        console.log("VideoOutput object:", videoOutput)
        console.log("VideoOutput valid:", videoOutput !== null)

        // 注意：VideoOutput 的 Component.onCompleted 已经会调用 setVideoOutput
        
        // 每次打开新的 main.qml 时，将倍速重置为 1 倍速
        if (player) {
            player.playbackSpeed = 1.0
            console.log("[Main.qml] 倍速已重置为 1.0x")
        }
    }
    
    // 窗口关闭时的资源清理
    onClosing: function(close) {
        console.log("[Main.qml] 窗口正在关闭，开始清理资源")

        if (player) {
            console.log("[Main.qml] 正在关闭媒体并清理所有资源")
            try {
                player.closeMedia()
                console.log("[Main.qml] 媒体已关闭，资源已清理")
            } catch (e) {
                console.error("[Main.qml] 关闭媒体异常:", e)
            }
        }

        // VideoOutput会随窗口一起销毁，PlaybackController使用QPointer会自动检测
        close.accepted = true
        console.log("[Main.qml] 窗口关闭流程完成")
    }

    // Shortcuts
    Item{
        Shortcut {
            sequence: "Ctrl+O"
            onActivated: fileDialog.open()
        }

        Shortcut {
            sequence: "Ctrl+Q"
            onActivated: mainWindow.close()
        }

        Shortcut {
            sequence: "Space"
            onActivated: {
                if (player) {
                    player.togglePlayPause()
                }
            }
        }

        Shortcut {
            sequence: "N"
            onActivated: {
                if (player) {
                    player.playNext()
                }
            }
        }

        Shortcut {
            sequence: "P"
            onActivated: {
                if (player) {
                    player.playPrevious()
                }
            }
        }

        Shortcut {
            sequence: "F11"
            onActivated: mainWindow.toggleFullscreen()
        }

        Shortcut {
            sequence: "Ctrl+S"
            onActivated: takeScreenshot()
        }

        Shortcut {
            sequence: "I"
            onActivated: {
                if (player && player.currentFile) {
                    videoInfoOverlay.visible = !videoInfoOverlay.visible
                    if (videoInfoOverlay.visible) {
                        infoOverlayTimer.restart()
                    }
                }
            }
        }
    }

    // 自动隐藏控制面板的定时器（仅在非控制面板区域）
    Timer {
        id: autoHideControlsTimer
        interval: 1000
        running: false
        onTriggered: {
            if (!controlPanel.hovered) {
                controlPanel.hide()
            }
        }
    }

    // 当播放状态改变时重置定时器
    Connections {
        target: player
        function onPlaybackStateChanged() {
            if (player && typeof player.isPlaying !== "undefined" && player.currentFile) {
                controlPanel.show()
                if (player.isPlaying && !controlPanel.hovered && mainWindow.visibility !== Window.FullScreen) {
                    autoHideControlsTimer.restart()
                } else {
                    autoHideControlsTimer.stop()
                }
            }
        }
    }

    // 监听播放器状态变化
    Connections {
        target: player  // Connections 在 target 为 null 时会自动忽略
        function onCurrentFileChanged() {
            // 文件变化时，显示控制面板
            // 注意：VideoOutput对象是固定的，不需要在每次文件切换时重新设置
            // PlaybackController 会保持 VideoOutput 引用，直到对象被销毁
            if (player && player.currentFile) {
                controlPanel.show()
            } else {
                controlPanel.hide()
            }
        }

        function onScreenshotSaved(path) {
            notificationToast.show(qsTr("截图已保存: ") + path)
        }

        function onScreenshotFailed(errorMessage) {
            notificationToast.show(qsTr("截图失败"))
            console.warn("[Main.qml] 截图失败:", errorMessage)
        }
    }
}
