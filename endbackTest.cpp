#include <QCoreApplication>
#include <QFileInfo>
#include <QTimer>
#include <QDebug>
#include <QTextStream>

#include "src/core/PlaybackController.h"

using namespace AdvancedPlayer;

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    app.setApplicationName("endbackTest");
    app.setApplicationVersion("1.0.0");
    QTextStream in(stdin);
    QTextStream out(stdout);

    QString mediaPath = R"(C:\Users\suizhao\Desktop\DemoPlayer\LocalVideoTest\4k60fps.mp4)";
    // QString mediaPath = R"(https://media.w3.org/2010/05/sintel/trailer.mp4)";

    int durationMs = 212000;

    PlaybackController controller;

    QObject::connect(&controller, &PlaybackController::stateChanged,
                     [](PlaybackState state) {
        qInfo() << "[endbackTest] stateChanged =" << static_cast<int>(state);
    });
    QObject::connect(&controller, &PlaybackController::positionChanged,
                     [](int64_t positionMs) {
        qInfo() << "[endbackTest] positionChanged =" << positionMs << "ms";
    });
    QObject::connect(&controller, &PlaybackController::durationChanged,
                     [](int64_t durationMsSignal) {
        qInfo() << "[endbackTest] durationChanged =" << durationMsSignal << "ms";
    });
    QObject::connect(&controller, &PlaybackController::errorOccurred,
                     [&app](const QString& message) {
        qCritical() << "[endbackTest] errorOccurred:" << message;
        QTimer::singleShot(0, &app, &QCoreApplication::quit);
    });

    // 自动判断：本地文件存在则按文件打开，否则按网络流尝试
    bool isNetStream = false;
    QFileInfo localFile(mediaPath);
    if (!localFile.exists()) {
        qWarning() << "[endbackTest] Local file does not exist, trying as network stream:" << mediaPath;
        isNetStream = true;
    } else {
        mediaPath = localFile.absoluteFilePath();
    }

    qInfo() << "[endbackTest] openMedia path =" << mediaPath
            << ", isNetStream =" << isNetStream
            << ", testDurationMs =" << durationMs;

    if (!controller.openMedia(mediaPath, isNetStream)) {
        qCritical() << "[endbackTest] openMedia failed";
        return -2;
    }

    controller.play();

    QTimer::singleShot(durationMs, &app, [&controller, &app]() {
        qInfo() << "[endbackTest] Test duration reached, preparing stop + close";
        controller.stop();
        app.quit();
    });

    return app.exec();
}
