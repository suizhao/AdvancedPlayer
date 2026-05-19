/**
 * @file RenderContext.h
 * @brief 渲染上下文和相关类型定义
 */

#ifndef RENDERCONTEXT_H
#define RENDERCONTEXT_H

#include <atomic>
#include <mutex>

// 前向声明
struct AVFrame;

namespace AdvancedPlayer {

// 前向声明
class LockFreeVideoFrameQueue;
class LockFreeAudioFrameQueue;
class AudioOutput;

// 前向声明播放状态枚举（完整定义在 PlaybackController.h）
enum class PlaybackState;

// ==================== 同步配置 ====================

/**
 * @brief 视频同步配置参数
 */
struct VideoSyncConfig {
    // 核心阈值（毫秒）
    double syncWindowMs = 50.0;           // 同步窗口 ±50ms
    double microDelayThresholdMs = 120.0; // 微延迟阈值
    double delayThresholdMs = 200.0;      // 延迟阈值
    double softSkipThresholdMs = 150.0;   // 软跳帧阈值
    double hardSkipThresholdMs = 250.0;   // 硬跳帧阈值
    
    // 延迟限制
    int maxDelayMs = 40;                  // 单次最大延迟时间
    int microDelayMs = 3;                  // 微延迟时间
    
    // 跳帧限制
    int maxConsecutiveSkips = 3;          // 最大连续跳帧数
    int softSkipRatio = 5;                // 软跳帧比例（每N帧跳1帧）
};

// ==================== 渲染上下文 ====================

/**
 * @brief 渲染上下文（简化版，不含同步组件）
 */
struct RenderContext {
    // 数据源（队列）
    LockFreeVideoFrameQueue* videoFrameQueue{nullptr};
    LockFreeAudioFrameQueue* audioFrameQueue{nullptr};
    
    // 输出设备
    AudioOutput* audioOutput{nullptr};
    
    // 状态引用
    std::atomic<PlaybackState>* playbackState{nullptr};
    
    // 流信息
    int videoStreamIndex{-1};
    int audioStreamIndex{-1};
    
    // 帧率（用于纯视频流的帧率控制）
    double frameRate{30.0};
    
    // 资源保护锁
    std::mutex* resourceMutex{nullptr};

    // 是否向上层(UI)转发视频帧
    bool enableVideoFrameForwarding{true};
    
    bool isValid() const {
        bool hasVideo = (videoStreamIndex >= 0) && videoFrameQueue;
        bool hasAudio = (audioStreamIndex >= 0) && audioFrameQueue;
        if (!playbackState) return false;
        return hasVideo || hasAudio;
    }
    
    bool hasVideoStream() const {
        return videoStreamIndex >= 0 && videoFrameQueue;
    }
    
    bool hasAudioStream() const {
        return audioStreamIndex >= 0 && audioFrameQueue;
    }
};

} // namespace AdvancedPlayer

#endif // RENDERCONTEXT_H
