/**
 * @file LockFreePacketQueue.h
 * @brief 无锁数据包队列（Lock-Free Packet Queue）
 * 
 * 设计定位：
 * - 用于解复用线程 → 解码线程之间的 AVPacket 传递
 * - SPSC 模型：单生产者（解复用线程）、单消费者（解码线程）
 * - 零锁竞争、高吞吐量
 * 
 */

#ifndef LOCKFREEPACKETQUEUE_H
#define LOCKFREEPACKETQUEUE_H

#include "LockFreeRingBuffer.h"
#include <QString>

struct AVPacket;

namespace AdvancedPlayer {

/**
 * @brief 无锁数据包队列
 * 
 * 使用环形缓冲区 + 原子操作实现的 SPSC 无锁队列
 * 
 * 所有权语义：
 * - push 成功：AVPacket 所有权转移到队列
 * - push 失败：调用者保留所有权，负责释放
 * - pop 成功：调用者获得 AVPacket 所有权，负责释放
 * 
 * EOF 标记：
 * - 使用 nullptr 作为 EOF 标记
 * - pushEofMarker() 推入 EOF
 * - isEofMarker(packet) 判断 EOF
 */
class LockFreePacketQueue {
public:
    /**
     * @brief 构造函数
     * @param capacity 容量（自动调整为 2 的幂）
     * @param name 队列名称（用于日志）
     */
    explicit LockFreePacketQueue(size_t capacity, const QString& name = QString());
    
    /**
     * @brief 析构函数
     * 自动释放队列中所有未消费的 AVPacket
     */
    ~LockFreePacketQueue();
    
    // ==================== 核心操作（非阻塞） ====================
    
    /**
     * @brief 推入数据包（非阻塞）
     * @param packet AVPacket 指针（所有权转移）
     * @return true 成功（所有权已转移），false 队列满或已中止（所有权未转移）
     * @note 不接受 nullptr，请使用 pushEofMarker() 推入 EOF
     */
    bool push(AVPacket* packet);
    
    /**
     * @brief 取出数据包（非阻塞）
     * @param packet 输出参数，接收数据包指针
     * @return true 成功，false 队列空或已中止
     * @note 当返回 true 且 *packet == nullptr 时，表示 EOF
     */
    bool pop(AVPacket** packet);
    
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
     * @brief 清空队列并释放所有 AVPacket
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
     * @brief 推入 EOF 标记（非阻塞）
     * @return true 成功推入，false 队列满或已中止
     * @note 由解复用线程在读取完所有数据后调用
     */
    bool pushEofMarker();
    
    /**
     * @brief 检查是否为 EOF 标记
     * @param packet 数据包指针
     * @return true 表示是 EOF 标记（nullptr）
     */
    static bool isEofMarker(AVPacket* packet) { return packet == nullptr; }

private:
    LockFreeRingBuffer<AVPacket*> buffer_;
    QString name_;
    
    LockFreePacketQueue(const LockFreePacketQueue&) = delete;
    LockFreePacketQueue& operator=(const LockFreePacketQueue&) = delete;
};

} // namespace AdvancedPlayer

#endif // LOCKFREEPACKETQUEUE_H
