/**
 * @file PlaybackController.h
 * @brief 播放控制器
 * 
 * - 时钟逻辑内置于 AudioOutput
 * - 同步逻辑内置于 VideoRenderThread
 * - 直接管理 VideoRenderThread 和 AudioRenderThread
 */

#ifndef PLAYBACKCONTROLLER_H
#define PLAYBACKCONTROLLER_H

#include "RenderContext.h"
#include <QObject>
#include <QString>
#include <QPointer>
#include <atomic>
#include <memory>
#include <mutex>
#include <QMetaObject>

// 前向声明
struct AVFormatContext;
struct AVCodecContext;
struct AVPacket;
struct AVFrame;

namespace AdvancedPlayer {

struct AVFormatContextDeleter {
    void operator()(AVFormatContext* ctx) const;
};

struct AVCodecContextDeleter {
    void operator()(AVCodecContext* ctx) const;
};

// 前向声明
class IDecoder;
class AudioOutput;
class VideoOutput;
class AudioResampler;
class LockFreePacketQueue;
class LockFreeVideoFrameQueue;
class LockFreeAudioFrameQueue;
class DemuxThread;
class VideoDecodeThread;
class AudioDecodeThread;
class VideoRenderThread;
class AudioRenderThread;

/**
 * @brief 播放状态枚举
 */
enum class PlaybackState {
    Stopped,
    Playing,
    Paused,
    Loading,
    Error
};

/**
 * @brief 播放控制器
 * 
 * 管理渲染线程，同步音视频，控制播放状态
 */
class PlaybackController : public QObject {
    Q_OBJECT
    
public:
    explicit PlaybackController(QObject* parent = nullptr);
    ~PlaybackController() override;
    
    // ==================== 媒体控制 ====================
    
    bool openMedia(const QString& mediaPath, const bool isNetStream);
    void closeMedia();
    void closeMediaAsync();
    void play();
    void pause();
    void stop();
    bool seekTo(int64_t positionMs);
    
    // ==================== 倍速/音量控制 ====================
    
    void setPlaybackSpeed(double speed);
    void setVolume(float volume);
    float getVolume() const;
    void setMuted(bool muted);
    bool isMuted() const;
    
    // ==================== 状态查询 ====================
    
    PlaybackState getState() const { return state_.load(); }
    int64_t getPosition() const { return position_.load(); }
    int64_t getDuration() const { return duration_.load(); }
    
    // ==================== 媒体信息 ====================
    
    AVFrame* getCurrentFrame();
    int getVideoWidth() const;
    int getVideoHeight() const;
    QString getVideoCodec() const;
    double getFrameRate() const;
    int64_t getVideoBitrate() const;
    QString getAudioCodec() const;
    int64_t getAudioBitrate() const;
    int getAudioSampleRate() const;
    int getAudioChannels() const;
    
    void setVideoOutput(VideoOutput* videoOutput);

signals:
    void stateChanged(AdvancedPlayer::PlaybackState state);
    void positionChanged(int64_t positionMs);
    void durationChanged(int64_t durationMs);
    void errorOccurred(const QString& message);
    void closeMediaCompleted();
    
private:
    // ==================== 播放状态 ====================
    std::atomic<PlaybackState> state_{PlaybackState::Stopped};
    std::atomic<bool> isClosing_{false};
    std::atomic<int64_t> position_{0};
    std::atomic<int64_t> duration_{0};
    std::atomic<double> playbackSpeed_{1.0};
    
    // ==================== FFmpeg 资源 ====================
    std::unique_ptr<AVFormatContext, AVFormatContextDeleter> formatCtx_{nullptr};
    std::unique_ptr<AVCodecContext, AVCodecContextDeleter> videoCodecCtx_{nullptr};
    std::unique_ptr<AVCodecContext, AVCodecContextDeleter> audioCodecCtx_{nullptr};
    int videoStreamIndex_{-1};
    int audioStreamIndex_{-1};
    
    // ==================== 解码器 ====================
    std::unique_ptr<IDecoder> videoDecoder_{nullptr};
    std::unique_ptr<IDecoder> audioDecoder_{nullptr};
    
    // ==================== 音视频输出 ====================
    std::unique_ptr<AudioOutput> audioOutput_{nullptr};
    QPointer<VideoOutput> videoOutput_{nullptr};
    std::unique_ptr<AudioResampler> audioResampler_{nullptr};
    
    // ==================== 线程 ====================
    std::unique_ptr<DemuxThread> demuxThread_{nullptr};
    std::unique_ptr<VideoDecodeThread> videoDecodeThread_{nullptr};
    std::unique_ptr<AudioDecodeThread> audioDecodeThread_{nullptr};
    std::unique_ptr<VideoRenderThread> videoRenderThread_{nullptr};
    std::unique_ptr<AudioRenderThread> audioRenderThread_{nullptr};
    QMetaObject::Connection frameReadyConnection_{};
    
    // ==================== 渲染状态 ====================
    // EOF 状态（由 PlaybackController 管理，传递给渲染线程）
    std::atomic<bool> videoEofReceived_{false};
    std::atomic<bool> audioEofReceived_{false};
    
    // 播放结束标志（确保只处理一次播放结束事件，避免两个线程重复发出信号）
    std::atomic<bool> playbackFinishedEmitted_{false};
    
    // 同步配置
    VideoSyncConfig syncConfig_{};
    
    // ==================== 队列（无锁）====================
    std::unique_ptr<LockFreePacketQueue> videoPacketQueue_{nullptr};
    std::unique_ptr<LockFreePacketQueue> audioPacketQueue_{nullptr};
    std::unique_ptr<LockFreeAudioFrameQueue> audioFrameQueue_{nullptr};
    std::unique_ptr<LockFreeVideoFrameQueue> videoFrameQueue_{nullptr};
    
    // ==================== 资源保护 ====================
    mutable std::mutex resourceMutex_;
    
    // ==================== 其他状态 ====================
    QString currentFilePath_{""};
    bool isNetworkStream_{false};
    
    // ==================== 内部方法 ====================
    void startAllThread();
    RenderContext createRenderContext();
    bool initializeVideoDecoder();
    bool initializeAudioDecoder();
    void requestStopAllWorkerThreads();
    void joinAllWorkerThreads();
    void continueCloseMediaAsync();
    void ensureFrameReadyConnection();
    void cleanupFFmpegResources();
    void clearBuffers();

    bool closeAsyncInProgress_{false};
    int closeAsyncJoinStep_{0};

    // ==================== 网络IO中断控制 ====================
    std::atomic<bool> ioInterruptRequested_{false};
    static int ffmpegInterruptCallback(void* opaque);
};

} // namespace AdvancedPlayer

#endif // PLAYBACKCONTROLLER_H
