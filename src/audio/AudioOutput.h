/**
 * @file AudioOutput.h
 * @brief 音频输出类（含音频时钟）
 * 
 * 设计哲学（简化版音视频同步）：
 * - **单一时钟源**：AudioOutput 是音频播放时间的唯一真相来源
 * - **单一变速模式**：所有倍速均由 SoundTouch TSOLA 算法处理（变速不变调）
 * - **被动驱动**：视频帧根据音频时钟决定渲染/跳帧/延迟
 * - **无音频回退**：当无音频流时，使用系统时钟 + 帧率控制
 * 
 * 时钟计算公式：
 *   playbackTime = lastPushPts - bufferDelay
 *   
 *   其中：
 *   - lastPushPts：SoundTouch 输出采样数 × speed / sampleRate + 会话起始PTS
 *   - bufferDelay：SDL 流队列延迟 × speed + 系统音频设备延迟
 * 
 * 使用方式：
 *   1. 初始化时 initialize()（内部同时创建 SoundTouch 实例）
 *   2. 推送音频时 play() 会自动经过 SoundTouch 并更新时钟
 *   3. 视频线程调用 getPlaybackTime() 获取当前播放位置
 *   4. 倍速控制通过 setPlaybackSpeed() 设置 SoundTouch tempo
 */

#ifndef AUDIOOUTPUT_H
#define AUDIOOUTPUT_H

#include <QObject>
#include <atomic>
#include <memory>
#include <chrono>
#include <mutex>

// SDL3 前向声明
struct SDL_AudioStream;

// SoundTouch 完整定义（unique_ptr 需要完整类型）
#ifdef HAS_SOUNDTOUCH
#include "SoundTouchProcessor.h"
#endif

namespace AdvancedPlayer {

// ==================== 音频时钟状态枚举 ====================

/**
 * @brief 音频播放状态
 */
enum class AudioPlaybackState {
    Stopped,    // 停止状态
    Playing,    // 播放中
    Paused      // 暂停
};

/**
 * @brief 音频输出类（含音频时钟）
 * 
 * 职责：
 * 1. SDL3 音频设备管理
 * 2. 音频数据推送和播放
 * 3. 音量/静音/倍速控制
 * 4. **音频时钟管理**（供视频同步使用）
 */
class AudioOutput : public QObject {
    Q_OBJECT
    
public:
    explicit AudioOutput(QObject* parent = nullptr);
    ~AudioOutput() override;
    
    // ==================== 设备管理 ====================
    
    /**
     * @brief 初始化音频输出
     * @param sampleRate 采样率
     * @param channels 声道数
     * @param format 音频格式
     * @return 成功时返回 true
     */
    bool initialize(int sampleRate, int channels, int format);
    
    /**
     * @brief 重新初始化音频输出（资源复用优化）
     */
    bool reinitialize(int sampleRate, int channels, int format);
    
    /**
     * @brief 检查是否已初始化
     */
    bool isInitialized() const;
    
    // ==================== 格式信息 ====================
    
    int getInputSampleRate() const { return inputSampleRate_; }
    int getInputChannels() const { return inputChannels_; }
    int getInputFormat() const { return inputFormat_; }
    int getSampleRate() const { return sampleRate_; }
    int getChannels() const { return channels_; }
    int getBytesPerSample() const { return 2; }  // S16 = 2 bytes
    
    // ==================== 音频播放 ====================
    
    /**
     * @brief 播放音频数据（含时钟更新）
     * @param data       音频数据
     * @param size       数据大小（字节）
     * @param inputPts   当前帧的媒体 PTS（秒），用于非 SoundTouch 模式的时钟更新
     * @return           数据已成功推入 SDL 缓冲区时返回 true；
     *                   SoundTouch 仍在内部缓冲（暂无输出）时返回 false
     *
     * @note 时钟更新（notifyAudioPush）在内部自动完成，调用方无需再单独调用。
     *       SoundTouch 模式下使用输出采样计数精确推算 PTS，避免输入/输出延迟导致
     *       时钟高估，进而引发视频帧超前于音频的问题。
     */
    bool play(const uint8_t* data, int size, double inputPts = 0.0);
    
    /**
     * @brief 播放处理后的音频数据（内部使用）
     * @param data 处理后的音频数据
     * @param size 数据大小
     * @return 是否成功推送到缓冲区
     */
    bool playProcessedData(const uint8_t* data, int size);
    
    /**
     * @brief 暂停音频
     */
    void pause();
    
    /**
     * @brief 恢复音频
     */
    void resume();
    
    /**
     * @brief 清空音频缓冲区
     */
    void clear();
    
    // ==================== 音量控制 ====================
    
    void setVolume(float volume);
    float getVolume() const { return volume_.load(); }
    void setMuted(bool muted);
    bool isMuted() const { return muted_.load(); }
    
    // ==================== 倍速控制 ====================
    
    /**
     * @brief 设置播放速度（倍速，始终通过 SoundTouch TSOLA 算法处理）
     * @param speed 速度倍数 (0.25 - 2.0)
     * @return 成功返回 true
     * 
     * @note 所有速度均使用 SoundTouch 变速不变调，同时影响音频播放和时钟推进速度
     */
    bool setPlaybackSpeed(double speed);
    double getPlaybackSpeed() const { return playbackSpeed_.load(); }
    
    // ==================== 音频时钟（核心接口）====================
    
    /**
     * @brief 通知音频数据已推送（更新时钟）
     * @param pts 推送的音频帧 PTS（秒）
     * @param dataSize 推送的数据大小（字节）
     * 
     * @note 每次调用 play() 后应调用此方法更新时钟
     */
    void notifyAudioPush(double pts);
    
    /**
     * @brief 获取当前音频播放位置（秒）
     * @return 实际播放位置
     * 
     * 计算公式：
     *   playbackTime = lastPushPts - bufferDelay
     * 
     * @note 这是视频同步的核心接口
     */
    double getPlaybackTime() const;
    
    /**
     * @brief 获取当前音频播放位置（毫秒）
     */
    int64_t getPlaybackTimeMs() const;
    
    /**
     * @brief 获取缓冲区延迟（秒）
     * @return SDL 缓冲区延迟 + 系统延迟
     */
    double getBufferDelay() const;
    
    /**
     * @brief 获取 SDL 缓冲区中排队的数据时长（秒）
     */
    double getQueuedTime() const;
    
    /**
     * @brief 获取最近推送的 PTS
     */
    double getLastPushPts() const { return lastPushPts_.load(); }
    
    /**
     * @brief 检查音频时钟是否有效（是否有音频数据推送）
     */
    bool hasValidClock() const;
    
    // ==================== 时钟状态控制 ====================
    
    /**
     * @brief 暂停时钟（暂停时调用）
     */
    void pauseClock();
    
    /**
     * @brief 恢复时钟（从暂停恢复时调用）
     */
    void resumeClock();
    
    /**
     * @brief 重置时钟（停止或切换文件时调用）
     */
    void resetClock();
    
    /**
     * @brief 获取播放状态
     */
    AudioPlaybackState getPlaybackState() const { return playbackState_.load(); }
    
    // ==================== 纯视频流支持（无音频时的回退时钟）====================
    
    /**
     * @brief 启动系统时钟（无音频流时使用）
     * @param startPts 起始 PTS（秒）
     */
    void startSystemClock(double startPts = 0.0);
    
    /**
     * @brief 获取系统时钟时间（无音频流时使用）
     * @return 当前时间（秒）
     */
    double getSystemClockTime() const;
    
    /**
     * @brief 检查是否正在使用系统时钟（无音频流模式）
     */
    bool isUsingSystemClock() const { return useSystemClock_.load(); }
    
    /**
     * @brief 设置使用系统时钟模式
     */
    void setUseSystemClock(bool use) { useSystemClock_.store(use); }
    
    // ==================== 监控与诊断 ====================
    
    /**
     * @brief 获取累计推送的音频帧数
     */
    uint64_t getTotalPushedFrames() const { return totalPushedFrames_.load(); }
    
    /**
     * @brief 获取上次推送距今的时间（毫秒）
     */
    int64_t getTimeSinceLastPush() const;
    
    // ==================== 兼容接口（保留旧接口）====================
    
    double getAudioClock() const { return getPlaybackTime(); }
    void setAudioClock(double clock);
    SDL_AudioStream* getAudioStream() const { return audioStream_; }

private:
    // ==================== SDL 音频设备 ====================
    SDL_AudioStream* audioStream_{nullptr};
    uint32_t audioDeviceId_{0};
    
    // 设备规格
    int sampleRate_{44100};
    int channels_{2};
    int inputSampleRate_{44100};
    int inputChannels_{2};
    int inputFormat_{0};
    
    // ==================== 音量控制 ====================
    std::atomic<float> volume_{1.0f};
    std::atomic<bool> muted_{false};
    std::atomic<float> lastSetGain_{-1.0f};  // 原子变量，避免多线程数据竞争
    
    // ==================== 倍速控制 ====================
    std::atomic<double> playbackSpeed_{1.0};
#ifdef HAS_SOUNDTOUCH
    std::unique_ptr<SoundTouchProcessor> soundTouchProcessor_;
#endif

    // ==================== SoundTouch 输出 PTS 追踪 ====================
    //
    // 问题根因：notifyAudioPush(inputPts) 使用的是送入 SoundTouch 的输入 PTS，
    // 而 SoundTouch 有 N 帧内部延迟，实际到达 SDL 的内容对应更早的媒体时间。
    // 这导致 lastPushPts 被高估，视频时钟以为音频已播到更前的位置，
    // 进而引发视频帧不等待音频、画面超前于声音的问题。
    //
    // 修正方案：通过累计 SoundTouch 输出采样数计算输出端 PTS，仅在数据
    // 真正到达 SDL 时才调用 notifyAudioPush，从而精确反映实际播放位置。
    int64_t soundTouchOutputSamples_{0};     // 累计输出采样数（单声道帧数）
    double  soundTouchInitialPts_{0.0};      // SoundTouch 会话的起始媒体 PTS
    bool    soundTouchPtsInitialized_{false}; // 是否已在本会话中初始化起始 PTS
    
    // ==================== 音频时钟状态 ====================
    
    /**
     * @brief 最近推送的音频帧 PTS（秒）
     */
    std::atomic<double> lastPushPts_{0.0};
    
    /**
     * @brief 播放状态
     */
    std::atomic<AudioPlaybackState> playbackState_{AudioPlaybackState::Stopped};
    
    /**
     * @brief 累计推送帧数
     */
    std::atomic<uint64_t> totalPushedFrames_{0};
    
    /**
     * @brief 上次推送时间（用于检测时钟有效性）
     */
    std::chrono::steady_clock::time_point lastPushTime_;
    std::atomic<bool> hasPushedData_{false};
    
    // ==================== 系统时钟（无音频流时使用）====================
    
    std::atomic<bool> useSystemClock_{false};
    std::chrono::steady_clock::time_point systemClockStartTime_;
    std::atomic<double> systemClockStartPts_{0.0};
    std::atomic<double> pausedPts_{0.0};  // 暂停时的 PTS 快照
    mutable std::mutex systemClockMutex_;
    
    // ==================== 常量 ====================
    
    /**
     * @brief 系统音频延迟估算值
     * 
     * SDL_GetAudioStreamQueued 仅计入流队列（stream input queue）中尚未处理的字节，
     * 不包含 SDL 内部已转换完毕但尚未被 DAC 消耗的设备缓冲区数据。
     * 
     * 典型硬件/驱动延迟：
     *   Windows WASAPI 共享模式: 20-30ms
     *   Windows WASAPI 独占模式: 3-10ms
     *   Linux ALSA              : 10-30ms
     *   macOS CoreAudio         : 5-15ms
     * 
     * 保守取 30ms（Windows 共享模式最常见），在 SoundTouch 2x 路径下
     * 经 bufferDelay = wallClockDelay × speed 放大为 60ms，
     * 相比之前 10ms 低估（→ 20ms 放大），额外补偿约 40ms 的时钟误差。
     */
    static constexpr double SYSTEM_LATENCY = 0.030;  // 30ms
    
    /**
     * @brief 音频数据有效性超时（秒）
     */
    static constexpr double AUDIO_TIMEOUT = 1.0;
    
    // ==================== 内部方法 ====================
    static void audioCallback(void* userdata, uint8_t* stream, int len);
};

} // namespace AdvancedPlayer

#endif // AUDIOOUTPUT_H
