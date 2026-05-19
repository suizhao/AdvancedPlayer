/**
 * @file LockFreeVideoFrameQueue.h
 * @brief 无锁视频帧队列（Lock-Free Video Frame Queue）
 * 
 * 设计定位：
 * - 用于解码线程 → 渲染线程之间的 AVFrame 传递
 * - SPSC 模型：单生产者（解码线程）、单消费者（渲染线程）
 * - 零锁竞争、适合高帧率视频（60fps+）
 * 
 * 与旧 VideoFrameQueue 的差异：
 * - 移除：resizeCapacity()、getInitialCapacity()（无锁队列容量固定）
 * - 移除：阻塞等待语义（所有操作非阻塞）
 * - 保留：EOF 标记、abort/resume 机制、pts 时间戳
 */

#ifndef LOCKFREEVIDEOFRAMEQUEUE_H
#define LOCKFREEVIDEOFRAMEQUEUE_H

#include "LockFreeRingBuffer.h"
#include <QString>
#include <utility>  // for std::pair

// FFmpeg 前向声明
struct AVFrame;

namespace AdvancedPlayer {

/**
 * @brief 视频帧节点
 * 封装 AVFrame 指针和时间戳
 */
struct VideoFrameNode {
    AVFrame* frame{nullptr};  // 视频帧指针
    double pts{0.0};          // 时间戳（秒）
    
    VideoFrameNode() = default;
    VideoFrameNode(AVFrame* f, double p) : frame(f), pts(p) {}
    
    // 移动语义
    VideoFrameNode(VideoFrameNode&& other) noexcept
        : frame(other.frame), pts(other.pts) {
        other.frame = nullptr;
        other.pts = 0.0;
    }
    
    VideoFrameNode& operator=(VideoFrameNode&& other) noexcept {
        if (this != &other) {
            frame = other.frame;
            pts = other.pts;
            other.frame = nullptr;
            other.pts = 0.0;
        }
        return *this;
    }
    
    // 拷贝语义（浅拷贝指针）
    VideoFrameNode(const VideoFrameNode&) = default;
    VideoFrameNode& operator=(const VideoFrameNode&) = default;
};

/**
 * @brief 无锁视频帧队列
 * 
 * 使用环形缓冲区 + 原子操作实现的 SPSC 无锁队列
 * 
 * 所有权语义：
 * - push 成功：AVFrame 所有权转移到队列
 * - push 失败：调用者保留所有权，负责释放
 * - pop 成功：调用者获得 AVFrame 所有权，负责释放
 * 
 * EOF 标记：
 * - 使用 nullptr 作为 EOF 标记
 * - pushEofMarker() 推入 EOF（pts = -1.0）
 * - isEofMarker(frame) 判断 EOF
 */
class LockFreeVideoFrameQueue {
public:
    /**
     * @brief 构造函数
     * @param capacity 容量（自动调整为 2 的幂）
     * @param name 队列名称（用于日志）
     */
    explicit LockFreeVideoFrameQueue(size_t capacity, const QString& name = QString());
    
    /**
     * @brief 析构函数
     * 自动释放队列中所有未消费的 AVFrame
     */
    ~LockFreeVideoFrameQueue();
    
    // ==================== 核心操作（非阻塞） ====================
    
    /**
     * @brief 推入视频帧（非阻塞）
     * @param frame AVFrame 指针（所有权转移）
     * @param pts 时间戳（秒）
     * @return true 成功（所有权已转移），false 队列满或已中止（所有权未转移）
     * @note 不接受 nullptr，请使用 pushEofMarker() 推入 EOF
     */
    bool push(AVFrame* frame, double pts);
    
    /**
     * @brief 取出视频帧（非阻塞）
     * @param frame 输出参数，接收帧指针
     * @param pts 输出参数，接收时间戳
     * @return true 成功，false 队列空或已中止
     * @note 当返回 true 且 *frame == nullptr 时，表示 EOF
     */
    bool pop(AVFrame** frame, double* pts);
    
    /**
     * @brief 查看队首帧但不取出
     * @param frame 输出参数
     * @param pts 输出参数
     * @return true 成功，false 队列空
     */
    bool peek(AVFrame** frame, double* pts) const;
    
    // ==================== 状态查询 ====================
    
    /**
     * @brief 获取当前大小（近似值）
     */
    size_t size() const;
    
    /**
     * @brief 获取容量
     */
    size_t capacity() const { return buffer_.capacity(); }
    
    /**
     * @brief 检查是否为空
     */
    bool empty() const { return buffer_.empty(); }
    
    /**
     * @brief 检查是否已满
     */
    bool full() const { return buffer_.full(); }
    
    // ==================== 控制操作 ====================
    
    /**
     * @brief 清空队列并释放所有 AVFrame
     * @warning 应在确保无并发访问时调用
     */
    void clear();
    
    /**
     * @brief 中止队列（设置中止标志）
     */
    void abort() { buffer_.abort(); }
    
    /**
     * @brief 恢复队列（清除中止标志）
     */
    void resume() { buffer_.resume(); }
    
    /**
     * @brief 检查是否已中止
     */
    bool isAborted() const { return buffer_.isAborted(); }
    
    // ==================== EOF 标记 ====================
    
    /**
     * @brief 推入 EOF 标记
     * @note 由解码线程在解码完所有帧后调用
     */
    bool pushEofMarker();
    
    /**
     * @brief 检查是否为 EOF 标记
     * @param frame 帧指针
     * @return true 表示是 EOF 标记（nullptr）
     */
    static bool isEofMarker(AVFrame* frame) { return frame == nullptr; }
    
    // ==================== 辅助接口 ====================
    
    /**
     * @brief 检查目标 PTS 是否在队列范围内
     * @param targetPts 目标时间戳（秒）
     * @return true 如果目标 PTS 在队列的 [minPts, maxPts] 范围内
     * @note 只能由消费者线程调用
     */
    bool containsPts(double targetPts) const;
    
    /**
     * @brief 丢弃所有 PTS < targetPts 的帧
     * @param targetPts 目标时间戳（秒）
     * @return 丢弃的帧数量
     * @note 只能由消费者线程调用，会释放被丢弃帧的内存
     */
    int discardBefore(double targetPts);
    
    /**
     * @brief 获取队列中的 PTS 范围
     * @return pair<minPts, maxPts>，如果队列为空返回 {0.0, 0.0}
     * @note 只能由消费者线程调用
     */
    std::pair<double, double> getPtsRange() const;

private:
    LockFreeRingBuffer<VideoFrameNode> buffer_;
    QString name_;
    
    // 禁用拷贝
    LockFreeVideoFrameQueue(const LockFreeVideoFrameQueue&) = delete;
    LockFreeVideoFrameQueue& operator=(const LockFreeVideoFrameQueue&) = delete;
};

} // namespace AdvancedPlayer

#endif // LOCKFREEVIDEOFRAMEQUEUE_H
