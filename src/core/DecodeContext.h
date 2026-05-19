/**
 * @file DecodeContext.h
 * @brief 解码上下文结构体（统一配置，替代冗长参数列表）
 * 
 * 设计哲学：
 * - 结构体替代参数列表：11个参数 → 1个结构体
 * - 非拥有指针：所有指针都是借用，生命周期由 PlaybackController 管理
 * - 类型安全：编译时验证上下文有效性
 */

#ifndef DECODECONTEXT_H
#define DECODECONTEXT_H

#include <atomic>

#ifdef HAS_FFMPEG
extern "C" {
#include <libavutil/rational.h>
}
#endif

struct AVFormatContext;

namespace AdvancedPlayer {

class LockFreePacketQueue;
class LockFreeVideoFrameQueue;
class LockFreeAudioFrameQueue;
class IDecoder;
class AudioResampler;
enum class PlaybackState;

// ==================== 视频解码上下文 ====================

/**
 * @brief 视频解码上下文（VideoDecodeThread 启动时传入）
 * 
 * 所有指针都是「非拥有」的，生命周期由 PlaybackController 管理
 */
struct VideoDecodeContext {
    // ===== 数据源与输出 =====
    LockFreePacketQueue* packetQueue{nullptr};      // 输入：视频包队列
    LockFreeVideoFrameQueue* frameQueue{nullptr};   // 输出：视频帧队列
    
    // ===== 解码器 =====
    IDecoder* decoder{nullptr};                     // 视频解码器
    
    // ===== 流信息 =====
    int streamIndex{-1};                            // 视频流索引
    AVRational timeBase{0, 1};                      // 时间基准
    unsigned int nbStreams{0};                      // 流总数（用于验证）
    
    // ===== 状态引用 =====
    std::atomic<PlaybackState>* playbackState{nullptr};
    
    /**
     * @brief 验证上下文是否有效
     */
    bool isValid() const {
        return packetQueue != nullptr
            && frameQueue != nullptr
            && decoder != nullptr
            && streamIndex >= 0
            && playbackState != nullptr
            && timeBase.den != 0;
    }
    
    /**
     * @brief 从 AVFormatContext 提取流信息
     */
    void extractStreamInfo(AVFormatContext* formatCtx, int videoStreamIdx);
};

// ==================== 音频解码上下文 ====================

/**
 * @brief 音频解码上下文（AudioDecodeThread 启动时传入）
 * 
 * 所有指针都是「非拥有」的，生命周期由 PlaybackController 管理
 */
struct AudioDecodeContext {
    // ===== 数据源与输出 =====
    LockFreePacketQueue* packetQueue{nullptr};      // 输入：音频包队列
    LockFreeAudioFrameQueue* frameQueue{nullptr};   // 输出：音频帧队列
    
    // ===== 解码器与重采样 =====
    IDecoder* decoder{nullptr};                     // 音频解码器
    AudioResampler* resampler{nullptr};             // 音频重采样器
    
    // ===== 流信息 =====
    int streamIndex{-1};                            // 音频流索引
    AVRational timeBase{0, 1};                      // 时间基准
    unsigned int nbStreams{0};                      // 流总数（用于验证）
    
    // ===== 状态引用 =====
    std::atomic<PlaybackState>* playbackState{nullptr};
    
    /**
     * @brief 验证上下文是否有效
     */
    bool isValid() const {
        return packetQueue != nullptr
            && frameQueue != nullptr
            && decoder != nullptr
            && resampler != nullptr
            && streamIndex >= 0
            && playbackState != nullptr
            && timeBase.den != 0;
    }
    
    /**
     * @brief 从 AVFormatContext 提取流信息
     */
    void extractStreamInfo(AVFormatContext* formatCtx, int audioStreamIdx);
};

} // namespace AdvancedPlayer

#endif // DECODECONTEXT_H
