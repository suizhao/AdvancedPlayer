/**
 * @file AudioRenderThread.cpp
 * @brief 音频播放线程实现
 */

#include "AudioRenderThread.h"
#include "RenderContext.h"
#include "PlaybackController.h"  // PlaybackState
#include "LockFreeAudioFrameQueue.h"
#include "src/audio/AudioOutput.h"
#include <QDebug>
#include <QMetaObject>
#include <chrono>

#ifdef HAS_FFMPEG
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/mem.h>
}
#endif

namespace AdvancedPlayer {

// ==================== 构造与析构 ====================

AudioRenderThread::AudioRenderThread(QObject* parent)
    : QObject(parent) {
    qDebug() << "[AudioRenderThread] Audio render thread object created";
}

AudioRenderThread::~AudioRenderThread() {
    stop();
    qDebug() << "[AudioRenderThread] Audio render thread object destroyed";
}

// ==================== 线程控制 ====================

bool AudioRenderThread::start(const RenderContext& ctx,
                              std::atomic<bool>* videoEof, std::atomic<bool>* audioEof) {
    if (audioThread_) {
        qWarning() << "[AudioRenderThread::start] Audio render thread is already running";
        return false;
    }
    
    if (!ctx.hasAudioStream()) {
        qWarning() << "[AudioRenderThread::start] No audio stream";
        return false;
    }
    
    if (!ctx.audioOutput) {
        qCritical() << "[AudioRenderThread::start] AudioOutput is invalid";
        return false;
    }
    
    ctx_ = ctx;
    videoEofReceived_ = videoEof;
    audioEofReceived_ = audioEof;
    
    // 重置状态
    audioFrameCounter_ = 0;
    playbackFinishedEmitted_.store(false);
    
    // 恢复队列
    if (ctx_.audioFrameQueue) {
        ctx_.audioFrameQueue->resume();
    }
    
    // 启动音频线程
    audioThread_ = std::make_unique<std::jthread>([this](std::stop_token stoken) {
        audioLoop(stoken);
    });
    
    qInfo() << "[AudioRenderThread::start] Audio render thread started";
    return true;
}

void AudioRenderThread::requestStop() {
    if (!audioThread_) {
        return;
    }
    qInfo() << "[AudioRenderThread::requestStop] Request stop audio render thread";

    // 先中止队列，让线程能够快速退出等待循环
    if (ctx_.audioFrameQueue) {
        ctx_.audioFrameQueue->abort();
    }
    
    // 请求停止
    audioThread_->request_stop();
}

void AudioRenderThread::joinAndReset() {
    if (!audioThread_) {
        return;
    }

    qInfo() << "[AudioRenderThread::joinAndReset] Joining audio render thread";
    audioThread_.reset();
    
    qInfo() << "[AudioRenderThread::joinAndReset] Audio render thread stopped";
}

void AudioRenderThread::stop() {
    requestStop();
    joinAndReset();
}

bool AudioRenderThread::isRunning() const {
    return audioThread_ && audioThread_->joinable();
}

// ==================== 音频播放循环 ====================

void AudioRenderThread::audioLoop(std::stop_token stoken) {
    bool eofHandled = false;
    
#ifdef HAS_FFMPEG
    while (!stoken.stop_requested()) {
        if (eofHandled) {
            // EOF 后不立即自然退出，统一由外部 stop() 回收线程
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        // 检查播放状态
        PlaybackState currentState = ctx_.playbackState->load();
        if (currentState != PlaybackState::Playing) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        
        if (stoken.stop_requested()) break;
        
        // 检查 EOF（支持纯音频流）
        // 检查所有存在的流是否都收到 EOF
        bool allEof = true;
        
        // 如果有视频流，必须收到视频 EOF
        if (ctx_.hasVideoStream()) {
            if (!videoEofReceived_ || !videoEofReceived_->load()) {
                allEof = false;
            }
        }
        
        // 如果有音频流，必须收到音频 EOF
        if (ctx_.hasAudioStream()) {
            if (!audioEofReceived_ || !audioEofReceived_->load()) {
                allEof = false;
            }
        }
        
        // 至少需要有一个流存在
        bool hasAnyStream = ctx_.hasVideoStream() || ctx_.hasAudioStream();
        
        if (allEof && hasAnyStream) {
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
        
        // 处理音频帧（批量）
        // 如果有音频流且未收到 EOF，继续处理
        if (ctx_.hasAudioStream() && (!audioEofReceived_ || !audioEofReceived_->load())) {
            int processed = processAllPendingAudio(stoken);
            
            // 队列暂时为空，避免忙等
            if (processed == 0) {
                bool audioEmpty = !ctx_.audioFrameQueue || ctx_.audioFrameQueue->empty() || 
                                 (audioEofReceived_ && audioEofReceived_->load());
                if (audioEmpty) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
        } else {
            // 没有音频流或已经 EOF，轻微休眠
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
#else
    Q_UNUSED(stoken);
#endif
    
    // 退出日志由 stop()（主线程）统一输出，避免 worker 退出路径调用 Qt 日志
}

// ==================== 音频处理 ====================

int AudioRenderThread::processAllPendingAudio(std::stop_token& stoken) {
#ifdef HAS_FFMPEG
    if (!ctx_.audioFrameQueue) return 0;
    
    constexpr int MAX_BATCH = 10;
    int processedCount = 0;
    
    for (int i = 0; i < MAX_BATCH && (!audioEofReceived_ || !audioEofReceived_->load()); ++i) {
        if (stoken.stop_requested()) break;
        
        uint8_t* data = nullptr;
        int size = 0;
        double pts = 0.0;
        
        if (!ctx_.audioFrameQueue->pop(&data, &size, &pts)) {
            break;
        }
        
        // EOF 检测
        if (LockFreeAudioFrameQueue::isEofMarker(data)) {
            if (audioEofReceived_) {
                audioEofReceived_->store(true);
            }
            continue;
        }
        
        if (stoken.stop_requested()) {
            av_free(data);
            break;
        }
        
        // 推送到 AudioOutput
        bool pushed = false;
        if (ctx_.audioOutput) {
            pushed = ctx_.audioOutput->play(data, size, pts);
            if (pushed) {
                ++processedCount;
            }
        }
        
        av_free(data);
        
        // 纯音频流：更新播放位置
        if (!ctx_.hasVideoStream() && ctx_.audioOutput) {
            double clockTime = ctx_.audioOutput->getPlaybackTime();
            emit positionChanged(static_cast<int64_t>(clockTime * 1000));
        }
        
        ++audioFrameCounter_;
    }
    
    return processedCount;
#else
    return 0;
#endif
}

} // namespace AdvancedPlayer
