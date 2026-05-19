/**
 * @file AudioDecodeThread.h
 * @brief 独立音频解码线程
 * 
 * 设计哲学：
 * - 单一职责：只负责音频解码 + 重采样，不处理视频
 * - 数据单向流动：AudioPacketQueue → 解码 → 重采样 → AudioFrameQueue
 * - 与 VideoDecodeThread 完全独立，充分利用多核 CPU
 * 
 * 架构位置：
 *   ┌─────────────────┐
 *   │   DemuxThread    │ → 解复用
 *   └────────┬────────┘
 *            │
 *   ┌────────┴────────┐
 *   │                 │
 *   ▼                 ▼
 * [VideoDecodeThread] [AudioDecodeThread]  ← 并行解码
 *   │                 │
 *   ▼                 ▼
 * VideoFrameQueue   AudioFrameQueue
 *   │                 │
 *   └────────┬────────┘
 *            │
 *            ▼
 *      RenderThread
 */

#ifndef AUDIODECODETHREAD_H
#define AUDIODECODETHREAD_H

#include "DecodeContext.h"
#include <QObject>
#include <QString>
#include <atomic>
#include <memory>
#include <thread>
#include <stop_token>

// 前向声明
struct AVFrame;

namespace AdvancedPlayer {

/**
 * @brief 独立音频解码线程
 * 
 * 职责：
 * 1. 从 AudioPacketQueue 取出音频包
 * 2. 调用解码器解码
 * 3. 重采样为目标格式
 * 4. 将重采样后的数据推入 AudioFrameQueue
 * 5. 处理 EOF 空包并传播 EOF 空帧
 * 
 * 不负责：
 * - 视频解码（由 VideoDecodeThread 负责）
 * - 音视频同步（由 RenderThread 负责）
 */
class AudioDecodeThread : public QObject {
    Q_OBJECT
    
public:
    explicit AudioDecodeThread(QObject* parent = nullptr);
    ~AudioDecodeThread() override;
    
    // ==================== 线程控制 ====================
    
    /**
     * @brief 启动音频解码线程
     * @param ctx 音频解码上下文（必须在整个解码期间保持有效）
     * @return true 成功启动，false 上下文无效或已在运行
     */
    bool start(const AudioDecodeContext& ctx);
    
    /**
     * @brief 停止音频解码线程
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
     * @brief 解码单个音频包
     * @param stoken 停止令牌
     * @return true 表示收到 EOF，false 表示正常解码
     */
    bool decodePacket(std::stop_token stoken);
    
    /**
     * @brief 处理解码后的音频帧（重采样并推入队列）
     * @param frame 解码后的帧
     */
    void processFrame(AVFrame* frame);
    
    /**
     * @brief 将重采样后的数据推入队列（带重试）
     * @param data 音频数据
     * @param size 数据大小
     * @param pts 时间戳（秒）
     * @return true 成功，false 失败
     */
    bool pushDataWithRetry(uint8_t* data, int size, double pts);
    
    // ==================== 成员变量 ====================
    
    // 线程
    std::unique_ptr<std::jthread> thread_{nullptr};
    
    // 上下文（解码期间保持有效）
    AudioDecodeContext ctx_{};
    
    // 禁用拷贝
    AudioDecodeThread(const AudioDecodeThread&) = delete;
    AudioDecodeThread& operator=(const AudioDecodeThread&) = delete;
};

} // namespace AdvancedPlayer

#endif // AUDIODECODETHREAD_H
