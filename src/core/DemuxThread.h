/**
 * @file DemuxThread.h
 * @brief 解复用线程
 * 
 * 设计哲学：
 * - 单一职责：只负责从 AVFormatContext 读取数据包并分发到队列
 * - 数据单向流动：AVFormatContext → DemuxThread → PacketQueues
 * - 条件变量暂停：避免 CPU 空转
 * - 背压感知：队列快满时主动降速，避免阻塞
 * 
 * 架构位置：
 * 
 *   ┌─────────────────┐
 *   │ AVFormatContext │  ← 媒体源
 *   └────────┬────────┘
 *            │
 *            ▼
 *   ┌─────────────────┐
 *   │   DemuxThread   │  ← 解复用（本类）
 *   └────────┬────────┘
 *            │
 *      ┌─────┴─────┐
 *      │           │
 *      ▼           ▼
 * VideoPacketQ  AudioPacketQ
 *      │           │
 *      ▼           ▼
 * VideoDecodeThread AudioDecodeThread
 */

#ifndef DEMUXTHREAD_H
#define DEMUXTHREAD_H

#include "DemuxContext.h"
#include <QObject>
#include <QString>
#include <memory>
#include <atomic>
#include <thread>
#include <stop_token>
#include <mutex>
#include <condition_variable>

struct AVFormatContext;
struct AVPacket;

namespace AdvancedPlayer {

class LockFreePacketQueue;
enum class PlaybackState;

/**
 * @brief 解复用线程状态
 */
enum class DemuxState {
    Idle,           // 空闲（未启动）
    Running,        // 正在运行
    Paused,         // 已暂停（等待条件变量）
    Stopping,       // 正在停止
    Stopped         // 已停止
};

/**
 * @brief 获取状态名称（用于日志）
 */
inline const char* getDemuxStateName(DemuxState state) {
    switch (state) {
        case DemuxState::Idle:     return "Idle";
        case DemuxState::Running:  return "Running";
        case DemuxState::Paused:   return "Paused";
        case DemuxState::Stopping: return "Stopping";
        case DemuxState::Stopped:  return "Stopped";
        default:                   return "Unknown";
    }
}

/**
 * @brief 解复用线程
 * 
 * 职责：
 * 1. 从 AVFormatContext 持续读取数据包
 * 2. 根据流索引分发到 VideoPacketQueue 或 AudioPacketQueue
 * 3. 处理 EOF 并推入 EOF 标记
 * 4. 响应暂停/恢复命令
 * 5. 处理网络流错误和重连
 */
class DemuxThread : public QObject {
    Q_OBJECT
    
public:
    explicit DemuxThread(QObject* parent = nullptr);
    ~DemuxThread() override;
    
    DemuxThread(const DemuxThread&) = delete;
    DemuxThread& operator=(const DemuxThread&) = delete;
    
    /**
     * @brief 启动解复用线程
     * @param ctx 解复用上下文（必须在整个解复用期间保持有效）
     * @return true 成功启动，false 上下文无效或已在运行
     * 
     * @note 调用前确保 ctx 中所有指针指向的资源已准备就绪
     *       调用后，ctx 会被复制到内部，外部修改不影响运行中的线程
     */
    bool start(const DemuxContext& ctx);
    
    /**
     * @brief 停止解复用线程
     * @note 会中断队列并等待线程完全退出
     */
    void requestStop();
    void joinAndReset();
    void stop();
    
    /**
     * @brief 检查是否正在运行
     */
    bool isRunning() const;
    
    /**
     * @brief 获取当前解复用线程状态
     */
    DemuxState getState() const { return state_.load(); }



signals:
    /**
     * @brief 错误信号
     */
    void errorOccurred(const QString& message);
    
    /**
     * @brief EOF 到达信号
     */
    void eofReached();
    
    /**
     * @brief 网络流缓冲不足信号（用于 UI 显示缓冲状态）
     */
    void bufferingStarted();
    
    /**
     * @brief 网络流缓冲完成信号
     */
    void bufferingFinished();
    
    
private:
    // ==================== 解复用循环 ====================
    
    /**
     * @brief 主解复用循环
     * @param stoken 停止令牌
     */
    void demuxLoop(std::stop_token stoken);
    
    /**
     * @brief 等待可运行状态（处理暂停）
     * @param stoken 停止令牌
     * @return true 可以继续运行，false 需要退出
     */
    bool waitForRunnable(std::stop_token stoken);
    
    /**
     * @brief 读取一个数据包
     * @param packet 输出参数，读取到的数据包
     * @return 读取结果（0 成功，AVERROR_EOF 结束，其他为错误）
     */
    int readPacket(AVPacket* packet);
    
    /**
     * @brief 分发数据包到对应队列
     * @param packet 数据包（成功推入后所有权转移给队列）
     * @param stoken 停止令牌
     * @return true 成功推入或丢弃，false 需要退出
     */
    bool dispatchPacket(AVPacket* packet, std::stop_token stoken);
    
    /**
     * @brief 推入数据包到队列（带背压控制）
     * @param queue 目标队列
     * @param packet 数据包
     * @param stoken 停止令牌
     * @return true 成功，false 超时或停止
     */
    bool pushPacketWithBackpressure(LockFreePacketQueue* queue, 
                                     AVPacket* packet,
                                     std::stop_token stoken);
    
    /**
     * @brief 推入 EOF 标记到队列（带背压控制）
     * @param queue 目标队列
     * @param stoken 停止令牌
     * @return true 成功推入，false 超时或停止
     * 
     * @note EOF 标记比普通数据包有更高的优先级，使用更长的超时时间确保最终能推入
     */
    bool pushEofMarkerWithBackpressure(LockFreePacketQueue* queue,
                                        std::stop_token stoken);
    
    /**
     * @brief 处理 EOF（推入 EOF 标记）
     * @param stoken 停止令牌（用于背压控制时的中断检查）
     */
    void handleEof(std::stop_token stoken);
    
    /**
     * @brief 处理读取错误
     * @param errorCode FFmpeg 错误码
     * @return true 可重试，false 需要退出
     */
    bool handleReadError(int errorCode);
    
    // 解复用线程
    std::unique_ptr<std::jthread> thread_{nullptr};
    // 解复用线程状态
    std::atomic<DemuxState> state_{DemuxState::Idle};
    // 上下文（启动时复制）
    DemuxContext ctx_{};
    // 条件变量（用于暂停/恢复）
    mutable std::mutex mutex_;
    std::condition_variable cv_;
};

} // namespace AdvancedPlayer

#endif // DEMUXTHREAD_H
