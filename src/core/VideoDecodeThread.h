/**
 * @file VideoDecodeThread.h
 * @brief 独立视频解码线程
 * 
 * 设计哲学：
 * - 单一职责：只负责视频解码，不处理音频
 * - 数据单向流动：VideoPacketQueue → 解码 → VideoFrameQueue
 * - 与 AudioDecodeThread 完全独立，充分利用多核 CPU
 * 
 * 架构位置：
 *          ┌─────────────────┐
 *          │   DemuxThread    │ → 解复用
 *          └────────┬────────┘
 *                   │
 *          ┌────────┴────────┐
 *          │                 │
 *          ▼                 ▼
 * [VideoDecodeThread] [AudioDecodeThread]  ← 并行解码
 *          │                 │
 *          ▼                 ▼
 *      VideoFrameQueue   AudioFrameQueue
 *          │                 │
 *          └────────┬────────┘
 *                   │
 *                   ▼
 *              RenderThread
 */

#ifndef VIDEODECODETHREAD_H
#define VIDEODECODETHREAD_H

#include "DecodeContext.h"
#include <QObject>
#include <QString>
#include <atomic>
#include <memory>
#include <thread>
#include <stop_token>

struct AVFrame;

namespace AdvancedPlayer {

/**
 * @brief 独立视频解码线程
 * 
 * 职责：
 * 1. 从 VideoPacketQueue 取出视频包
 * 2. 调用解码器解码
 * 3. 将解码后的帧推入 VideoFrameQueue
 * 4. 处理 EOF 空包并传播 EOF 空帧
 * 
 * 不负责：
 * - 音频解码（由 AudioDecodeThread 负责）
 * - 格式转换（由 RenderThread 负责）
 * - 音视频同步（由 RenderThread 负责）
 */
class VideoDecodeThread : public QObject {
    Q_OBJECT
    
public:
    explicit VideoDecodeThread(QObject* parent = nullptr);
    ~VideoDecodeThread() override;
    
    // ==================== 线程控制 ====================
    
    /**
     * @brief 启动视频解码线程
     * @param ctx 视频解码上下文（必须在整个解码期间保持有效）
     * @return true 成功启动，false 上下文无效或已在运行
     */
    bool start(const VideoDecodeContext& ctx);
    
    /**
     * @brief 停止视频解码线程
     * @note 会等待线程完全退出
     */
    void requestStop();
    void joinAndReset();
    void stop();
    
    /**
     * @brief 检查是否正在运行
     */
    bool isRunning() const;
    
    /**
     * @brief 刷新解码器
     */
    void flush();
    
signals:
    /**
     * @brief 错误信号
     */
    void errorOccurred(const QString& message);
    
private:
    // ==================== 解码循环 ====================
    
    /**
     * @brief 主解码循环
     * @param stoken 停止令牌
     */
    void decodeLoop(std::stop_token stoken);
    
    /**
     * @brief 解码单个视频包
     * @param stoken 停止令牌
     * @return true 表示收到 EOF，false 表示正常解码
     */
    bool decodePacket(std::stop_token stoken);
    
    /**
     * @brief 处理解码后的视频帧
     * @param frame 解码后的帧（所有权转移给队列）
     */
    void processFrame(AVFrame* frame);
    
    /**
     * @brief 将帧推入队列（带重试）
     * @param frame 帧指针
     * @param pts 时间戳（秒）
     * @return true 成功，false 失败
     */
    bool pushFrameWithRetry(AVFrame* frame, double pts);
    
    // ==================== 成员变量 ====================
    
    // 线程
    std::unique_ptr<std::jthread> thread_{nullptr};
    
    // 上下文（解码期间保持有效）
    VideoDecodeContext ctx_{};
    
    // 禁用拷贝
    VideoDecodeThread(const VideoDecodeThread&) = delete;
    VideoDecodeThread& operator=(const VideoDecodeThread&) = delete;
};

} // namespace AdvancedPlayer

#endif // VIDEODECODETHREAD_H
