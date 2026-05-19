/**
 * @file SoundTouchProcessor.h
 * @brief SoundTouch 音频变速不变调处理器
 * 
 * 功能：
 * - 使用 SoundTouch 2.3.3 实现音频变速不变调
 * - 支持分块处理（1024 采样）
 * - 支持渐变变速（100ms 逐步调整）
 * - 保证视频同步
 */

#ifndef SOUNDTOUCHPROCESSOR_H
#define SOUNDTOUCHPROCESSOR_H

#include <cstdint>
#include <atomic>
#include <mutex>
#include <chrono>

#ifdef HAS_SOUNDTOUCH
#include "SoundTouch/SoundTouchDLL.h"
#endif

namespace AdvancedPlayer {

/**
 * @brief SoundTouch 音频处理器
 * 
 * 特性：
 * - 原生支持 16bit 有符号 PCM
 * - 采样率/声道数必须和输入 PCM 严格一致
 * - 分块处理（1024 采样），边处理边播放
 * - 渐变变速（100ms 逐步调整）
 */
class SoundTouchProcessor {
public:
    SoundTouchProcessor();
    ~SoundTouchProcessor();
    
    // 禁用拷贝和移动
    SoundTouchProcessor(const SoundTouchProcessor&) = delete;
    SoundTouchProcessor& operator=(const SoundTouchProcessor&) = delete;
    SoundTouchProcessor(SoundTouchProcessor&&) = delete;
    SoundTouchProcessor& operator=(SoundTouchProcessor&&) = delete;
    
    /**
     * @brief 初始化 SoundTouch 处理器
     * @param sampleRate 采样率（必须与输入 PCM 一致）
     * @param channels 声道数（必须与输入 PCM 一致）
     * @return 成功时返回 true
     */
    bool initialize(int sampleRate, int channels);
    
    /**
     * @brief 检查是否已初始化
     */
    bool isInitialized() const { return initialized_.load(); }
    
    /**
     * @brief 处理音频数据（分块处理）
     * @param inputData 输入 PCM 数据（16bit 有符号）
     * @param inputSamples 输入采样数（单声道采样数，立体声需要除以2）
     * @param outputBuffer 输出缓冲区
     * @param outputBufferSize 输出缓冲区大小（字节）
     * @return 实际输出的采样数（单声道采样数）
     */
    int processSamples(const int16_t* inputData, int inputSamples, 
                      int16_t* outputBuffer, int outputBufferSize);
    
    /**
     * @brief 设置播放速度（渐变）
     * @param speed 播放速度（1.0 = 正常，2.0 = 2倍速）
     * @param transitionMs 渐变时间（毫秒），默认 100ms
     */
    void setTempo(double speed, int transitionMs = 100);
    
    /**
     * @brief 获取当前播放速度
     */
    double getTempo() const { return currentTempo_.load(); }
    
    /**
     * @brief 清空内部缓冲区
     */
    void clear();
    
    /**
     * @brief 刷新剩余数据（处理完所有输入后调用）
     * @param outputBuffer 输出缓冲区
     * @param outputBufferSize 输出缓冲区大小（字节）
     * @return 实际输出的采样数
     */
    int flush(int16_t* outputBuffer, int outputBufferSize);
    
    /**
     * @brief 获取可用的输出采样数
     */
    int numSamples() const;
    
    /**
     * @brief 检查是否为空
     */
    bool isEmpty() const;

private:
    /**
     * @brief 更新渐变速度（在 processSamples 中调用）
     */
    void updateTempoTransition();
    
    /**
     * @brief 将输入数据分块处理
     * @param inputData 输入数据
     * @param inputSamples 输入采样数
     * @param outputBuffer 输出缓冲区
     * @param outputBufferSize 输出缓冲区大小
     * @return 实际输出的采样数
     */
    int processChunks(const int16_t* inputData, int inputSamples,
                     int16_t* outputBuffer, int outputBufferSize);

private:
#ifdef HAS_SOUNDTOUCH
    HANDLE soundTouchHandle_{nullptr};
#endif
    
    std::atomic<bool> initialized_{false};
    int sampleRate_{0};
    int channels_{0};
    
    // 渐变变速相关
    std::atomic<double> targetTempo_{1.0};
    std::atomic<double> currentTempo_{1.0};
    std::chrono::steady_clock::time_point transitionStartTime_;
    int transitionDurationMs_{100};
    bool isTransitioning_{false};
    mutable std::mutex transitionMutex_;
    
    // 分块处理相关
    static constexpr int CHUNK_SIZE = 1024;  // 每次处理 1024 采样
    std::vector<int16_t> inputBuffer_;      // 输入缓冲区
    int inputBufferOffset_{0};               // 输入缓冲区偏移
};

} // namespace AdvancedPlayer

#endif // SOUNDTOUCHPROCESSOR_H
