import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import "../../src/utils/Utils.js" as Utils

Rectangle {
    id: playlistPanel
    color: "#1E1E1E"
    
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        
        // Title bar
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 50
            color: "#252525"
            
            RowLayout {
                anchors.fill: parent
                anchors.margins: 10
                
                Text {
                    text: qsTr("播放列表")
                    color: "#FFFFFF"
                    font.pixelSize: 16
                    font.bold: true
                }
                
                Item { Layout.fillWidth: true }
                
                Text {
                    text: (playlistModel ? playlistModel.count : 0) + qsTr(" 项")
                    color: "#B0B0B0"
                    font.pixelSize: 12
                }
            }
        }
        
        // Playlist
        ListView {
            id: playlistView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            
            model: playlistModel
            
            delegate: Item {
                width: playlistView.width
                height: 60
                
                property bool itemHovered: rowHoverHandler.hovered
                property bool isHighlighted: playlistModel && index === playlistModel.currentIndex

                HoverHandler {
                    id: rowHoverHandler
                }
                
                Rectangle {
                    id: backgroundRect
                    anchors.fill: parent
                    color: parent.isHighlighted ? "#303030" : 
                           (parent.itemHovered ? "#252525" : "transparent")
                }

                // 使用MouseArea来处理双击事件，避免被子元素拦截
                MouseArea {
                    id: mouseArea
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton

                    onClicked: {
                        console.log("[PlaylistPanel] 单击播放列表项，索引:", index)
                    }

                    onDoubleClicked: {
                        console.log("[PlaylistPanel] 双击播放列表项，索引:", index)
                        if (playlistModel) {
                            console.log("[PlaylistPanel] 调用 playlistModel.playIndex(", index, ")")
                            playlistModel.playIndex(index)
                        } else {
                            console.log("[PlaylistPanel] 错误：playlistModel 为空")
                        }
                    }
                }
                
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 10
                    
                    // Index or play icon
                    Rectangle {
                        Layout.preferredWidth: 30
                        Layout.preferredHeight: 30
                        radius: 15
                        color: isHighlighted ? "#FF4081" : "#303030"
                        
                        Text {
                            anchors.centerIn: parent
                            text: isHighlighted ? "▶" : (index + 1)
                            color: "#FFFFFF"
                            font.pixelSize: isHighlighted ? 12 : 14
                        }
                    }
                    
                    // File info
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        
                        Text {
                            text: model.fileName
                            color: "#FFFFFF"
                            font.pixelSize: 14
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        
                        Row {
                            spacing: 10
                            
                            Text {
                                text: Utils.formatDuration(model.duration)
                                color: "#808080"
                                font.pixelSize: 11
                            }
                            
                            Text {
                                text: Utils.formatFileSize(model.size)
                                color: "#808080"
                                font.pixelSize: 11
                            }
                        }
                    }
                    
                    // Delete button
                    Button {
                        Layout.preferredWidth: 32
                        Layout.preferredHeight: 32
                        flat: true
                        text: "×"
                        font.pixelSize: 24
                        visible: itemHovered
                        
                        onClicked: {
                            console.log("[PlaylistPanel] 删除按钮被点击，索引:", index)
                            if (playlistModel) {
                                playlistModel.removeMedia(index)
                            }
                        }
                        
                        background: Rectangle {
                            radius: 16
                            color: parent.pressed ? "#60FF4081" : 
                                   (parent.hovered ? "#40FFFFFF" : "transparent")
                        }
                        
                        contentItem: Text {
                            text: parent.text
                            font: parent.font
                            color: "#FFFFFF"
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
                
            }
            
            // Empty list hint
            Label {
                anchors.centerIn: parent
                text: qsTr("播放列表为空\n拖放文件到这里或点击下方按钮添加")
                color: "#606060"
                font.pixelSize: 14
                horizontalAlignment: Text.AlignHCenter
                visible: playlistView.count === 0
            }
            
            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
            }
        }
        
        // Bottom toolbar
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 60
            color: "#252525"
            
            Row {
                anchors.centerIn: parent
                spacing: 50
                
                Button {
                    text: qsTr("添加文件")
                    onClicked: addFilesDialog.open()
                    
                    background: Rectangle {
                        radius: 4
                        color: parent.pressed ? "#FF4081" : 
                               (parent.hovered ? "#60FF4081" : "#303030")
                    }
                    
                    contentItem: Text {
                        text: parent.text
                        font: parent.font
                        color: "#FFFFFF"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                
                Button {
                    text: qsTr("清空")
                    enabled: playlistModel && playlistModel.count > 0
                    onClicked: {
                        if (playlistModel) {
                            playlistModel.clearPlaylist()
                        }
                    }
                    
                    background: Rectangle {
                        radius: 4
                        color: parent.pressed ? "#D32F2F" : 
                               (parent.hovered ? "#60D32F2F" : "#303030")
                        opacity: parent.enabled ? 1.0 : 0.3
                    }
                    
                    contentItem: Text {
                        text: parent.text
                        font: parent.font
                        color: "#FFFFFF"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
        }
    }
    
    // File dialogs
    FileDialog {
        id: addFilesDialog
        title: qsTr("添加文件到播放列表")
        fileMode: FileDialog.OpenFiles
        nameFilters: [
            qsTr("所有文件 (*.*)"),
            qsTr("媒体文件 (*.mp4 *.mkv *.avi *.mov *.mp3 *.flac)")
        ]
        onAccepted: {
            if (playlistModel) {
                for (let i = 0; i < selectedFiles.length; i++) {
                    playlistModel.addMedia(selectedFiles[i])
                }
            }
        }
    }
}

