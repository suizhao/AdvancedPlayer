import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Dialog {
    id: settingsDialog
    title: qsTr("设置")
    width: 700
    height: 550
    modal: true
    anchors.centerIn: Overlay.overlay
    
    // 临时设置变量，用于在应用前保存用户修改
    property bool tempRememberPosition: false
    property bool tempAutoPlayNext: false
    property bool tempHardwareAccel: false
    property string tempScreenshotDir: ""
    property string tempScreenshotFormat: "png"
    
    background: Rectangle {
        color: "#1E1E1E"
        radius: 8
    }
    
    header: Rectangle {
        height: 60
        color: "#252525"
        radius: 8
        
        Text {
            anchors.left: parent.left
            anchors.leftMargin: 20
            anchors.verticalCenter: parent.verticalCenter
            text: settingsDialog.title
            color: "#FFFFFF"
            font.pixelSize: 18
            font.bold: true
        }
    }
    
    contentItem: RowLayout {
        spacing: 0
        
        // Left tab bar
        ListView {
            id: settingsTabBar
            Layout.preferredWidth: 180
            Layout.fillHeight: true
            
            model: ListModel {
                ListElement { name: "General"; }
                ListElement { name: "Playback";}
                ListElement { name: "Video"; }
                ListElement { name: "Audio";}
                ListElement { name: "Interface"; }
            }
            
            currentIndex: 0
            
            delegate: ItemDelegate {
                width: ListView.view.width
                height: 50
                highlighted: ListView.isCurrentItem
                
                background: Rectangle {
                    color: parent.highlighted ? "#303030" : 
                           (parent.hovered ? "#252525" : "transparent")
                }
                
                contentItem: Row {
                    spacing: 15
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 20

                    Text {
                        text: model.name
                        color: "#FFFFFF"
                        font.pixelSize: 14
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                
                onClicked: settingsTabBar.currentIndex = index
            }
        }

        // Separator
        Rectangle {
            Layout.preferredWidth: 1
            Layout.fillHeight: true
            color: "#303030"
        }
        
        // Right settings panel
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: settingsTabBar.currentIndex
            
            // General settings
            ScrollView {
                clip: true
                
                ColumnLayout {
                    width: parent.width - 40
                    anchors.margins: 20
                    spacing: 20
                    
                    GroupBox {
                        Layout.fillWidth: true
                        title: qsTr("常规")
                        
                        background: Rectangle {
                            color: "#252525"
                            radius: 6
                        }
                        
                        label: Text {
                            text: parent.title
                            color: "#FFFFFF"
                            font.bold: true
                            padding: 10
                        }
                        
                        ColumnLayout {
                            width: parent.width
                            spacing: 15
                            
                            CheckBox {
                                id: rememberPositionCheckBox
                                text: qsTr("记住播放位置")
                                checked: settingsDialog.tempRememberPosition
                                
                                onCheckedChanged: {
                                    settingsDialog.tempRememberPosition = checked
                                }
                                
                                contentItem: Text {
                                    text: parent.text
                                    color: "#FFFFFF"
                                    leftPadding: parent.indicator.width + parent.spacing
                                }
                            }
                            
                            CheckBox {
                                id: autoPlayNextCheckBox
                                text: qsTr("自动播放")
                                checked: settingsDialog.tempAutoPlayNext
                                
                                onCheckedChanged: {
                                    settingsDialog.tempAutoPlayNext = checked
                                }
                                
                                contentItem: Text {
                                    text: parent.text
                                    color: "#FFFFFF"
                                    leftPadding: parent.indicator.width + parent.spacing
                                }
                            }
                            
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 6
                                
                                Text {
                                    text: qsTr("截图目录:")
                                    color: "#FFFFFF"
                                    Layout.preferredWidth: 80
                                    font.pixelSize: 13
                                }
                                
                                TextField {
                                    id: screenshotDirField
                                    text: settingsDialog.tempScreenshotDir
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 32
                                    readOnly: true
                                    font.pixelSize: 12
                                    
                                    background: Rectangle {
                                        color: "#303030"
                                        radius: 4
                                        border.color: parent.focus ? "#00A1D6" : "#606060"
                                    }
                                    
                                    color: "#FFFFFF"
                                }
                                
                                Button {
                                    text: qsTr("浏览")
                                    Layout.preferredWidth: 80
                                    Layout.preferredHeight: 32
                                    onClicked: screenshotFolderDialog.open()
                                    
                                    background: Rectangle {
                                        color: parent.pressed ? "#00A1D6" : "#303030"
                                        radius: 4
                                    }
                                    
                                    contentItem: Text {
                                        text: parent.text
                                        color: "#FFFFFF"
                                        font.pixelSize: 12
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 6

                                Text {
                                    text: qsTr("截图格式:")
                                    color: "#FFFFFF"
                                    Layout.preferredWidth: 80
                                    font.pixelSize: 13
                                }

                                ComboBox {
                                    id: screenshotFormatCombo
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 32
                                    model: [ "png", "jpg", "bmp" ]
                                    currentIndex: 0
                                    onActivated: {
                                        settingsDialog.tempScreenshotFormat = currentText
                                    }

                                    background: Rectangle {
                                        color: "#303030"
                                        radius: 4
                                        border.color: screenshotFormatCombo.activeFocus ? "#00A1D6" : "#606060"
                                    }

                                    contentItem: Text {
                                        leftPadding: 10
                                        rightPadding: 10
                                        text: screenshotFormatCombo.displayText
                                        color: "#FFFFFF"
                                        verticalAlignment: Text.AlignVCenter
                                        font.pixelSize: 12
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                        }
                    }
                }
            }
            
            // Playback settings
            ScrollView {
                clip: true
                
                ColumnLayout {
                    width: parent.width - 40
                    anchors.margins: 20
                    spacing: 20
                    
                    GroupBox {
                        Layout.fillWidth: true
                        title: qsTr("播放选项")
                        
                        background: Rectangle {
                            color: "#252525"
                            radius: 6
                        }
                        
                        label: Text {
                            text: parent.title
                            color: "#FFFFFF"
                            font.bold: true
                            padding: 10
                        }
                        
                        ColumnLayout {
                            width: parent.width
                            spacing: 15
                            
                            CheckBox {
                                id: hardwareAccelCheckBox
                                text: qsTr("启用硬件加速")
                                checked: settingsDialog.tempHardwareAccel
                                
                                onCheckedChanged: {
                                    settingsDialog.tempHardwareAccel = checked
                                }
                                
                                contentItem: Text {
                                    text: parent.text
                                    color: "#FFFFFF"
                                    leftPadding: parent.indicator.width + parent.spacing
                                }
                            }
                            
                            Text {
                                text: qsTr("提示：硬件加速设置更改后，需要重新打开文件才能生效")
                                color: "#808080"
                                font.pixelSize: 11
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }
                            
                        }
                    }
                }
            }
            
            // Video settings
            ScrollView {
                clip: true
                
                ColumnLayout {
                    width: parent.width - 40
                    anchors.margins: 20
                    spacing: 20
                    
                    Text {
                        text: qsTr("视频设置")
                        color: "#FFFFFF"
                        font.pixelSize: 16
                    }
                    
                    Text {
                        text: qsTr("视频相关设置将在后续版本中添加")
                        color: "#808080"
                    }
                }
            }
            
            // Audio settings
            ScrollView {
                clip: true
                
                ColumnLayout {
                    width: parent.width - 40
                    anchors.margins: 20
                    spacing: 20
                    
                    Text {
                        text: qsTr("音频设置")
                        color: "#FFFFFF"
                        font.pixelSize: 16
                    }
                    
                    Text {
                        text: qsTr("音频相关设置将在后续版本中添加")
                        color: "#808080"
                    }
                }
            }
            
            // Interface settings
            ScrollView {
                clip: true
                
                ColumnLayout {
                    width: parent.width - 40
                    anchors.margins: 20
                    spacing: 20
                    
                    Text {
                        text: qsTr("界面设置")
                        color: "#FFFFFF"
                        font.pixelSize: 16
                    }
                    
                    Text {
                        text: qsTr("界面相关设置将在后续版本中添加")
                        color: "#808080"
                    }
                }
            }
        }
    }
    
    footer: Rectangle {
        height: 60
        color: "#252525"
        radius: 8
        
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            spacing: 0
            // 左侧空白
            Item { Layout.fillWidth: true }

            Button {
                text: qsTr("取消")
                background: Rectangle {
                    color: parent.pressed ? "#303030" : "#252525"
                    radius: 4
                }
                contentItem: Text {
                    text: parent.text
                    color: "#FFFFFF"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                onClicked: {
                    settingsDialog.close()
                }
            }
            
            // 中间空白
            Item { Layout.fillWidth: true }

            Button {
                text: qsTr("应用")
                background: Rectangle {
                    color: parent.pressed ? "#303030" : "#252525"
                    radius: 4
                }
                contentItem: Text {
                    text: parent.text
                    color: "#FFFFFF"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                
                onClicked: {
                    // 应用所有临时设置到settingsManager
                    settingsManager.rememberPlaybackPosition = settingsDialog.tempRememberPosition
                    settingsManager.autoPlayNext = settingsDialog.tempAutoPlayNext
                    settingsManager.hardwareAccelerationEnabled = settingsDialog.tempHardwareAccel
                    settingsManager.screenshotDirectory = settingsDialog.tempScreenshotDir
                    settingsManager.screenshotFormat = settingsDialog.tempScreenshotFormat
                    // 保存设置
                    settingsManager.save()
                    // 关闭对话框
                    settingsDialog.close()
                }
            }
            
            // 右侧空白
            Item { Layout.fillWidth: true }
        }
    }
    
    // 截图保存路径对话框
    FolderDialog {
        id: screenshotFolderDialog
        title: qsTr("选择截图保存目录")
        onAccepted: {
            var folderPath = selectedFolder.toString()  // 将QUrl转换为本地文件路径
            // 处理file:///前缀（Windows和Unix都适用）
            if (folderPath.startsWith("file:///")) {
                folderPath = folderPath.substring(8)
            } else if (folderPath.startsWith("file://")) {
                folderPath = folderPath.substring(7)
            }

            folderPath = folderPath.replace(/\|/g, ":")   // 处理Windows路径中的|符号（QUrl可能将:转换为|）

            settingsDialog.tempScreenshotDir = folderPath // 更新临时变量，而不是直接更新settingsManager
        }
    }

    onOpened: {
        tempRememberPosition = settingsManager.rememberPlaybackPosition
        tempAutoPlayNext = settingsManager.autoPlayNext
        tempHardwareAccel = settingsManager.hardwareAccelerationEnabled
        tempScreenshotDir = settingsManager.screenshotDirectory
        tempScreenshotFormat = settingsManager.screenshotFormat
        var formatIndex = screenshotFormatCombo.find(tempScreenshotFormat)
        screenshotFormatCombo.currentIndex = formatIndex >= 0 ? formatIndex : 0
    }
}

