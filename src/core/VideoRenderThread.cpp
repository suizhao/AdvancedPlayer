/**
 * @file VideoRenderThread.cpp
 * @brief 视频渲染线程实现
 */

#include "VideoRenderThread.h"
#include "RenderContext.h"
#include "PlaybackController.h"  // PlaybackState
#include "LockFreeVideoFrameQueue.h"
#include "src/audio/AudioOutput.h"
#include <QDebug>
#include <QMetaObject>
#include <QMetaType>
#include <chrono>
#include <cmath>
#include <algorithm>

#ifdef HAS_FFMPEG
extern "C" {
#include <libavutil/frame.h>
}
#endif

namespace AdvancedPlayer {

// ==================== 构造与析构 ====================

VideoRenderThread::VideoRenderThread(QObject* parent)
    : QObject(parent) {
    qRegisterMetaType<AVFrame*>("AVFrame*");
    qDebug() << "[VideoRenderThread] Video render thread object created";
}

VideoRenderThread::~VideoRenderThread() {
    stop();
    
    {
        std::lock_guard<std::mutex> lock(frameMutex_);
#ifdef HAS_FFMPEG
        if (currentFrame_) {
            av_frame_free(&currentFrame_);
            currentFrame_ = nullptr;
        }
#endif
    }
    
    qDebug() << "[VideoRenderThread] Video render thread object destroyed";
}

// ==================== 线程控制 ====================

bool VideoRenderThread::start(const RenderContext& ctx, const VideoSyncConfig& syncConfig,
                              std::atomic<bool>* videoEof, std::atomic<bool>* audioEof) {
    if (videoThread_) {
        qWarning() << "[VideoRenderThread::start] Video render thread is already running";
        return false;
    }
    
    if (!ctx.hasVideoStream()) {
        qWarning() << "[VideoRenderThread::start] No video stream";
        return false;
    }
    
    ctx_ = ctx;
    syncConfig_ = syncConfig;
    videoEofReceived_ = videoEof;
    audioEofReceived_ = audioEof;

    if (!ctx_.playbackState) {
        qCritical() << "[VideoRenderThread::start] Invalid render context: playbackState is null";
        return false;
    }
    
    // 计算帧间隔
    frameIntervalMs_ = ctx.frameRate > 0 ? (1000.0 / ctx.frameRate) : 33.33;
    
    // 重置状态
    wasPaused_ = false;
    frameCounter_ = 0;
    resetSyncStats();
    playbackFinishedEmitted_.store(false);
    
    // 恢复队列
    if (ctx_.videoFrameQueue) {
        ctx_.videoFrameQueue->resume();
    }
    
    // 启动视频线程
    videoThread_ = std::make_unique<std::jthread>([this](std::stop_token stoken) {
        videoLoop(stoken);
    });
    
    qInfo() << "[VideoRenderThread::start] Video render thread started";
    return true;
}

void VideoRenderThread::requestStop() {
    if (!videoThread_) {
        return;
    }
    qInfo() << "[VideoRenderThread::requestStop] Request stop video render thread";

    // 先中止队列，让线程能够快速退出等待循环
    if (ctx_.videoFrameQueue) {
        ctx_.videoFrameQueue->abort();
    }
    
    // 请求停止
    videoThread_->request_stop();
}

void VideoRenderThread::joinAndReset() {
    if (!videoThread_) {
        return;
    }

    qInfo() << "[VideoRenderThread::joinAndReset] Joining video render thread";
    videoThread_.reset();
    qInfo() << "[VideoRenderThread::joinAndReset] Video render thread stopped";
}

void VideoRenderThread::stop() {
    requestStop();
    joinAndReset();
}

bool VideoRenderThread::isRunning() const {
    return videoThread_ && videoThread_->joinable();
}

// ==================== 当前帧访问 ====================

AVFrame* VideoRenderThread::getCurrentFrameClone() {
#ifdef HAS_FFMPEG
    std::lock_guard<std::mutex> lock(frameMutex_);
    if (currentFrame_) {
        return av_frame_clone(currentFrame_);
    }
#endif
    return nullptr;
}

// ==================== 同步统计 ====================

double VideoRenderThread::calculateSyncQuality() const {
    uint64_t rendered = framesRendered_.load();
    uint64_t skipped = framesSkipped_.load();
    uint64_t total = rendered + skipped;
    
    double skipRate = total > 0 ? (skipped * 100.0 / total) : 0.0;
    
    double avgDriftMs = 0.0;
    {
        std::lock_guard<std::mutex> lock(driftMutex_);
        if (!driftHistory_.empty()) {
            avgDriftMs = totalDrift_ / driftHistory_.size();
        }
    }
    
    double score = 100.0;
    score -= skipRate * 5.0;
    score -= std::abs(avgDriftMs) / 10.0;
    return std::max(0.0, std::min(100.0, score));
}

void VideoRenderThread::resetSyncStats() {
    framesRendered_.store(0);
    framesSkipped_.store(0);
    framesDelayed_.store(0);
    consecutiveSkips_.store(0);
    
    {
        std::lock_guard<std::mutex> lock(driftMutex_);
        driftHistory_.clear();
        totalDrift_ = 0.0;
    }
    
    lastFrameTimeValid_ = false;
}

// ==================== 视频渲染循环 ====================

void VideoRenderThread::videoLoop(std::stop_token stoken) {
    bool eofHandled = false;
    
#ifdef HAS_FFMPEG
    while (!stoken.stop_requested()) {
        if (eofHandled) {
            // EOF 后不立即自然退出，统一由外部 stop() 回收线程
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        // 检查播放状态
        if (!ctx_.playbackState) {
            break;
        }
        PlaybackState currentState = ctx_.playbackState->load();
        if (currentState != PlaybackState::Playing) {
            handlePauseState(currentState);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        
        handleResumeFromPause();
        
        if (stoken.stop_requested()) break;
        
        // 检查 EOF
        if (isAllEofReceived()) {
            bool expected = false;
            if (playbackFinishedEmitted_.compare_exchange_strong(expected, true)) {
                // 收敛到对象线程（主线程）发信号，避免在 worker 退出阶段触发 Qt 运行时路径
                QMetaObject::invokeMethod(this, [this]() {
                    emit playbackFinished();
                }, Qt::QueuedConnection);
            }
            eofHandled = true;
            continue;
        }
        
        // 处理视频帧（逐帧处理，确保精确的帧率控制）
        if (ctx_.hasVideoStream() && (!videoEofReceived_ || !videoEofReceived_->load())) {
            if (stoken.stop_requested()) break;
            processVideoFrameWithSync(stoken);
        }
        
        // 空闲休眠（仅关注视频队列）
        bool videoEmpty = !ctx_.videoFrameQueue || ctx_.videoFrameQueue->empty() || 
                         (videoEofReceived_ && videoEofReceived_->load());
        if (videoEmpty) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
#else
    Q_UNUSED(stoken);
#endif
    
    // 退出日志由 stop()（主线程）统一输出，避免 worker 退出路径调用 Qt 日志
}

// ==================== 视频处理 ====================

bool VideoRenderThread::processVideoFrameWithSync(std::stop_token& stoken) {
#ifdef HAS_FFMPEG
    if (!ctx_.videoFrameQueue) return false;
    
    AVFrame* frame = nullptr;
    double pts = 0.0;
    
    if (!ctx_.videoFrameQueue->pop(&frame, &pts)) {
        return false;  // 队列空或已中止
    }
    
    // EOF 检测
    if (LockFreeVideoFrameQueue::isEofMarker(frame)) {
        if (videoEofReceived_) {
            videoEofReceived_->store(true);
        }
        return true;
    }
    
    // ==================== 同步决策 ====================
    
    if (ctx_.hasAudioStream() && ctx_.audioOutput) {
        // 有音频流：音频驱动同步
        double audioPts = ctx_.audioOutput->getPlaybackTime();
        int syncAction = calculateSyncAction(pts, audioPts);
        
        // 递增帧计数器（统计所有处理的帧，包括跳帧）
        ++frameCounter_;
        
        if (syncAction < 0) {
            // ==================== 跳帧路径 ====================
            framesSkipped_.fetch_add(1);
            consecutiveSkips_.fetch_add(1);
            
            av_frame_free(&frame);
            return true;
        }
        
        if (syncAction > 0) {
            // 延迟
            std::this_thread::sleep_for(std::chrono::milliseconds(syncAction));
            framesDelayed_.fetch_add(1);
        }
        
    } else if (ctx_.audioOutput && ctx_.audioOutput->isUsingSystemClock()) {
        // 纯视频流：基于 PTS 时间轴速率缩放的帧率控制
        double speed = 1.0;
        if (ctx_.audioOutput) {
            speed = ctx_.audioOutput->getPlaybackSpeed();
        }
        
        double systemClockTime = ctx_.audioOutput->getSystemClockTime();
        double targetPts = systemClockTime * speed;
        double ptsDrift = pts - targetPts;
        double ptsDriftMs = ptsDrift * 1000.0;
        
        int delayMs = 0;
        if (ptsDriftMs > 50.0) {
            delayMs = static_cast<int>(ptsDriftMs / speed);
            delayMs = std::min(delayMs, 100);
        }
        
        if (delayMs > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
        }
        
        ++frameCounter_;
    }
    
    if (stoken.stop_requested()) {
        av_frame_free(&frame);
        return false;
    }
    
    // ==================== 渲染视频帧 ====================
    
    if (!ctx_.enableVideoFrameForwarding) {
        // 无 UI 场景：消费与同步视频帧，但不向上层转发
    } else if (ctx_.resourceMutex) {
        std::lock_guard<std::mutex> lock(*ctx_.resourceMutex);

        if (stoken.stop_requested()) {
            av_frame_free(&frame);
            return false;
        }

        // 通过独立 AVFrame 引用跨线程投递，避免在渲染线程直接操作 QQuickItem
        AVFrame* frameForGui = av_frame_alloc();
        if (!frameForGui) {
            av_frame_free(&frame);
            return false;
        }

        if (av_frame_ref(frameForGui, frame) < 0) {
            av_frame_free(&frameForGui);
            av_frame_free(&frame);
            return false;
        }

        emit frameReady(frameForGui);
        // 更新播放位置
        emit positionChanged(static_cast<int64_t>(pts * 1000));
    }
    
    // 缓存当前帧
    cacheCurrentFrame(frame);
    
    // 更新统计
    framesRendered_.fetch_add(1);
    consecutiveSkips_.store(0);
    
    av_frame_free(&frame);
    
#endif
    return true;
}

// ==================== 同步决策 ====================

int VideoRenderThread::calculateSyncAction(double videoPts, double audioPts) {
    double driftMs = (videoPts - audioPts) * 1000.0;
    
    // 更新漂移历史
    updateDriftHistory(driftMs);
    
    // 根据播放速度调整同步阈值
    double speed = 1.0;
    if (ctx_.audioOutput) {
        speed = ctx_.audioOutput->getPlaybackSpeed();
    }
    
    double thresholdScale = 1.0;
    if (speed > 1.0) {
        thresholdScale = 0.8 + 0.2 / speed;
    } else if (speed < 1.0) {
        thresholdScale = 1.0 + 0.2 * (1.0 - speed);
    }
    
    double adjustedSyncWindow = syncConfig_.syncWindowMs * thresholdScale;
    double adjustedDelayThreshold = syncConfig_.delayThresholdMs * thresholdScale;
    double adjustedMicroDelayThreshold = syncConfig_.microDelayThresholdMs * thresholdScale;
    double adjustedSoftSkipThreshold = syncConfig_.softSkipThresholdMs * thresholdScale;
    double adjustedHardSkipThreshold = syncConfig_.hardSkipThresholdMs * thresholdScale;
    
    // 限制最小阈值
    adjustedSyncWindow = std::max(adjustedSyncWindow, 30.0);
    adjustedDelayThreshold = std::max(adjustedDelayThreshold, 120.0);
    adjustedMicroDelayThreshold = std::max(adjustedMicroDelayThreshold, 80.0);
    adjustedSoftSkipThreshold = std::max(adjustedSoftSkipThreshold, 100.0);
    adjustedHardSkipThreshold = std::max(adjustedHardSkipThreshold, 150.0);
    
    if (driftMs > adjustedDelayThreshold) {
        int delayMs = std::min(
            static_cast<int>(driftMs - adjustedSyncWindow),
            syncConfig_.maxDelayMs
        );
        return delayMs;
    }
    else if (driftMs > adjustedMicroDelayThreshold) {
        return syncConfig_.microDelayMs;
    }
    else if (driftMs < -adjustedHardSkipThreshold) {
        uint64_t consecutiveSkips = consecutiveSkips_.load();
        if (consecutiveSkips < static_cast<uint64_t>(syncConfig_.maxConsecutiveSkips)) {
            return -1;
        }
    }
    else if (driftMs < -adjustedSoftSkipThreshold) {
        uint64_t consecutiveSkips = consecutiveSkips_.load();
        if (consecutiveSkips < static_cast<uint64_t>(syncConfig_.maxConsecutiveSkips)) {
            if (frameCounter_ % static_cast<uint64_t>(syncConfig_.softSkipRatio) == 0) {
                return -1;
            }
        }
    }
    
    return 0;
}

int VideoRenderThread::calculateFrameRateDelay() {
    if (!lastFrameTimeValid_) {
        lastFrameTime_ = std::chrono::steady_clock::now();
        lastFrameTimeValid_ = true;
        return 0;
    }

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - lastFrameTime_).count();
    
    double speed = 1.0;
    if (ctx_.audioOutput) {
        speed = ctx_.audioOutput->getPlaybackSpeed();
    }
    double targetIntervalMs = frameIntervalMs_ / speed;
    int delayMs = static_cast<int>(targetIntervalMs) - static_cast<int>(elapsed);
    
    if (delayMs <= 0) {
        lastFrameTime_ = now;
        return static_cast<int>(targetIntervalMs);
    }
    
    return delayMs;
}

// ==================== 辅助方法 ====================

void VideoRenderThread::handlePauseState(PlaybackState state) {
    if (!wasPaused_ && state == PlaybackState::Paused) {
        wasPaused_ = true;
    }
}

void VideoRenderThread::handleResumeFromPause() {
    if (wasPaused_) {
        wasPaused_ = false;
    }
}

void VideoRenderThread::cacheCurrentFrame(AVFrame* frame) {
#ifdef HAS_FFMPEG
    std::lock_guard<std::mutex> lock(frameMutex_);
    if (currentFrame_) {
        av_frame_unref(currentFrame_);
        av_frame_free(&currentFrame_);
    }
    currentFrame_ = av_frame_alloc();
    if (currentFrame_) {
        av_frame_ref(currentFrame_, frame);
    }
#endif
}

bool VideoRenderThread::isAllEofReceived() const {
    // 检查所有存在的流是否都收到 EOF
    // 如果有视频流，必须收到视频 EOF
    if (ctx_.hasVideoStream()) {
        if (!videoEofReceived_ || !videoEofReceived_->load()) {
            return false;  // 视频流存在但未收到 EOF
        }
    }
    
    // 如果有音频流，必须收到音频 EOF
    if (ctx_.hasAudioStream()) {
        if (!audioEofReceived_ || !audioEofReceived_->load()) {
            return false;  // 音频流存在但未收到 EOF
        }
    }
    
    // 至少需要有一个流存在
    bool hasAnyStream = ctx_.hasVideoStream() || ctx_.hasAudioStream();
    if (!hasAnyStream) {
        return false;  // 没有流，不应该结束
    }
    
    // 所有存在的流都收到 EOF
    return true;
}

void VideoRenderThread::updateDriftHistory(double driftMs) {
    std::lock_guard<std::mutex> lock(driftMutex_);
    
    driftHistory_.push_back(driftMs);
    totalDrift_ += driftMs;
    
    while (driftHistory_.size() > DRIFT_HISTORY_SIZE) {
        double removed = driftHistory_.front();
        driftHistory_.pop_front();
        totalDrift_ -= removed;
    }
}

} // namespace AdvancedPlayer
