/**
 * @file VideoRenderThread.h
 * @brief 视频渲染线程
 * 
 * 职责：
 * - 从视频队列取帧
 * - 执行音视频同步决策
 * - 渲染视频帧到 VideoOutput
 */

#ifndef VIDEORENDERTHREAD_H
#define VIDEORENDERTHREAD_H

#include "RenderContext.h"
#include <QObject>
#include <atomic>
#include <memory>
#include <thread>
#include <mutex>
#include <deque>

// 前向声明
struct AVFrame;

namespace AdvancedPlayer {

/**
 * @brief 视频渲染线程
 */
class VideoRenderThread : public QObject {
    Q_OBJECT
    
public:
    explicit VideoRenderThread(QObject* parent = nullptr);
    ~VideoRenderThread() override;
    
    // ==================== 线程控制 ====================
    
    bool start(const RenderContext& ctx, const VideoSyncConfig& syncConfig,
               std::atomic<bool>* videoEof, std::atomic<bool>* audioEof);
    void requestStop();
    void joinAndReset();
    void stop();
    bool isRunning() const;
    
    // ==================== 当前帧访问 ====================
    
    AVFrame* getCurrentFrameClone();
    
    // ==================== 同步配置 ====================
    
    void setSyncConfig(const VideoSyncConfig& config) { syncConfig_ = config; }
    const VideoSyncConfig& getSyncConfig() const { return syncConfig_; }
    
    void resetSyncStats();
    
signals:
    void frameReady(AVFrame* frame);
    void positionChanged(int64_t positionMs);
    void playbackFinished();
    void error(const QString& message);

private:
    // ==================== 渲染循环 ====================
    
    void videoLoop(std::stop_token stoken);
    
    // ==================== 帧处理 ====================
    
    bool processVideoFrameWithSync(std::stop_token& stoken);
    
    // ==================== 同步决策 ====================
    
    /**
     * @brief 计算视频帧的同步决策
     * @param videoPts 视频帧 PTS（秒）
     * @param audioPts 音频播放位置（秒）
     * @return 延迟时间（毫秒），-1 表示跳帧，0 表示立即渲染
     */
    int calculateSyncAction(double videoPts, double audioPts);
    
    /**
     * @brief 纯视频流的帧率控制
     * @return 距离下一帧的延迟时间（毫秒）
     */
    int calculateFrameRateDelay();
    
    // ==================== 辅助方法 ====================
    
    void handlePauseState(PlaybackState state);
    void handleResumeFromPause();
    void cacheCurrentFrame(AVFrame* frame);
    bool isAllEofReceived() const;
    void updateDriftHistory(double driftMs);
    double calculateSyncQuality() const;
    
    // ==================== 成员变量 ====================
    
    std::unique_ptr<std::jthread> videoThread_{nullptr};
    RenderContext ctx_{};
    
    // EOF 状态（通过指针访问，由 PlaybackController 管理）
    std::atomic<bool>* videoEofReceived_{nullptr};
    std::atomic<bool>* audioEofReceived_{nullptr};
    
    // 暂停状态
    bool wasPaused_{false};
    
    // 当前帧缓存
    AVFrame* currentFrame_{nullptr};
    mutable std::mutex frameMutex_;
    
    // 帧计数
    uint64_t frameCounter_{0};
    
    // ==================== 同步状态 ====================
    
    VideoSyncConfig syncConfig_{};
    
    // 同步统计
    std::atomic<uint64_t> framesRendered_{0};
    std::atomic<uint64_t> framesSkipped_{0};
    std::atomic<uint64_t> framesDelayed_{0};
    std::atomic<int> consecutiveSkips_{0};
    
    // 漂移历史
    mutable std::mutex driftMutex_;
    std::deque<double> driftHistory_{};
    static constexpr size_t DRIFT_HISTORY_SIZE = 60;
    double totalDrift_{0.0};
    
    // 帧率控制
    std::chrono::steady_clock::time_point lastFrameTime_{};
    bool lastFrameTimeValid_{false};
    double frameIntervalMs_{33.33};
    
    // 播放结束信号只发送一次
    std::atomic<bool> playbackFinishedEmitted_{false};
    
    // 禁用拷贝
    VideoRenderThread(const VideoRenderThread&) = delete;
    VideoRenderThread& operator=(const VideoRenderThread&) = delete;
};

} // namespace AdvancedPlayer

#endif // VIDEORENDERTHREAD_H
