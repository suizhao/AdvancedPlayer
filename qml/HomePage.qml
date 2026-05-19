import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import AdvancedPlayer 1.0
import "components"
import "../src/utils/Utils.js" as Utils

ApplicationWindow {
    id: homeWindow

    width: 1400
    height: 900
    visible: true
    title: "Advanced Media Player"
    color: "#121212"
    
    property string currentTopTab: "本地" // 当前选中的顶部标签
    property string currentLeftMenu: "首页" // 当前选中的左侧导航栏
    property int currentView: 0 // 当前视图：0=主页，1=播放器
    property var playerWindow: null // 播放器窗口引用
    property int selectedMediaIndex: -1 // 当前选中的媒体文件索引
    
    // ================================UI结构===========================

    // 主布局
    RowLayout {
        spacing: 0

        anchors.fill: parent
        
        // 左侧导航栏
        Rectangle {
            Layout.preferredWidth: 200
            Layout.fillHeight: true
            color: "#1E1E1E"
            
            Column {
                spacing: 10

                anchors.fill: parent
                anchors.topMargin: 20
                
                // 首页
                Rectangle {
                    width: parent.width
                    height: 50
                    color: currentLeftMenu === "首页" ? "#2A2A2A" : "transparent"
                    
                    Row {
                        spacing: 12

                        anchors.left: parent.left
                        anchors.leftMargin: 20
                        anchors.verticalCenter: parent.verticalCenter
                        
                        Text {
                            text: "🏠"
                            font.pixelSize: 20

                            anchors.verticalCenter: parent.verticalCenter
                        }
                        
                        Text {
                            text: "首页"
                            color: currentLeftMenu === "首页" ? "#00A1D6" : "#FFFFFF"
                            font.pixelSize: 16

                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                    
                    MouseArea {
                        anchors.fill: parent

                        onClicked: {
                            currentLeftMenu = "首页"
                        }
                    }
                }
                
                // 我的
                Rectangle {
                    width: parent.width
                    height: 50
                    color: currentLeftMenu === "我的" ? "#2A2A2A" : "transparent"
                    
                    Row {
                        spacing: 12

                        anchors.left: parent.left
                        anchors.leftMargin: 20
                        anchors.verticalCenter: parent.verticalCenter
                        
                        Text {
                            text: "👤"
                            font.pixelSize: 20

                            anchors.verticalCenter: parent.verticalCenter
                        }
                        
                        Text {
                            text: "我的"
                            color: currentLeftMenu === "我的" ? "#00A1D6" : "#FFFFFF"
                            font.pixelSize: 16

                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                    
                    MouseArea {
                        anchors.fill: parent

                        onClicked: {
                            currentLeftMenu = "我的"
                        }
                    }
                }
            }
        }
        
        // 右侧主内容区
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
            
            // 顶部导航栏
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 60
                color: "#1A1A1A"
                
                RowLayout {
                    spacing: 20

                    anchors.fill: parent
                    anchors.leftMargin: 20
                    anchors.rightMargin: 20
                    
                    // 顶部标签按钮
                    Row {
                        spacing: 30
                        Layout.alignment: Qt.AlignVCenter
                        
                        // 直播
                        Text {
                            text: "直播"
                            color: currentTopTab === "直播" ? "#00A1D6" : "#B0B0B0"
                            font.pixelSize: 16
                            font.bold: currentTopTab === "直播"
                            
                            MouseArea {
                                anchors.fill: parent
                                anchors.margins: -10

                                onClicked: {
                                    currentTopTab = "直播"
                                }
                            }
                        }
                        
                        // 热门
                        Text {
                            text: "热门"
                            color: currentTopTab === "热门" ? "#00A1D6" : "#B0B0B0"
                            font.pixelSize: 16
                            font.bold: currentTopTab === "热门"
                            
                            MouseArea {
                                anchors.fill: parent
                                anchors.margins: -10

                                onClicked: {
                                    currentTopTab = "热门"
                                }
                            }
                        }
                        
                        // 本地
                        Text {
                            text: "本地"
                            color: currentTopTab === "本地" ? "#00A1D6" : "#B0B0B0"
                            font.pixelSize: 16
                            font.bold: currentTopTab === "本地"
                            
                            MouseArea {
                                anchors.fill: parent
                                anchors.margins: -10

                                onClicked: {
                                    currentTopTab = "本地"
                                }
                            }
                        }
                    }
                    
                    // 搜索框(仅在热门标签显示)
                    Rectangle {
                        color: "#2A2A2A"
                        radius: 18
                        border.color: searchField.activeFocus ? "#00A1D6" : "#404040"
                        border.width: 1
                        visible: currentTopTab === "热门"
                        Layout.fillWidth: visible
                        Layout.preferredHeight: visible ? 36 : 0

                        Row {
                            anchors.left: parent.left
                            anchors.leftMargin: 15
                            anchors.right: addUrlButton.left
                            anchors.rightMargin: 5
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 10

                            Text {
                                text: "🔍"
                                font.pixelSize: 16
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            TextField {
                                id: searchField
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width - 40
                                height: 30
                                background: Item {}
                                color: "#FFFFFF"
                                placeholderText: "输入网络流URL添加到热门列表 (http/https/rtmp/rtsp)..."
                                placeholderTextColor: "#808080"
                                font.pixelSize: 14

                                // 按Enter键添加网络流URL
                                Keys.onReturnPressed: {
                                    if (text.trim().length > 0) {
                                        addNetworkStreamUrl(text.trim())
                                    }
                                }
                                Keys.onEnterPressed: {
                                    if (text.trim().length > 0) {
                                        addNetworkStreamUrl(text.trim())
                                    }
                                }
                            }
                        }

                        // 添加URL按钮（仅在热门标签页显示）
                        Button {
                            id: addUrlButton

                            width: 60
                            height: 28
                            visible: currentTopTab === "热门"
                            text: "添加"
                            font.pixelSize: 12

                            anchors.right: parent.right
                            anchors.rightMargin: 5
                            anchors.verticalCenter: parent.verticalCenter

                            background: Rectangle {
                                radius: 14
                                color: parent.pressed ? "#007ACC" : (parent.hovered ? "#1A9AD6" : "#00A1D6")
                            }

                            contentItem: Text {
                                text: parent.text
                                font: parent.font
                                color: "#FFFFFF"
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }

                            onClicked: {
                                if (searchField.text.trim().length > 0) {
                                    addNetworkStreamUrl(searchField.text.trim())
                                }
                            }
                        }
                    }
                }
            }
            
            // 主内容区域 - 播放列表
            Rectangle {
                id: playlistArea
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "#121212"
                
                // 获取当前使用的模型
                property var currentModel: {
                    if (currentTopTab === "热门") {
                        return networkPlaylistModel
                    } else if (currentTopTab === "直播") {
                        return null  // 直播标签页暂时为空，未来用GStreamer实现
                    } else {
                        return playlistModel
                    }
                }
                
                ScrollView {
                    anchors.fill: parent
                    anchors.margins: 20
                    
                    ScrollBar.vertical.policy: ScrollBar.AsNeeded
                    ScrollBar.vertical.interactive: true
                    
                    // 播放列表网格视图
                    GridView {
                        id: playlistGridView
                        anchors.fill: parent
                        cellWidth: 280
                        cellHeight: 220
                        model: playlistArea.currentModel
                        
                        // 空列表提示
                        Text {
                            anchors.centerIn: parent
                            color: "#808080"
                            font.pixelSize: 16
                            horizontalAlignment: Text.AlignHCenter
                            visible: {
                                if (currentTopTab === "直播") return true
                                return playlistArea.currentModel && playlistArea.currentModel.count === 0 && !dropArea.containsDrag
                            }
                            text: {
                                if (currentTopTab === "直播") return "直播功能开发中\n\n未来将使用GStreamer实现实时流播放"

                                if (playlistArea.currentModel && playlistArea.currentModel.count === 0) {
                                    if (currentTopTab === "本地") {
                                        return "暂无媒体文件\n\n点击菜单栏「文件」→「打开文件夹」添加媒体\n或拖动文件到该区域来加入文件"
                                    } else if (currentTopTab === "热门") {
                                        return "暂无网络流\n\n在搜索框输入网络流URL并点击「添加」\n支持 http、https、rtmp、rtsp 等协议"
                                    } else {
                                        return "暂无媒体文件"
                                    }
                                }
                                return ""
                            }
                        }
                        
                        delegate: Rectangle {
                            id: mediaItemDelegate

                            width: playlistGridView.cellWidth - 20
                            height: playlistGridView.cellHeight - 20
                            color: "#1E1E1E"
                            radius: 8
                            
                            property bool isNetworkList: currentTopTab === "热门" // 判断是否为网络流列表
                            property bool hovered: false // 鼠标悬停效果
                            property bool isSelected: homeWindow.selectedMediaIndex === index // 是否被选中
                            
                            // 统一获取显示数据（兼容本地文件和网络流）
                            property string displayName: isNetworkList ? (model.title || "") : (model.fileName || "")
                            property string mediaPath: isNetworkList ? (model.url || "") : (model.filePath || "")
                            property bool isLiveStream: isNetworkList ? (model.isLive || false) : false
                            property string sourceInfo: isNetworkList ? (model.source || "") : ""
                            
                            // 鼠标悬空或单击时背景动画过度
                            Rectangle {
                                anchors.fill: parent
                                color: {
                                    if (parent.isSelected) {
                                        return "#2A4A6A"
                                    } else if (parent.hovered) {
                                        return "#2A2A2A"
                                    } else {
                                        return "transparent"
                                    }
                                }
                                radius: 8
                                border.color: {
                                    if (parent.isSelected) {
                                        return "#00A1D6"
                                    } else if (parent.hovered) {
                                        return "#00A1D6"
                                    } else {
                                        return "transparent"
                                    }
                                }
                                border.width: 2
                                
                                Behavior on color {
                                    ColorAnimation { duration: 100 }
                                }
                                Behavior on border.color {
                                    ColorAnimation { duration: 100 }
                                }
                            }
                            
                            Column {
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 8
                                
                                // 缩略图占位符
                                Rectangle {
                                    width: parent.width
                                    height: 120
                                    color: "#2A2A2A"
                                    radius: 6
                                    
                                    // 播放图标
                                    Text {
                                        anchors.centerIn: parent
                                        text: "▶"
                                        font.pixelSize: 40
                                        color: "#808080"
                                    }
                                    
                                    // 直播标识（网络流）
                                    Rectangle {
                                        anchors.left: parent.left
                                        anchors.top: parent.top
                                        anchors.margins: 8
                                        width: liveText.width + 12
                                        height: 22
                                        radius: 4
                                        color: "#FF4444"
                                        visible: mediaItemDelegate.isLiveStream
                                        
                                        Text {
                                            id: liveText
                                            anchors.centerIn: parent
                                            text: "LIVE"
                                            color: "#FFFFFF"
                                            font.pixelSize: 11
                                            font.bold: true
                                        }
                                    }
                                    
                                    // 文件类型/来源图标
                                    Text {
                                        anchors.right: parent.right
                                        anchors.bottom: parent.bottom
                                        anchors.margins: 8
                                        text: {
                                            if (mediaItemDelegate.isNetworkList) {
                                                return "🌐" // 网络流图标
                                            }
                                            // 本地文件图标
                                            var name = mediaItemDelegate.displayName.toLowerCase()
                                            if (name.endsWith(".mp4") || name.endsWith(".mkv") || name.endsWith(".avi")) {
                                                return "🎬"
                                            } else if (name.endsWith(".mp3") || name.endsWith(".flac") || name.endsWith(".wav")) {
                                                return "🎵"
                                            } else {
                                                return "📄"
                                            }
                                        }
                                        font.pixelSize: 24
                                    }
                                }
                                
                                // 标题/文件名
                                Text {
                                    width: parent.width
                                    text: mediaItemDelegate.displayName
                                    color: "#FFFFFF"
                                    font.pixelSize: 14
                                    elide: Text.ElideRight       // 文字太长 → 末尾显示 ...
                                    maximumLineCount: 2          // 最多显示 2 行
                                    wrapMode: Text.Wrap          // 自动换行（单词/整字换行）
                                }
                                
                                // 文件信息/来源信息
                                Row {
                                    spacing: 15
                                    
                                    // 本地文件显示大小
                                    Text {
                                        text: mediaItemDelegate.isNetworkList ? "" : Utils.formatFileSize(model.size || 0)
                                        color: "#808080"
                                        font.pixelSize: 12
                                        visible: !mediaItemDelegate.isNetworkList
                                    }
                                    
                                    // 本地文件显示时长
                                    Text {
                                        // PlaylistModel 当前未实现真实时长提取，duration 可能长期为 0
                                        text: (model.duration || 0) > 0 ? Utils.formatDuration(model.duration || 0) : "时长未知"
                                        color: "#808080"
                                        font.pixelSize: 12
                                        visible: !mediaItemDelegate.isNetworkList
                                    }
                                    
                                    // 网络流显示来源
                                    Text {
                                        text: mediaItemDelegate.sourceInfo ? ("来源: " + mediaItemDelegate.sourceInfo) : ""
                                        color: "#808080"
                                        font.pixelSize: 12
                                        visible: mediaItemDelegate.isNetworkList
                                    }
                                }
                            }
                            
                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                
                                property bool isDoubleClick: false
                                
                                onEntered: {
                                    mediaItemDelegate.hovered = true
                                }
                                
                                onExited: {
                                    mediaItemDelegate.hovered = false
                                }
                                
                                onClicked: function(mouse) {
                                    if (mouse.button === Qt.LeftButton) {
                                        isDoubleClick = false // 重置双击标志
                                        singleClickTimer.restart() // 启动定时器，延迟执行选中操作
                                    } else if (mouse.button === Qt.RightButton) {
                                        contextMenu.popup() // 右键显示菜单（在鼠标位置）
                                    }
                                }
                                
                                onDoubleClicked: function(mouse) {
                                    if (mouse.button === Qt.LeftButton) {
                                        // 标记这是双击，取消定时器，阻止单击事件触发选中
                                        isDoubleClick = true
                                        singleClickTimer.stop()

                                        homeWindow.selectedMediaIndex = index// 立即设置选中状态（显示蓝框）
                                        playMedia(mediaItemDelegate.mediaPath, index, mediaItemDelegate.isNetworkList) // 双击打开媒体
                                    }
                                }

                                // 延迟处理单击事件的定时器
                                Timer {
                                    id: singleClickTimer
                                    interval: 100  // 双击检测时间窗口
                                    onTriggered: {
                                        // 如果这不是双击，则执行选中操作
                                        if (!parent.isDoubleClick) {
                                            homeWindow.selectedMediaIndex = index
                                        }
                                        // 重置双击标志
                                        parent.isDoubleClick = false
                                    }
                                }
                            }
                            
                            // 右键菜单
                            Menu {
                                id: contextMenu
                                
                                MenuItem {
                                    text: qsTr("打开")
                                    onTriggered: {
                                        // 统一播放入口：根据列表类型自动处理本地/网络流
                                        playMedia(mediaItemDelegate.mediaPath, index, mediaItemDelegate.isNetworkList)
                                    }
                                }
                                
                                MenuItem {
                                    text: qsTr("复制链接")
                                    visible: mediaItemDelegate.isNetworkList
                                    height: visible ? implicitHeight : 0
                                    enabled: visible // 注释后也可
                                    onTriggered: {
                                        // 复制URL到剪贴板
                                        if (mediaItemDelegate.mediaPath) {
                                            // TODO: 实现复制到剪贴板功能
                                            notificationToast.show(qsTr("链接已复制"))
                                        }
                                    }
                                }
                                
                                
                                MenuItem {
                                    text: qsTr("删除")
                                    onTriggered: {
                                        if (mediaItemDelegate.isNetworkList) {
                                            if (networkPlaylistModel) {
                                                networkPlaylistModel.removeStream(index)
                                            }
                                        } else {
                                            if (playlistModel) {
                                                playlistModel.removeMedia(index)
                                            }
                                        }
                                        // 如果删除的是选中的项，清除选中状态
                                        if (homeWindow.selectedMediaIndex === index) {
                                            homeWindow.selectedMediaIndex = -1
                                        } else if (homeWindow.selectedMediaIndex > index) {
                                            // 如果删除的项在选中项之前，需要调整选中索引
                                            homeWindow.selectedMediaIndex--
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // 拖放区域（仅在"本地"标签页时启用）
                DropArea {
                    id: dropArea
                    anchors.fill: parent
                    keys: ["text/uri-list"] // 限定接收数据类型
                    enabled: currentTopTab === "本地"

                    // 拖放时的视觉反馈
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 10
                        color: "#1A00A1D6"
                        border.color: "#00A1D6"
                        border.width: 3
                        radius: 8
                        visible: dropArea.containsDrag
                        opacity: 0.3

                        Behavior on opacity {
                            NumberAnimation { duration: 200 }
                        }
                    }

                    // 拖放提示文本
                    Text {
                        anchors.centerIn: parent
                        text: "拖动文件到该区域来加入文件"
                        color: "#00A1D6"
                        font.pixelSize: 24
                        font.bold: true
                        visible: dropArea.containsDrag
                        z: 10
                    }


                    // 拖放松开
                    onDropped: function(drop) {
                        if (drop.hasUrls && playlistModel) {
                            var fileCount = 0
                            var pendingFolders = []

                            // 分离文件和文件夹
                            for (var i = 0; i < drop.urls.length; i++) {
                                var filePath = Utils.urlToLocalPath(drop.urls[i].toString())

                                // 检查路径是否有扩展名（简单判断是否为文件）
                                var lastDot = filePath.lastIndexOf('.')
                                var lastSlash = Math.max(filePath.lastIndexOf('/'), filePath.lastIndexOf('\\'))
                                var hasExtension = (lastDot > lastSlash && lastDot > 0)

                                if (hasExtension) {
                                    // 尝试作为文件添加（addMedia 会检查文件是否存在）
                                    var oldCount = playlistModel.count
                                    playlistModel.addMedia(filePath)
                                    if (playlistModel.count > oldCount) {
                                        fileCount++
                                    }
                                } else {
                                    // 可能是文件夹
                                    pendingFolders.push(filePath)
                                }
                            }

                            // 处理文件夹
                            if (pendingFolders.length > 0) {
                                var folderFilesAdded = 0
                                var completedFolders = 0
                                var scanners = []

                                for (var j = 0; j < pendingFolders.length; j++) {
                                    var folderPath = pendingFolders[j]
                                    var scanner = Qt.createQmlObject('import AdvancedPlayer 1.0; FileScanner {}', homeWindow)
                                    scanners.push(scanner)

                                    scanner.fileFound.connect(function(filePath) {
                                        playlistModel.addMedia(filePath)
                                        folderFilesAdded++
                                    })

                                    scanner.scanCompleted.connect(function(count) {
                                        completedFolders++
                                        // 所有文件夹扫描完成
                                        if (completedFolders === pendingFolders.length) {
                                            var totalAdded = fileCount + folderFilesAdded
                                            if (totalAdded > 0) {
                                                notificationToast.show(qsTr("已添加 ") + totalAdded + qsTr(" 个文件到播放列表"))
                                            }
                                            // 清理所有扫描器
                                            for (var k = 0; k < scanners.length; k++) {
                                                if (scanners[k]) {
                                                    scanners[k].destroy()
                                                }
                                            }
                                        }
                                    })

                                    scanner.scanFolder(folderPath, true)
                                }
                            } else if (fileCount > 0) {
                                // 只有文件，立即显示通知
                                notificationToast.show(qsTr("已添加 ") + fileCount + qsTr(" 个文件到播放列表"))
                            }
                        }
                    }
                }
            }
        }
    }
    
    // 菜单栏
    menuBar: MenuBar {
        Menu {
            title: qsTr("文件(&F)")
            
            MenuItem {
                text: qsTr("打开文件(Ctrl+O)")
                onTriggered: fileDialog.open()
            }
            
            MenuItem {
                text: qsTr("打开文件夹")
                onTriggered: folderDialog.open()
            }
            
            MenuSeparator {}
            
            MenuItem {
                text: qsTr("退出(Ctrl+Q)")
                onTriggered: Qt.quit()
            }
        }
        
        Menu {
            title: qsTr("帮助(&H)")
            
            MenuItem {
                text: qsTr("关于")
                onTriggered: aboutDialog.open()
            }
        }
    }
    
    // 文件对话框
    FileDialog {
        id: fileDialog
        title: qsTr("打开媒体文件")
        fileMode: FileDialog.OpenFiles
        nameFilters: [
            qsTr("所有文件 (*.*)"),
            qsTr("视频文件 (*.mp4 *.mkv *.avi *.mov *.flv *.wmv *.webm)"),
            qsTr("音频文件 (*.mp3 *.flac *.aac *.wav *.ogg)")
        ]

        onAccepted: {
            if (playlistModel) {
                for (let i = 0; i < selectedFiles.length; i++) {
                    playlistModel.addMedia(Utils.urlToLocalPath(selectedFiles[i]))
                }
            }
        }
    }
    
    // 文件夹对话框
    FolderDialog {
        id: folderDialog
        title: qsTr("打开文件夹")

        onAccepted: {
            var folderPath = Utils.urlToLocalPath(selectedFolder) // 将 URL 格式转换为本地文件路径
            console.log("Selected folder:", folderPath)
            
            let scanner = Qt.createQmlObject('import AdvancedPlayer 1.0; FileScanner {}', homeWindow)

            scanner.fileFound.connect(function(filePath) {
                playlistModel.addMedia(filePath)
            })

            scanner.scanCompleted.connect(function(count) {
                notificationToast.show(qsTr("已添加 ") + count + qsTr(" 个文件到播放列表"))
                scanner.destroy()
            })
            
            scanner.scanFolder(folderPath, true)
        }
    }
    
    // 关于对话框
    Dialog {
        id: aboutDialog
        title: qsTr("关于")
        width: 400
        height: 300
        modal: true

        anchors.centerIn: parent
        
        ColumnLayout {
            spacing: 15

            anchors.fill: parent
            anchors.margins: 20
            
            Text {
                text: "Advanced Media Player"
                font.pixelSize: 24
                font.bold: true
                color: "#FFFFFF"
            }

            Text {
                text: qsTr("版本: 1.0.0")
                color: "#B0B0B0"
            }
            
            Text {
                text: qsTr("一款高性能的现代化多媒体播放器")
                color: "#B0B0B0"
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }
        
        standardButtons: Dialog.Close
    }
    
    // 通知提示
    NotificationToast {
        id: notificationToast
        width: Math.min(400, parent.width * 0.8)
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 100
    }


    // ===============================js函数、业务逻辑============================
    
    // 播放媒体（统一入口，isNetworkSource=true表示网络流）
    function playMedia(mediaPath, index, isNetworkSource) {
        console.log("[HomePage.playMedia] 开始播放, mediaPath:", mediaPath, "index:", index, "isNetwork:", isNetworkSource)

        // 设置UI选中态
        if (index >= 0 && index !== undefined) {
            selectedMediaIndex = index
            playlistArea.currentModel.setCurrentIndex(index)
        }

        // 统一转为URL格式传入 player.openFile
        var mediaUrl = isNetworkSource ? mediaPath : Utils.localPathToUrl(Utils.urlToLocalPath(mediaPath))

        // 如果窗口不存在，先创建窗口；否则直接播放
        if (!playerWindow) {
            console.log("[HomePage.playMedia] 创建播放器窗口...")
            openPlayerWindow(mediaUrl)
        } else {
            playerWindow.show()
            playerWindow.raise()
            playerWindow.requestActivate()
            if (player) {
                console.log("[HomePage.playMedia] 在现有窗口播放:", mediaUrl)
                player.openFile(mediaUrl)
            }
        }
    }

    // 打开播放器窗口并播放（参数统一为URL格式）
    function openPlayerWindow(mediaUrl) {
        // 如果播放器窗口已存在，复用它而不是重新创建
        if (playerWindow) {
            console.log("[HomePage.openPlayerWindow] 播放器窗口已存在，直接播放")
            playerWindow.show()
            playerWindow.raise()
            playerWindow.requestActivate()

            // 等待确保窗口激活后再播放
            Qt.callLater(function() {
                if (player) {
                    console.log("[HomePage.openPlayerWindow] 在现有窗口中播放:", mediaUrl)
                    player.openFile(mediaUrl)
                }
            })
            return
        }

        // 使用Qt.createComponent动态加载Main.qml
        var paths = [
            "qrc:/qt/qml/AdvancedPlayer/qml/Main.qml",
            "qrc:/AdvancedPlayer/Main.qml",
            ":/qt/qml/AdvancedPlayer/qml/Main.qml"
        ]

        var component = null
        var foundPath = ""

        for (var i = 0; i < paths.length; i++) {
            component = Qt.createComponent(paths[i])
            if (component.status === Component.Ready || component.status === Component.Loading) {
                foundPath = paths[i]
                console.log("[HomePage.openPlayerWindow] 找到Main.qml路径:", foundPath)
                break
            } else if (component.status === Component.Error) {
                console.warn("[HomePage.openPlayerWindow] 路径不可用:", paths[i], component.errorString())
            }
        }

        if (!component || component.status === Component.Error) {
            console.error("[HomePage.openPlayerWindow] 无法加载Main.qml")
            if (component) {
                console.error("[HomePage.openPlayerWindow] 错误信息:", component.errorString())
            }
            // 备用方案：直接播放
            if (player) {
                player.openFile(mediaUrl)
            }
            return
        }

        function createWindow() {
            if (component.status === Component.Ready) {
                // 创建为完全独立的窗口，在任务栏显示
                playerWindow = component.createObject(null, {
                    "visible": true
                })

                if (playerWindow) {
                    playerWindow.closing.connect(function(close) {
                        console.log("[HomePage.openPlayerWindow] 播放器窗口正在关闭")
                        close.accepted = true
                        Qt.callLater(function() { playerWindow = null })
                    })
                    console.log("[HomePage.openPlayerWindow] 播放器窗口创建成功")

                    // 等待窗口完全初始化后开始播放
                    Qt.callLater(function() {
                        var playTimer = Qt.createQmlObject('import QtQuick; Timer { interval: 100; running: true }', homeWindow)
                        playTimer.triggered.connect(function() {
                            if (player) {
                                console.log("[HomePage.openPlayerWindow] 开始播放:", mediaUrl)
                                player.openFile(mediaUrl)
                            } else {
                                console.error("[HomePage.openPlayerWindow] player 对象为 null")
                            }
                            playTimer.destroy()
                        })
                    })
                } else {
                    console.error("[HomePage.openPlayerWindow] 创建播放器窗口失败")
                    if (player) {
                        player.openFile(mediaUrl)
                    }
                }
            } else if (component.status === Component.Error) {
                console.error("[HomePage.openPlayerWindow] 无法加载Main.qml:", component.errorString())
                if (player) {
                    player.openFile(mediaUrl)
                }
            }
        }

        if (component.status === Component.Ready) {
            createWindow()
        } else {
            component.statusChanged.connect(createWindow)
        }
    }
    
    // 添加网络流URL到热门列表
    function addNetworkStreamUrl(url) {
        if (!url || url.trim().length === 0) {
            notificationToast.show(qsTr("请输入有效的网络流URL"))
            return
        }
        
        var trimmedUrl = url.trim()
        
        // 验证URL格式
        var supportedProtocols = ["http://", "https://", "rtmp://", "rtsp://", "rtp://", "udp://", "mms://", "hls://"]
        var isValid = false
        for (var i = 0; i < supportedProtocols.length; i++) {
            if (trimmedUrl.toLowerCase().startsWith(supportedProtocols[i])) {
                isValid = true
                break
            }
        }
        
        if (!isValid) {
            notificationToast.show(qsTr("不支持的URL格式\n请使用 http、https、rtmp、rtsp 等协议"))
            return
        }
        
        // 添加到网络流播放列表
        if (networkPlaylistModel) {
            var success = networkPlaylistModel.addStream(trimmedUrl)
            if (success) {
                notificationToast.show(qsTr("已添加网络流到热门列表"))
                searchField.text = ""  // 清空搜索框
            } else {
                notificationToast.show(qsTr("添加失败：该URL可能已存在"))
            }
        }
    }

    // =============================信号、定时器、状态机(Behavior/State)、快捷键==============================
    
    // 主窗口关闭时的优化资源清理流程
    onClosing: function(close) {
        console.log("[HomePage]  主窗口正在关闭，开始资源清理")

        // ===== 先关闭媒体，确保解码器资源完全清理 =====
        if (player) {
            console.log("[HomePage] 正在关闭媒体并清理所有解码器资源")
            try {
                player.closeMedia()
                console.log("[HomePage] 媒体已关闭，解码器资源已清理")
            } catch (e) {
                console.error("[HomePage] 关闭媒体异常:", e)
            }
        }

        // 然后关闭播放器窗口
        if (playerWindow) {
            console.log("[HomePage] 关闭播放器窗口")
            playerWindow.destroy()
            playerWindow = null
        }

        // 接受关闭事件
        close.accepted = true

        console.log("[HomePage] 主窗口关闭流程完成，应用程序将退出")
    }

    // 快捷键
    Item{
        Shortcut {
            sequence: "Ctrl+O"
            onActivated: fileDialog.open()
        }

        Shortcut {
        sequence: "Ctrl+Q"
        onActivated: Qt.quit()
    }
    }
}

