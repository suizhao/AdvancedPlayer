#ifndef MEDIAPLAYER_H
#define MEDIAPLAYER_H

#include <QObject>
#include <QString>
#include <QUrl>
#include <QTimer>
#include <QMetaObject>
#include <memory>
#include <chrono>
#include <vector>
namespace AdvancedPlayer {

class PlaybackController;
class ScreenshotCapture;
class VideoOutput;

/**
 * @brief 主媒体播放器类
 * 
 * 这是暴露给 QML 的主接口类，负责协调各个子系统
 */
class MediaPlayer : public QObject {
    Q_OBJECT
    
    // QML 属性
    Q_PROPERTY(bool isPlaying READ isPlaying NOTIFY playbackStateChanged)
    Q_PROPERTY(bool isPaused READ isPaused NOTIFY playbackStateChanged)
    Q_PROPERTY(double position READ position WRITE setPosition NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(double previousVolume READ previousVolume NOTIFY previousVolumeChanged)
    Q_PROPERTY(bool muted READ isMuted WRITE setMuted NOTIFY mutedChanged)
    Q_PROPERTY(double playbackSpeed READ playbackSpeed WRITE setPlaybackSpeed NOTIFY playbackSpeedChanged)
    Q_PROPERTY(QString currentFile READ currentFile NOTIFY currentFileChanged)
    Q_PROPERTY(bool isLoading READ isLoading NOTIFY loadingStateChanged)
    
    // 视频信息属性
    Q_PROPERTY(int videoWidth READ videoWidth NOTIFY videoInfoChanged)
    Q_PROPERTY(int videoHeight READ videoHeight NOTIFY videoInfoChanged)
    Q_PROPERTY(QString videoCodec READ videoCodec NOTIFY videoInfoChanged)
    Q_PROPERTY(double frameRate READ frameRate NOTIFY videoInfoChanged)
    Q_PROPERTY(qint64 videoBitrate READ videoBitrate NOTIFY videoInfoChanged)
    
    // 音频信息属性
    Q_PROPERTY(QString audioCodec READ audioCodec NOTIFY videoInfoChanged)
    Q_PROPERTY(qint64 audioBitrate READ audioBitrate NOTIFY videoInfoChanged)
    Q_PROPERTY(int audioSampleRate READ audioSampleRate NOTIFY videoInfoChanged)
    Q_PROPERTY(int audioChannels READ audioChannels NOTIFY videoInfoChanged)
    
public:
    explicit MediaPlayer(QObject* parent = nullptr);
    ~MediaPlayer() override;
    
    // 属性访问器
    bool isPlaying() const { return isPlaying_; }
    bool isPaused() const { return isPaused_; }
    double position() const { return position_; }
    double duration() const { return duration_; }
    double volume() const { return volume_; }
    double previousVolume() const { return previousVolume_; }
    bool isMuted() const { return muted_; }
    double playbackSpeed() const { return playbackSpeed_; }
    QString currentFile() const { return currentFile_; }
    bool isLoading() const { return isLoading_; }
    
    // 视频信息访问器
    int videoWidth() const;
    int videoHeight() const;
    QString videoCodec() const;
    double frameRate() const;
    qint64 videoBitrate() const;
    
    // 音频信息访问器
    QString audioCodec() const;
    qint64 audioBitrate() const;
    int audioSampleRate() const;
    int audioChannels() const;
    
    // 属性设置器
    void setPosition(double pos);
    void setVolume(double vol);
    void setMuted(bool muted);
    void setPlaybackSpeed(double speed);
    
public slots:
    /**
     * @brief 打开媒体文件或网络流
     * @param fileUrl 文件 URL 或网络流 URL（支持 http、https、rtmp、rtsp 等）
     */
    void openFile(const QUrl& fileUrl);
    
    /**
     * @brief 播放
     */
    void play();
    
    /**
     * @brief 暂停
     */
    void pause();
    
    /**
     * @brief 停止
     */
    void stop();
    
    /**
     * @brief 关闭当前媒体文件
     * 停止播放、清理所有资源
     */
    void closeMedia();
    
    /**
     * @brief 切换播放/暂停状态
     */
    void togglePlayPause();
    
    /**
     * @brief 切换静音状态
     */
    void toggleMute();
    
    /**
     * @brief 播放上一个文件
     */
    void playPrevious();
    
    /**
     * @brief 播放下一个文件
     */
    void playNext();
    
    /**
     * @brief 捕获当前帧
     * @param outputPath 输出路径
     */
    void captureScreenshot(const QString& outputPath);
    
    /**
     * @brief 设置视频输出组件
     * @param videoOutput VideoOutput实例（QML组件）
     */
    void setVideoOutput(AdvancedPlayer::VideoOutput* videoOutput);
    
signals:
    void playbackStateChanged();
    void positionChanged();
    void durationChanged();
    void volumeChanged();
    void previousVolumeChanged();
    void mutedChanged();
    void playbackSpeedChanged();
    void currentFileChanged();
    void loadingStateChanged();
    void videoInfoChanged();

    void error(const QString& message);
    void screenshotSaved(const QString& path);
    void screenshotFailed(const QString& errorMessage);
    void requestNextFile();
    void requestPreviousFile();
    
private:
    std::unique_ptr<PlaybackController> controller_{nullptr};
    std::vector<PlaybackController*> retiredControllers_{}; // 退役控制器等待回收
    std::unique_ptr<ScreenshotCapture> screenshotCapture_{nullptr};
    VideoOutput* boundVideoOutput_{nullptr};
    QTimer* retiredReclaimTimer_{nullptr};
    bool retiredReclaimInProgress_{false};
    PlaybackController* reclaimingController_{nullptr};     // 当前正在异步回收的控制器
    QMetaObject::Connection reclaimCloseConnection_{};
    static constexpr int RETIRED_CONTROLLER_LIMIT = 2;
    static constexpr int RETIRED_RECLAIM_INTERVAL_MS = 1200;
    
    bool isPlaying_{false};
    bool isPaused_{false};
    double position_{0.0};
    double duration_{0.0};
    double volume_{1.0};
    double previousVolume_{1.0};
    bool muted_{false};
    double playbackSpeed_{1.0};
    QString currentFile_{""};
    bool isLoading_{false};
    bool wasPlayingBeforeStopped_{false};               // 标记停止前是否在播放，用于检测自动播放
    
    // 防抖机制：防止短时间内重复调用 openFile
    QString pendingFilePath_{""};                           // 待打开的文件路径
    std::chrono::steady_clock::time_point lastOpenTime_{}; // 上次 openFile 调用时间
    static constexpr int OPEN_FILE_DEBOUNCE_MS = 100;   // 防抖间隔（毫秒）
    bool autoNextInFlight_{false};                       // 自动下一首单次闸门，避免 close/open 交叠
    bool deferredOpenInProgress_{false};                 // 延迟重开流程进行中
    QUrl deferredOpenUrl_{};                             // 延迟重开的目标 URL
    QMetaObject::Connection closeCompletedConnection_{}; // 异步关闭完成连接
    
    // 记住播放位置相关
    QString currentFilePath_{""};                           // 当前文件的绝对路径（用于保存位置）
    QTimer* positionSaveTimer_{nullptr};                         // 定期保存播放位置的定时器
    static constexpr int POSITION_SAVE_INTERVAL_MS = 5000; // 每5秒保存一次位置
    void saveCurrentPosition();                         // 保存当前播放位置
    void restorePlaybackPosition();                     // 恢复播放位置
    void bindControllerSignals();
    void rotatePlaybackControllerForAutoNext();
    void triggerRetiredControllerReclaim();
    void reclaimOneRetiredController();
};

} // namespace AdvancedPlayer

#endif // MEDIAPLAYER_H

