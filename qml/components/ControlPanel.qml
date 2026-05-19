import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../../src/utils/Utils.js" as Utils

Rectangle {
    id: controlPanel
    color: "#A0000000"
    
    HoverHandler {
        id: panelHover
        acceptedDevices: PointerDevice.Mouse
    }
    
    property bool hovered: panelHover.hovered||
                           (speedControlItem && speedControlItem.speedHovered) ||
                           (volumeControlItem && volumeControlItem.volumeHovered)
    property var settingsDialogRef: null
    
    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
    }


    ColumnLayout {
        id: contentLayout
        anchors.fill: parent
        spacing: 0
        
        // Progress bar at the top
        CustomProgressBar {
            id: progressBar
            Layout.fillWidth: true
            Layout.preferredHeight: 7
            Layout.topMargin: 5
            anchors.leftMargin: 10
            anchors.rightMargin: 10
        }
        
        // Main control row
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 35
            Layout.leftMargin: 15
            Layout.rightMargin: 15
            Layout.bottomMargin: 8
            spacing: 15
            
            // Left side - Play controls
            Row {
                spacing: 5
                Layout.alignment: Qt.AlignVCenter
                
                // Play/Pause
                Button {
                    id: playPauseButton
                    width: 40
                    height: 40
                    flat: true
                    icon.source: (player && player.isPlaying) ? "../../resources/pause.svg" : "../../resources/play.svg"
                    icon.color: "#FFFFFF"
                    icon.width: 28
                    icon.height: 28
                    onClicked: {
                        if (player) player.togglePlayPause()
                    }
                    
                    background: Rectangle {
                        radius: 4
                        color: parent.pressed ? "#40FFFFFF" : (parent.hovered ? "#20FFFFFF" : "transparent")
                    }
                    
                    ToolTip.visible: hovered
                    ToolTip.text: (player && player.isPlaying) ? qsTr("暂停 (Space)") : qsTr("播放 (Space)")
                }
                
                // Previous
                Button {
                    id: prevButton
                    width: 40
                    height: 40
                    flat: true
                    icon.source: "../../resources/previous.svg"
                    icon.color: "#FFFFFF"
                    icon.width: 28
                    icon.height: 28
                    onClicked: {
                        if (player) {
                            player.playPrevious()
                        }
                    }
                    
                    background: Rectangle {
                        radius: 4
                        color: parent.pressed ? "#40FFFFFF" : (parent.hovered ? "#20FFFFFF" : "transparent")
                    }
                    
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("上一个 (P)")
                }
                
                // Next
                Button {
                    id: nextButton
                    width: 40
                    height: 40
                    flat: true
                    icon.source: "../../resources/next.svg"
                    icon.color: "#FFFFFF"
                    icon.width: 28
                    icon.height: 28
                    onClicked: {
                        if (player) {
                            player.playNext()
                        }
                    }
                    
                    background: Rectangle {
                        radius: 4
                        color: parent.pressed ? "#40FFFFFF" : (parent.hovered ? "#20FFFFFF" : "transparent")
                    }
                    
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("下一个 (N)")
                }
            }
            
            // Center - Time display
            Row {
                spacing: 5
                Layout.alignment: Qt.AlignVCenter
                
                Text {
                    id: currentTime
                    text: Utils.formatTime(player ? player.position : 0)
                    color: "#FFFFFF"
                    font.pixelSize: 13
                    anchors.verticalCenter: parent.verticalCenter
                }
                
                Text {
                    text: "/"
                    color: "#999999"
                    font.pixelSize: 13
                    anchors.verticalCenter: parent.verticalCenter
                }
                
                Text {
                    id: totalTime
                    text: Utils.formatTime(player ? player.duration : 0)
                    color: "#FFFFFF"
                    font.pixelSize: 13
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
            
            // Spacer
            Item {
                Layout.fillWidth: true
            }
            
            // Right side - Additional controls
            Row {
                spacing: 5
                Layout.alignment: Qt.AlignVCenter
                
                // Screenshot button
                Button {
                    id: screenshotButton
                    width: 40
                    height: 40
                    flat: true
                    icon.source: "../../resources/screenshot.svg"
                    icon.color: "#FFFFFF"
                    icon.width: 28
                    icon.height: 28
                    onClicked: {
                        if (mainWindow && typeof mainWindow.takeScreenshot === "function") {
                            mainWindow.takeScreenshot()
                        }
                    }
                    
                    background: Rectangle {
                        radius: 4
                        color: parent.pressed ? "#40FFFFFF" : (parent.hovered ? "#20FFFFFF" : "transparent")
                    }
                    
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("截图 (Ctrl+S)")
                }
                
                // Settings button
                Button {
                    id: settingsButton
                    width: 40
                    height: 40
                    flat: true
                    icon.source: "../../resources/settings.svg"
                    icon.color: "#FFFFFF"
                    icon.width: 28
                    icon.height: 28
                    onClicked: {
                        // 通过settingsDialogRef属性访问设置对话框
                        if (controlPanel.settingsDialogRef) {
                            controlPanel.settingsDialogRef.open()
                        }
                    }
                    
                    background: Rectangle {
                        radius: 4
                        color: parent.pressed ? "#40FFFFFF" : (parent.hovered ? "#20FFFFFF" : "transparent")
                    }
                    
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("设置")
                }
                
                // Playback speed control with hover expand
                SpeedControl {
                    id: speedControlItem
                    width: 60
                    height: 40
                }
                
                // Volume control with vertical slider
                VolumeControl {
                    id: volumeControlItem
                    width: 60
                    height: 40
                }
                
                // Fullscreen button
                Button {
                    id: fullscreenButton
                    width: 36
                    height: 36
                    flat: true
                    icon.source: (mainWindow && mainWindow.visibility === Window.FullScreen) ? "../../resources/fullscreen-exit.svg" : "../../resources/fullscreen.svg"
                    icon.color: "#FFFFFF"
                    icon.width: 24
                    icon.height: 24
                    onClicked: {
                        mainWindow.toggleFullscreen()
                    }
                    
                    background: Rectangle {
                        radius: 4
                        color: parent.pressed ? "#40FFFFFF" : (parent.hovered ? "#20FFFFFF" : "transparent")
                    }
                    
                    ToolTip.visible: hovered
                    ToolTip.text: (mainWindow && mainWindow.visibility === Window.FullScreen) ? qsTr("退出全屏 (F11)") : qsTr("全屏 (F11)")
                }
            }
        }
    }


    // Show/hide animation
    function show() {
        opacity = 1
    }
    
    function hide() {
        opacity = 0
    }
    
    Behavior on opacity {
        NumberAnimation { duration: 300 }
    }
}

