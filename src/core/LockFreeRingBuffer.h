/**
 * @file LockFreeRingBuffer.h
 * @brief 通用 SPSC 无锁环形缓冲区模板
 * 
 * 设计哲学：
 * - 单一职责：只负责无锁的环形缓冲区逻辑
 * - 零锁竞争：生产者和消费者完全无阻塞
 * - 内存高效：固定容量，无动态分配
 * - 缓存友好：head/tail 分离到不同缓存行
 * 
 * 使用约束：
 * - 只支持 SPSC（单生产者-单消费者）
 * - 容量必须是 2 的幂（构造时自动调整）
 * - 所有操作都是非阻塞的
 */

#ifndef LOCKFREERINGBUFFER_H
#define LOCKFREERINGBUFFER_H

#include <atomic>
#include <memory>
#include <cstddef>
#include <type_traits>

namespace AdvancedPlayer {

// ==================== 缓存行大小常量 ====================
// 使用固定值 64 字节，避免 GCC 的 -Winterference-size 警告
// 64 字节是大多数现代 x86/ARM CPU 的缓存行大小
inline constexpr size_t CACHE_LINE_SIZE = 64;

/**
 * @brief SPSC 无锁环形缓冲区模板
 * 
 * @tparam T 存储的元素类型（必须是可默认构造和可移动的）
 * 
 * 实现原理：
 * - 使用两个原子索引：head（消费者写）和 tail（生产者写）
 * - 通过 memory_order 保证可见性
 * - 容量减一用于区分满和空状态
 */
template<typename T>
class LockFreeRingBuffer {
    static_assert(std::is_default_constructible_v<T>, 
                  "T must be default constructible");
    static_assert(std::is_move_constructible_v<T> || std::is_copy_constructible_v<T>,
                  "T must be move or copy constructible");

public:
    /**
     * @brief 构造函数
     * @param capacity 请求的容量（将自动调整为 2 的幂）
     */
    explicit LockFreeRingBuffer(size_t capacity)
        : capacity_(roundUpToPowerOf2(capacity))
        , mask_(capacity_ - 1)
        , buffer_(std::make_unique<T[]>(capacity_))
        , head_{0}
        , tail_{0}
        , abort_{false} {
    }
    
    ~LockFreeRingBuffer() = default;
    
    // ==================== 核心操作 ====================
    
    /**
     * @brief 尝试推入元素（非阻塞）
     * @param item 要推入的元素（将被移动）
     * @return true 成功，false 队列满或已中止
     */
    bool tryPush(T&& item) {
        if (abort_.load(std::memory_order_acquire)) {
            return false;
        }
        
        const size_t currentTail = tail_.load(std::memory_order_relaxed);
        const size_t nextTail = (currentTail + 1) & mask_;
        
        // 检查是否满（nextTail == head 表示满）
        if (nextTail == head_.load(std::memory_order_acquire)) {
            return false;
        }
        
        // 写入数据
        buffer_[currentTail] = std::move(item);
        
        // 更新 tail（release 保证数据写入对消费者可见）
        tail_.store(nextTail, std::memory_order_release);
        
        return true;
    }
    
    /**
     * @brief 尝试推入元素（const 引用版本）
     */
    bool tryPush(const T& item) {
        if (abort_.load(std::memory_order_acquire)) {
            return false;
        }
        
        const size_t currentTail = tail_.load(std::memory_order_relaxed);
        const size_t nextTail = (currentTail + 1) & mask_;
        
        if (nextTail == head_.load(std::memory_order_acquire)) {
            return false;
        }
        
        buffer_[currentTail] = item;
        tail_.store(nextTail, std::memory_order_release);
        
        return true;
    }
    
    /**
     * @brief 尝试取出元素（非阻塞）
     * @param item 输出参数，接收元素
     * @return true 成功，false 队列空或已中止
     */
    bool tryPop(T& item) {
        if (abort_.load(std::memory_order_acquire)) {
            return false;
        }
        
        const size_t currentHead = head_.load(std::memory_order_relaxed);
        
        // 检查是否空（head == tail 表示空）
        if (currentHead == tail_.load(std::memory_order_acquire)) {
            return false;
        }
        
        // 读取数据
        item = std::move(buffer_[currentHead]);
        
        // 清空槽位（对于持有资源的类型很重要）
        buffer_[currentHead] = T{};
        
        // 更新 head
        const size_t nextHead = (currentHead + 1) & mask_;
        head_.store(nextHead, std::memory_order_release);
        
        return true;
    }
    
    /**
     * @brief 查看队首元素但不取出
     * @param item 输出参数
     * @return true 成功，false 队列空
     */
    bool peek(T& item) const {
        const size_t currentHead = head_.load(std::memory_order_relaxed);
        
        if (currentHead == tail_.load(std::memory_order_acquire)) {
            return false;
        }
        
        item = buffer_[currentHead];
        return true;
    }
    
    // ==================== 状态查询 ====================
    
    /**
     * @brief 获取当前大小（近似值）
     */
    size_t size() const {
        const size_t head = head_.load(std::memory_order_acquire);
        const size_t tail = tail_.load(std::memory_order_acquire);
        
        if (tail >= head) {
            return tail - head;
        } else {
            return capacity_ - head + tail;
        }
    }
    
    /**
     * @brief 获取容量
     */
    size_t capacity() const { return capacity_; }
    
    /**
     * @brief 获取可用空间
     */
    size_t available() const { return capacity_ - size() - 1; }
    
    /**
     * @brief 检查是否为空
     */
    bool empty() const {
        return head_.load(std::memory_order_acquire) == 
               tail_.load(std::memory_order_acquire);
    }
    
    /**
     * @brief 检查是否已满
     */
    bool full() const {
        const size_t nextTail = (tail_.load(std::memory_order_acquire) + 1) & mask_;
        return nextTail == head_.load(std::memory_order_acquire);
    }
    
    // ==================== 控制操作 ====================
    
    /**
     * @brief 清空缓冲区
     * @warning 只能在确保无并发访问时调用，或由消费者调用
     */
    void clear() {
        // 快速重置索引
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_release);
    }
    
    /**
     * @brief 清空缓冲区并对每个元素调用清理函数
     * @tparam CleanupFunc 清理函数类型：void(T&)
     */
    template<typename CleanupFunc>
    void clearWithCleanup(CleanupFunc&& cleanup) {
        size_t currentHead = head_.load(std::memory_order_relaxed);
        const size_t currentTail = tail_.load(std::memory_order_relaxed);
        
        while (currentHead != currentTail) {
            cleanup(buffer_[currentHead]);
            buffer_[currentHead] = T{};
            currentHead = (currentHead + 1) & mask_;
        }
        
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_release);
    }
    
    /**
     * @brief 设置中止标志
     */
    void abort() { abort_.store(true, std::memory_order_release); }
    
    /**
     * @brief 清除中止标志
     */
    void resume() { abort_.store(false, std::memory_order_release); }
    
    /**
     * @brief 检查是否已中止
     */
    bool isAborted() const { return abort_.load(std::memory_order_acquire); }
    
    // ==================== 遍历接口 ====================
    
    /**
     * @brief 遍历队列中的元素（只读）
     * @tparam Func 遍历函数类型：bool(const T&, size_t index)，返回 false 停止遍历
     * @note 只能由消费者线程调用，遍历期间生产者可能推入新元素
     */
    template<typename Func>
    void forEach(Func&& func) const {
        size_t currentHead = head_.load(std::memory_order_acquire);
        const size_t currentTail = tail_.load(std::memory_order_acquire);
        size_t index = 0;
        
        while (currentHead != currentTail) {
            if (!func(buffer_[currentHead], index)) {
                break;  // 回调返回 false，停止遍历
            }
            currentHead = (currentHead + 1) & mask_;
            ++index;
        }
    }
    
    /**
     * @brief 丢弃队首的 N 个元素
     * @param count 要丢弃的元素数量
     * @param cleanup 清理函数，对每个被丢弃的元素调用
     * @return 实际丢弃的元素数量
     * @note 只能由消费者线程调用
     */
    template<typename CleanupFunc>
    size_t discardN(size_t count, CleanupFunc&& cleanup) {
        size_t currentHead = head_.load(std::memory_order_relaxed);
        const size_t currentTail = tail_.load(std::memory_order_acquire);
        size_t discarded = 0;
        
        while (discarded < count && currentHead != currentTail) {
            cleanup(buffer_[currentHead]);
            buffer_[currentHead] = T{};
            currentHead = (currentHead + 1) & mask_;
            ++discarded;
        }
        
        // 更新 head
        head_.store(currentHead, std::memory_order_release);
        
        return discarded;
    }
    
    /**
     * @brief 获取当前队列的头尾索引快照
     * @return pair<head, tail>
     */
    std::pair<size_t, size_t> getIndices() const {
        return {head_.load(std::memory_order_acquire), 
                tail_.load(std::memory_order_acquire)};
    }
    
    /**
     * @brief 直接访问指定索引的元素（只读）
     * @note 调用者必须确保索引在 [head, tail) 范围内
     */
    const T& at(size_t index) const {
        return buffer_[index & mask_];
    }

private:
    /**
     * @brief 向上取整到 2 的幂
     */
    static size_t roundUpToPowerOf2(size_t n) {
        if (n == 0) return 1;
        if ((n & (n - 1)) == 0) return n;  // 已经是 2 的幂
        #if __cpp_lib_bitops >= 201907L // C++20 及以上，直接用标准库内置函数（最优）
            // std::bit_ceil 专门用于计算 >=n 的最小 2 的幂，效率最高
            return std::bit_ceil(n);
        #else // C++17 兼容实现
            // std::bit_width：返回表示 n 所需的二进制位数（C++17）
            // 例如：n=5(101) → bit_width=3 → 1<<2 = 8；n=8(1000) → bit_width=4 → 1<<3=8
            const size_t bits = std::bit_width(n - 1);
            return static_cast<size_t>(1) << bits;
        #endif
    }
    
    // ==================== 数据成员 ====================
    const size_t capacity_;
    const size_t mask_;
    std::unique_ptr<T[]> buffer_;
    
    // 分离到不同缓存行，避免伪共享
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> head_;
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> tail_;
    
    std::atomic<bool> abort_;
    
    // 禁用拷贝
    LockFreeRingBuffer(const LockFreeRingBuffer&) = delete;
    LockFreeRingBuffer& operator=(const LockFreeRingBuffer&) = delete;
};

} // namespace AdvancedPlayer

#endif // LOCKFREERINGBUFFER_H
