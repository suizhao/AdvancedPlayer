/**
 * @file DemuxContext.h
 * @brief 解复用上下文结构体
 * 
 * 设计哲学：
 * - 结构体替代参数列表：6个参数 → 1个结构体
 * - 非拥有指针：所有指针都是借用，生命周期由 PlaybackController 管理
 * - 可配置策略：支持网络流特定配置
 * - 类型安全：编译时验证上下文有效性
 */

#ifndef DEMUXCONTEXT_H
#define DEMUXCONTEXT_H

#include <atomic>

struct AVFormatContext;

namespace AdvancedPlayer {

class LockFreePacketQueue;
enum class PlaybackState;

// ==================== 解复用策略配置 ====================

/**
 * @brief 解复用策略配置
 * 
 * 提供灵活的性能调优参数，支持本地文件和网络流的差异化配置
 */
struct DemuxStrategy {
    // ===== 背压控制 =====
    int maxPushRetries{500};              // 队列满时最大重试次数
    int basePushRetryIntervalMs{10};           // 基础重试间隔（毫秒），HDR视频考虑设为50
    int maxRetryIntervalMs{200};            // 最大重试间隔（毫秒）
    
    // ===== 网络流配置 =====
    bool isNetworkStream{false};          // 是否为网络流
    int networkErrorRetryMs{100};         // 网络错误重试等待时间（毫秒）
    int networkTimeoutMs{10000};          // 网络超时时间（毫秒）
    int maxConsecutiveErrors{10};         // 最大连续错误次数（超过则停止）
    
    // ===== 暂停等待配置 =====
    int pauseCheckIntervalMs{10};         // 暂停时检查间隔（毫秒）
    
    /**
     * @brief 获取本地文件默认配置
     */
    static DemuxStrategy forLocalFile() {
        return DemuxStrategy{};
    }
    
    /**
     * @brief 获取网络流默认配置
     */
    static DemuxStrategy forNetworkStream() {
        DemuxStrategy strategy{};
        strategy.isNetworkStream = true;
        strategy.maxPushRetries = 1000;       // 网络流需要更大的重试次数
        strategy.networkTimeoutMs = 15000;    // 更长的超时
        return strategy; // NRVO(命名返回值优化)，返回局部变量无额外拷贝
    }
};

// ==================== 解复用上下文 ====================

/**
 * @brief 解复用上下文（DemuxThread 启动时传入）
 * 
 * 所有指针都是「非拥有」的，生命周期由 PlaybackController 管理
 * 
 * 使用示例：
 * @code
 * DemuxContext ctx;
 * ctx.formatCtx = formatCtx_;
 * ctx.videoStreamIndex = videoStreamIndex_;
 * ctx.audioStreamIndex = audioStreamIndex_;
 * ctx.videoPacketQueue = videoPacketQueue_.get();
 * ctx.audioPacketQueue = audioPacketQueue_.get();
 * ctx.playbackState = &state_;
 * ctx.strategy = DemuxStrategy::forLocalFile();
 * 
 * if (ctx.isValid()) {
 *     demuxThread_->start(ctx);
 * }
 * @endcode
 */
struct DemuxContext {
    // ===== FFmpeg 资源（非拥有） =====
    AVFormatContext* formatCtx{nullptr};          // FFmpeg 格式上下文
    
    // ===== 流信息 =====
    int videoStreamIndex{-1};                     // 视频流索引
    int audioStreamIndex{-1};                     // 音频流索引
    
    // ===== 输出队列（非拥有） =====
    LockFreePacketQueue* videoPacketQueue{nullptr};   // 视频包队列
    LockFreePacketQueue* audioPacketQueue{nullptr};   // 音频包队列
    
    // ===== 状态引用 =====
    std::atomic<PlaybackState>* playbackState{nullptr};
    
    // ===== 策略配置 =====
    DemuxStrategy strategy{};
    
    /**
     * @brief 验证上下文是否有效
     * @return true 如果所有必要字段都已设置
     */
    bool isValid() const {
        // formatCtx 必须有效
        if (!formatCtx) return false;
        
        // 至少要有一个有效的流
        if (videoStreamIndex < 0 && audioStreamIndex < 0) return false;
        
        // 有视频流时，视频队列必须有效
        if (videoStreamIndex >= 0 && !videoPacketQueue) return false;
        
        // 有音频流时，音频队列必须有效
        if (audioStreamIndex >= 0 && !audioPacketQueue) return false;
        
        // 播放状态必须有效
        if (!playbackState) return false;
        
        return true;
    }
    
    /**
     * @brief 检查是否有视频流
     */
    bool hasVideo() const {
        return videoStreamIndex >= 0 && videoPacketQueue;
    }
    
    /**
     * @brief 检查是否有音频流
     */
    bool hasAudio() const {
        return audioStreamIndex >= 0 && audioPacketQueue;
    }
    
    /**
     * @brief 检查是否为纯音频
     */
    bool isAudioOnly() const {
        return hasAudio() && !hasVideo();
    }
    
    /**
     * @brief 检查是否为纯视频
     */
    bool isVideoOnly() const {
        return hasVideo() && !hasAudio();
    }
};

} // namespace AdvancedPlayer

#endif // DEMUXCONTEXT_H
