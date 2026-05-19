/**
 * @file AudioRenderThread.h
 * @brief 音频播放线程
 * 
 * 职责：
 * - 从音频队列取帧
 * - 推送到 AudioOutput 播放
 */

#ifndef AUDIORENDERTHREAD_H
#define AUDIORENDERTHREAD_H

#include "RenderContext.h"
#include <QObject>
#include <atomic>
#include <memory>
#include <thread>

namespace AdvancedPlayer {

/**
 * @brief 音频播放线程
 */
class AudioRenderThread : public QObject {
    Q_OBJECT
    
public:
    explicit AudioRenderThread(QObject* parent = nullptr);
    ~AudioRenderThread() override;
    
    // ==================== 线程控制 ====================
    
    bool start(const RenderContext& ctx,
               std::atomic<bool>* videoEof, std::atomic<bool>* audioEof);
    void requestStop();
    void joinAndReset();
    void stop();
    bool isRunning() const;
    
signals:
    void positionChanged(int64_t positionMs);
    void playbackFinished();
    void error(const QString& message);

private:
    // ==================== 渲染循环 ====================
    
    void audioLoop(std::stop_token stoken);
    
    // ==================== 帧处理 ====================
    
    int processAllPendingAudio(std::stop_token& stoken);
    
    // ==================== 成员变量 ====================
    
    std::unique_ptr<std::jthread> audioThread_{nullptr};
    RenderContext ctx_{};
    
    // EOF 状态（通过指针访问，由 PlaybackController 管理）
    std::atomic<bool>* videoEofReceived_{nullptr};
    std::atomic<bool>* audioEofReceived_{nullptr};
    
    // 帧计数
    uint64_t audioFrameCounter_{0};
    
    // 播放结束信号只发送一次
    std::atomic<bool> playbackFinishedEmitted_{false};
    
    // 禁用拷贝
    AudioRenderThread(const AudioRenderThread&) = delete;
    AudioRenderThread& operator=(const AudioRenderThread&) = delete;
};

} // namespace AdvancedPlayer

#endif // AUDIORENDERTHREAD_H
