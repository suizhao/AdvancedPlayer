#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QIcon>
#include <QSurfaceFormat>
#include <QMetaType>
#include <QDir>
#include <QFileInfo>
#include <QCoreApplication>

// 包含所有需要注册到 QML 的类
#include "src/core/MediaPlayer.h"
#include "src/ui/PlaylistModel.h"
#include "src/ui/NetworkPlaylistModel.h"
#include "src/ui/SettingsManager.h"
#include "src/utils/ScreenshotCapture.h"
#include "src/utils/FileScanner.h"
#include "src/video/VideoOutput.h"

using namespace AdvancedPlayer;

int main(int argc, char *argv[])
{
    // ========== 强制Qt使用OpenGL渲染后端 ==========
    // QQuickFramebufferObject必须在OpenGL渲染后端下才能工作
    qputenv("QSG_RHI_BACKEND", "opengl");
    qputenv("QT_QUICK_BACKEND", "");  // 清除可能的software backend设置
    
    // 设置 Qt Quick Controls 样式为 Basic，支持自定义 contentItem
    // 这样可以避免原生样式不支持自定义的警告
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
    
    // 设置OpenGL表面格式
    QSurfaceFormat format;
    format.setVersion(3, 3);  // OpenGL 3.3
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    format.setSamples(0);  // 禁用多重采样以提高性能
    QSurfaceFormat::setDefaultFormat(format);
    
    QGuiApplication app(argc, argv);
    
    // 设置应用程序信息
    app.setOrganizationName("AdvancedPlayer");
    app.setOrganizationDomain("advancedplayer.com");
    app.setApplicationName("Advanced Media Player");
    app.setApplicationVersion("1.0.0");
    
    // 注册 QML 类型
    qmlRegisterType<MediaPlayer>("AdvancedPlayer", 1, 0, "MediaPlayer");
    qmlRegisterType<PlaylistModel>("AdvancedPlayer", 1, 0, "PlaylistModel");
    qmlRegisterType<NetworkPlaylistModel>("AdvancedPlayer", 1, 0, "NetworkPlaylistModel");
    qmlRegisterType<ScreenshotCapture>("AdvancedPlayer", 1, 0, "ScreenshotCapture");
    qmlRegisterType<FileScanner>("AdvancedPlayer", 1, 0, "FileScanner");
    qmlRegisterType<VideoOutput>("AdvancedPlayer", 1, 0, "VideoOutput");
    
    // ========== 注册VideoOutput指针类型 ==========
    // 让Qt元对象系统识别VideoOutput*作为有效的参数类型
    qRegisterMetaType<VideoOutput*>("VideoOutput*");
    
    QQmlApplicationEngine engine;
    
    // 创建并注册单例对象
    SettingsManager& settings = SettingsManager::instance();
    engine.rootContext()->setContextProperty("settingsManager", &settings);

    MediaPlayer mediaPlayer;
    engine.rootContext()->setContextProperty("player", &mediaPlayer);
    
    PlaylistModel playlistModel;
    engine.rootContext()->setContextProperty("playlistModel", &playlistModel);

    NetworkPlaylistModel networkPlaylistModel;
    engine.rootContext()->setContextProperty("networkPlaylistModel", &networkPlaylistModel);
    
    // =================================添加默认本地视频==================================

    // 获取项目根目录（从可执行文件所在目录向上查找，找到包含"LocalVideoTest"或"AdvancedPlayerIndexes"文件夹的目录）
    QString projectRootDir;
    QString appDir = QCoreApplication::applicationDirPath();
    QDir dir(appDir);
    
    // 向上查找项目根目录，最多查找5层
    for (int i = 0; i < 5; ++i) {
        QString testDir = dir.absolutePath() + "/LocalVideoTest";
        QString indexDir = dir.absolutePath() + "/AdvancedPlayerIndexes";
        if (QDir(testDir).exists() || QDir(indexDir).exists()) {
            projectRootDir = dir.absolutePath();
            break;
        }
        if (!dir.cdUp()) {
            break;
        }
    }
    
    // 如果找不到，使用可执行文件目录的父目录（通常是build目录的父目录）
    if (projectRootDir.isEmpty()) {
        QDir appDirObj(appDir);
        if (appDirObj.cdUp()) {
            projectRootDir = appDirObj.absolutePath();
        } else {
            projectRootDir = appDir;
        }
    }
    
    qInfo() << "[main] Project root directory:" << projectRootDir;
    
    // 添加默认测试视频文件到播放列表
    QString testVideoDir = projectRootDir + "/LocalVideoTest";
    QString luofengPath = testVideoDir + "/luofeng.mp4";
    QString zhaohuaPath = testVideoDir + "/zhaohua.mp4";
    QString haiyuniPath = testVideoDir + "/haiyuni.mp4";
    
    // 检查并添加文件
    QFileInfo luofengInfo(luofengPath);
    if (luofengInfo.exists()) {
        playlistModel.addMedia(luofengPath);
        qInfo() << "[main] Added default video: luofeng.mp4";
    } else {
        qWarning() << "[main] Default test video does not exist:" << luofengPath;
    }
    
    QFileInfo zhaohuaInfo(zhaohuaPath);
    if (zhaohuaInfo.exists()) {
        playlistModel.addMedia(zhaohuaPath);
        qInfo() << "[main] Added default video: zhaohua.mp4";
    } else {
        qWarning() << "[main] Default test video does not exist:" << zhaohuaPath;
    }

    QFileInfo haiyuniInfo(haiyuniPath);
    if (haiyuniInfo.exists()) {
        playlistModel.addMedia(haiyuniPath);
        qInfo() << "[main] Added default video: haiyuni.mp4";
    } else {
        qWarning() << "[main] Default test video does not exist:" << haiyuniPath;
    }
    
    // ==============================connect处理=============================

    // 连接本地播放列表和播放器
    QObject::connect(&playlistModel, &PlaylistModel::playRequested,
                     &mediaPlayer, [&mediaPlayer](const QString& filePath) {
        qInfo() << "[main] Received playRequested signal";
        QUrl fileUrl = QUrl::fromLocalFile(filePath);
        mediaPlayer.openFile(fileUrl);
    });
    
    // 连接网络播放列表和播放器
    QObject::connect(&networkPlaylistModel, &NetworkPlaylistModel::playRequested,
                     &mediaPlayer, [&mediaPlayer](const QString& streamUrl) {
        QUrl url(streamUrl);
        mediaPlayer.openFile(url);
    });
    
    QObject::connect(&mediaPlayer, &MediaPlayer::requestNextFile,
                     &playlistModel, &PlaylistModel::playNext);
    
    QObject::connect(&mediaPlayer, &MediaPlayer::requestPreviousFile,
                     &playlistModel, &PlaylistModel::playPrevious);
    
    // 添加QML警告处理
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::warnings,
        [](const QList<QQmlError> &warnings) {
            for (const QQmlError &warning : warnings) {
                qWarning() << "[QML Warning]" << warning.toString();
            }
        });
    
    // 加载 QML
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { 
            qCritical() << "[main] Failed to create QML object!";
            QCoreApplication::exit(-1); 
        },
        Qt::QueuedConnection);
    
    qInfo() << "[main] Start loading QML module...";
    engine.loadFromModule("AdvancedPlayer", "HomePage");
    
    if (engine.rootObjects().isEmpty()) {
        qCritical() << "[main] Error: root object not found, QML load failed";
        return -1;
    }
    
    qInfo() << "[main] QML loaded successfully, root object count:" << engine.rootObjects().size();
    qInfo() << "[main] Main window created successfully - rendering video with OpenGL";
    
    return app.exec();
}
