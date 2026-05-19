/**
 * @file LockFreeAudioFrameQueue.h
 * @brief 无锁音频帧队列（Lock-Free Audio Frame Queue）
 * 
 * 设计定位：
 * - 用于解码线程 → 音频输出之间的音频数据传递
 * - SPSC 模型：单生产者（解码线程）、单消费者（音频回调）
 * - 零锁竞争、低延迟（音频回调对延迟极其敏感）
 * 
 * 与旧 AudioFrameQueue 的差异：
 * - 移除：resizeCapacity()、getInitialCapacity()（无锁队列容量固定）
 * - 移除：阻塞等待语义（所有操作非阻塞）
 * - 保留：EOF 标记、abort/resume 机制、pts 时间戳
 */

#ifndef LOCKFREEAUDIOFRAMEQUEUE_H
#define LOCKFREEAUDIOFRAMEQUEUE_H

#include "LockFreeRingBuffer.h"
#include <QString>
#include <cstdint>
#include <utility>  // for std::pair

namespace AdvancedPlayer {

/**
 * @brief 音频帧节点
 * 封装音频数据、大小和时间戳
 */
struct AudioFrameNode {
    uint8_t* data{nullptr};  // 音频数据指针（已重采样）
    int size{0};             // 数据大小（字节）
    double pts{0.0};         // 时间戳（秒）
    
    AudioFrameNode() = default;
    AudioFrameNode(uint8_t* d, int s, double p) : data(d), size(s), pts(p) {}
    
    // 移动语义
    AudioFrameNode(AudioFrameNode&& other) noexcept
        : data(other.data), size(other.size), pts(other.pts) {
        other.data = nullptr;
        other.size = 0;
        other.pts = 0.0;
    }
    
    AudioFrameNode& operator=(AudioFrameNode&& other) noexcept {
        if (this != &other) {
            data = other.data;
            size = other.size;
            pts = other.pts;
            other.data = nullptr;
            other.size = 0;
            other.pts = 0.0;
        }
        return *this;
    }
    
    // 拷贝语义（浅拷贝指针）
    AudioFrameNode(const AudioFrameNode&) = default;
    AudioFrameNode& operator=(const AudioFrameNode&) = default;
};

/**
 * @brief 无锁音频帧队列
 * 
 * 使用环形缓冲区 + 原子操作实现的 SPSC 无锁队列
 * 
 * 所有权语义：
 * - push 成功：音频数据所有权转移到队列
 * - push 失败：调用者保留所有权，负责释放
 * - pop 成功：调用者获得音频数据所有权，负责释放
 * 
 * EOF 标记：
 * - 使用 nullptr 作为 EOF 标记
 * - pushEofMarker() 推入 EOF（size=0, pts=-1.0）
 * - isEofMarker(data) 判断 EOF
 */
class LockFreeAudioFrameQueue {
public:
    /**
     * @brief 构造函数
     * @param capacity 容量（自动调整为 2 的幂）
     * @param name 队列名称（用于日志）
     */
    explicit LockFreeAudioFrameQueue(size_t capacity, const QString& name = QString());
    
    /**
     * @brief 析构函数
     * 自动释放队列中所有未消费的音频数据
     */
    ~LockFreeAudioFrameQueue();
    
    // ==================== 核心操作（非阻塞） ====================
    
    /**
     * @brief 推入音频数据（非阻塞）
     * @param data 音频数据指针（所有权转移）
     * @param size 数据大小（字节）
     * @param pts 时间戳（秒）
     * @return true 成功（所有权已转移），false 队列满或已中止（所有权未转移）
     * @note 不接受 nullptr，请使用 pushEofMarker() 推入 EOF
     */
    bool push(uint8_t* data, int size, double pts);
    
    /**
     * @brief 取出音频数据（非阻塞）
     * @param data 输出参数，接收数据指针
     * @param size 输出参数，接收数据大小
     * @param pts 输出参数，接收时间戳
     * @return true 成功，false 队列空或已中止
     * @note 当返回 true 且 *data == nullptr 时，表示 EOF
     */
    bool pop(uint8_t** data, int* size, double* pts);
    
    /**
     * @brief 查看队首数据但不取出
     * @param data 输出参数
     * @param size 输出参数
     * @param pts 输出参数
     * @return true 成功，false 队列空
     */
    bool peek(uint8_t** data, int* size, double* pts) const;
    
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
     * @brief 清空队列并释放所有音频数据
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
     * @note 由解码线程在解码完所有音频后调用
     */
    bool pushEofMarker();
    
    /**
     * @brief 检查是否为 EOF 标记
     * @param data 数据指针
     * @return true 表示是 EOF 标记（nullptr）
     */
    static bool isEofMarker(uint8_t* data) { return data == nullptr; }
    
    // ==================== 辅助接口 ====================
    
    /**
     * @brief 检查目标 PTS 是否在队列范围内
     * @param targetPts 目标时间戳（秒）
     * @return true 如果目标 PTS 在队列的 [minPts, maxPts] 范围内
     * @note 只能由消费者线程调用
     */
    bool containsPts(double targetPts) const;
    
    /**
     * @brief 丢弃所有 PTS < targetPts 的音频数据
     * @param targetPts 目标时间戳（秒）
     * @return 丢弃的音频帧数量
     * @note 只能由消费者线程调用，会释放被丢弃数据的内存
     */
    int discardBefore(double targetPts);
    
    /**
     * @brief 获取队列中的 PTS 范围
     * @return pair<minPts, maxPts>，如果队列为空返回 {0.0, 0.0}
     * @note 只能由消费者线程调用
     */
    std::pair<double, double> getPtsRange() const;

private:
    LockFreeRingBuffer<AudioFrameNode> buffer_;
    QString name_;
    
    // 禁用拷贝
    LockFreeAudioFrameQueue(const LockFreeAudioFrameQueue&) = delete;
    LockFreeAudioFrameQueue& operator=(const LockFreeAudioFrameQueue&) = delete;
};

} // namespace AdvancedPlayer

#endif // LOCKFREEAUDIOFRAMEQUEUE_H
