import QtQuick
import QtQuick.Controls

// 通用通知提示组件
Rectangle {
    id: notificationToast
    width: 400
    height: 50
    radius: 8
    color: "#323232"
    opacity: 0
    visible: opacity > 0

    anchors.horizontalCenter: parent ? parent.horizontalCenter : undefined
    anchors.bottom: parent ? parent.bottom : undefined
    anchors.bottomMargin: 100

    // 外部可设置的消息
    property string message: ""

    // 文本
    Text {
        anchors.centerIn: parent
        text: notificationToast.message
        color: "#FFFFFF"
        font.pixelSize: 14
    }

    // 出入场动画
    SequentialAnimation on opacity {
        id: toastAnimation
        running: false
        NumberAnimation { from: 0; to: 1; duration: 200 }
        PauseAnimation { duration: 2000 }
        NumberAnimation { from: 1; to: 0; duration: 300 }
    }

    // 对外暴露的显示方法
    function show(msg) {
        message = msg
        toastAnimation.restart()
    }
}

