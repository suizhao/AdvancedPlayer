/**
 * @file AudioOutput.cpp
 * @brief 音频输出实现（含音频时钟）
 */

#include "AudioOutput.h"
#include "SoundTouchProcessor.h"
#include <QDebug>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <thread>
#include <chrono>

#ifdef HAS_SDL3
#include <SDL3/SDL.h>
#endif

namespace AdvancedPlayer {

// ==================== 构造与析构 ====================

AudioOutput::AudioOutput(QObject* parent)
    : QObject(parent) {
    lastPushTime_ = std::chrono::steady_clock::now();
    systemClockStartTime_ = std::chrono::steady_clock::now();
    qInfo() << "[AudioOutput] Audio output created (with built-in clock)";
}

AudioOutput::~AudioOutput() {
#ifdef HAS_SDL3
    qDebug() << "[AudioOutput::~AudioOutput] Cleaning up audio device...";
    
    // 静音防止电流声
    // if (audioStream_ && audioDeviceId_ != 0) {
    //     SDL_SetAudioStreamGain(audioStream_, 0.0f);
    // }
    
    // 暂停设备
    if (audioDeviceId_ != 0) {
        SDL_PauseAudioDevice(audioDeviceId_);
        SDL_Delay(10);
    }
    
    // 清理流
    if (audioStream_) {
        SDL_UnbindAudioStream(audioStream_);
        SDL_ClearAudioStream(audioStream_);
        SDL_Delay(5);
        SDL_DestroyAudioStream(audioStream_);
        audioStream_ = nullptr;
    }
    
    // 关闭设备
    if (audioDeviceId_ != 0) {
        SDL_CloseAudioDevice(audioDeviceId_);
        audioDeviceId_ = 0;
    }
#endif
    qInfo() << "[AudioOutput::~AudioOutput] Audio output destroyed";
}

// ==================== 设备管理 ====================

bool AudioOutput::initialize(int sampleRate, int channels, int format) {
    qInfo() << "[AudioOutput::initialize] Initialize: sampleRate=" << sampleRate
            << ", channels=" << channels << ", format=" << format;
#ifdef HAS_SDL3
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        qCritical() << "[AudioOutput::initialize] SDL audio init failed:" << SDL_GetError();
        return false;
    }
    
    SDL_AudioSpec desired_spec, actual_spec;
    SDL_zero(desired_spec);
    SDL_zero(actual_spec);

    // 这里的 desired_spec 只用于「打开设备」，不再直接作为 stream 的输入规格
    desired_spec.freq = sampleRate;
    desired_spec.format = static_cast<SDL_AudioFormat>(format);
    desired_spec.channels = channels;
    
    audioDeviceId_ = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &desired_spec);
    if (audioDeviceId_ == 0) {
        qCritical() << "[AudioOutput::initialize] Failed to open audio device:" << SDL_GetError();
        return false;
    }
    
    if (!SDL_GetAudioDeviceFormat(audioDeviceId_, &actual_spec, nullptr)) {
        qWarning() << "[AudioOutput::initialize] Failed to get device format, using desired spec";
        actual_spec = desired_spec;
    }
    
    // 设备实际规格（输出到声卡）
    sampleRate_ = actual_spec.freq;
    channels_ = actual_spec.channels;
    
    // ==================== 关键修正点 ====================
    // 我们的 AudioResampler 已经把数据重采样到「设备实际规格 + S16」，
    // 因此 SDL_AudioStream 的输入规格必须与重采样输出完全一致，
    // 否则 SDL 会再做一次 1->2 声道转换，导致时长翻倍（听起来就是 0.5x 速度，只能调到 2x 才正常）。
    //
    // 约定：
    //  - AudioResampler 输出: dstRate = sampleRate_，dstChannels = channels_，dstFormat = AV_SAMPLE_FMT_S16
    //  - 这里 SDL_AudioStream 输入: freq = sampleRate_，channels = channels_，format = SDL_AUDIO_S16
    //
    // 这样整个链路只有一处「声道/采样率」转换，不会出现时间基被乘以 2 的问题。
    SDL_AudioSpec stream_input_spec;
    SDL_zero(stream_input_spec);
    stream_input_spec.freq = sampleRate_;                         // 与重采样输出采样率一致
    stream_input_spec.format = static_cast<SDL_AudioFormat>(format); // SDL_AUDIO_S16
    stream_input_spec.channels = static_cast<Uint8>(channels_);   // 与重采样输出声道数一致
    
    // 记录「我们喂给 SDL_AudioStream 的数据规格」，用于缓冲时间计算等
    inputSampleRate_ = stream_input_spec.freq;
    inputChannels_ = stream_input_spec.channels;
    inputFormat_ = stream_input_spec.format;
    
    // 注意：此处的 fromSpec = stream_input_spec（重采样输出），toSpec = actual_spec（设备）
    // 如果设备本身就是 S16 / 同采样率 / 同声道，则 SDL 不再做任何重采样，只负责缓冲。
    audioStream_ = SDL_CreateAudioStream(&stream_input_spec, &actual_spec);
    if (!audioStream_) {
        qCritical() << "[AudioOutput::initialize] Failed to create audio stream:" << SDL_GetError();
        SDL_CloseAudioDevice(audioDeviceId_);
        audioDeviceId_ = 0;
        return false;
    }
    
    if (!SDL_BindAudioStream(audioDeviceId_, audioStream_)) {
        qCritical() << "[AudioOutput::initialize] Failed to bind audio stream:" << SDL_GetError();
        SDL_CloseAudioDevice(audioDeviceId_);
        SDL_DestroyAudioStream(audioStream_);
        audioDeviceId_ = 0;
        audioStream_ = nullptr;
        return false;
    }
    
    if (!SDL_ResumeAudioDevice(audioDeviceId_)) {
        qCritical() << "[AudioOutput::initialize] Failed to resume audio device:" << SDL_GetError();
        SDL_CloseAudioDevice(audioDeviceId_);
        SDL_DestroyAudioStream(audioStream_);
        audioDeviceId_ = 0;
        audioStream_ = nullptr;
        return false;
    }
    
    // 初始化时钟状态
    resetClock();
    
    // 初始化增益
    float initialVolume = muted_.load() ? 0.0f : volume_.load();
    if (!SDL_SetAudioStreamGain(audioStream_, initialVolume)) {
        qWarning() << "[AudioOutput::initialize] Failed to set initial gain:" << SDL_GetError();
    }
    lastSetGain_.store(initialVolume);
    
#ifdef HAS_SOUNDTOUCH
    // SoundTouch 在此统一创建，所有速度均通过它处理
    if (!soundTouchProcessor_) {
        soundTouchProcessor_ = std::make_unique<SoundTouchProcessor>();
        if (!soundTouchProcessor_->initialize(inputSampleRate_, inputChannels_)) {
            qWarning() << "[AudioOutput::initialize] SoundTouch initialization failed";
            soundTouchProcessor_.reset();
        }
    }
    // 将当前速度同步到 SoundTouch（处理先于 SDL 初始化调用 setPlaybackSpeed 的情况）
    if (soundTouchProcessor_) {
        soundTouchProcessor_->setTempo(playbackSpeed_.load(), 0);
    }
#endif

    qInfo() << "[AudioOutput::initialize] SDL3 audio initialized";
    return true;
#else
    qWarning() << "[AudioOutput::initialize] SDL3 is not enabled";
    return false;
#endif
}

bool AudioOutput::reinitialize(int sampleRate, int channels, int format) {
    qInfo() << "[AudioOutput::reinitialize] Reinitializing audio";
    
#ifdef HAS_SDL3
    bool formatSame = (inputSampleRate_ == sampleRate && 
                       inputChannels_ == channels && 
                       inputFormat_ == format);
    
    if (formatSame && audioStream_ && audioDeviceId_ != 0) {
        // 快速路径：格式相同，只重置状态
        qInfo() << "[AudioOutput::reinitialize] Format unchanged, reusing current device";
        
        SDL_PauseAudioDevice(audioDeviceId_);
        SDL_ClearAudioStream(audioStream_);
        
        resetClock();
        
        float effectiveVolume = muted_.load() ? 0.0f : volume_.load();
        SDL_SetAudioStreamGain(audioStream_, effectiveVolume);
        lastSetGain_.store(effectiveVolume);
        
#ifdef HAS_SOUNDTOUCH
        // 清空 SoundTouch 内部缓冲区，避免旧文件残留数据污染新文件
        // 格式相同无需重建实例，只需清空缓冲并重置 PTS 追踪
        if (soundTouchProcessor_) {
            soundTouchProcessor_->clear();
        }
        // 重置输出 PTS 追踪，确保新文件从 PTS=0 开始正确计算音频时钟
        soundTouchOutputSamples_  = 0;
        soundTouchPtsInitialized_ = false;
#endif
        
        SDL_ResumeAudioDevice(audioDeviceId_);
        return true;
    }
    
    // 慢速路径：格式不同，重新初始化
    qInfo() << "[AudioOutput::reinitialize] Format changed, reinitializing";
    
    float savedVolume = volume_.load();
    bool savedMuted = muted_.load();
    double savedSpeed = playbackSpeed_.load();
    // 格式变化时保存 useSoundTouch 状态，initialize() 后决定是否需要 SoundTouch
    // 注意：SoundTouch 实例会被重置，因为新格式（sampleRate/channels）不同，
    //       需要用新参数重新初始化。play() 调用 setPlaybackSpeed() 时会自动重建。
    
    if (audioStream_) {
        SDL_SetAudioStreamGain(audioStream_, 0.0f);
        SDL_UnbindAudioStream(audioStream_);
        SDL_ClearAudioStream(audioStream_);
        SDL_DestroyAudioStream(audioStream_);
        audioStream_ = nullptr;
    }
    
    if (audioDeviceId_ != 0) {
        SDL_PauseAudioDevice(audioDeviceId_);
        SDL_CloseAudioDevice(audioDeviceId_);
        audioDeviceId_ = 0;
    }

#ifdef HAS_SOUNDTOUCH
    // 格式变化（sampleRate/channels 不同），SoundTouch 实例必须销毁并重建，
    // 否则 SoundTouch 会使用旧格式参数处理新格式数据，导致音频损坏。
    // 重建由 initialize() 内部自动完成（soundTouchProcessor_ 为 nullptr 时触发）。
    if (soundTouchProcessor_) {
        soundTouchProcessor_.reset();
    }
#endif
    
    volume_.store(savedVolume);
    muted_.store(savedMuted);
    playbackSpeed_.store(savedSpeed);
    
    return initialize(sampleRate, channels, format);
#else
    return initialize(sampleRate, channels, format);
#endif
}

bool AudioOutput::isInitialized() const {
#ifdef HAS_SDL3
    return audioStream_ != nullptr && audioDeviceId_ != 0;
#else
    return false;
#endif
}

// ==================== 音频播放 ====================

bool AudioOutput::play(const uint8_t* data, int size, double inputPts) {
#ifdef HAS_SDL3
    if (!audioStream_ || !data || size <= 0) {
        return false;
    }

#ifdef HAS_SOUNDTOUCH
    if (!soundTouchProcessor_ || !soundTouchProcessor_->isInitialized()) {
        // SoundTouch 未就绪（极少情况：initialize 失败）：直接推送并用输入 PTS 更新时钟
        bool pushed = playProcessedData(data, size);
        if (pushed) notifyAudioPush(inputPts);
        return pushed;
    }

    // ==================== SoundTouch 变速不变调路径（唯一路径）====================

    // 记录本次会话的起始 PTS（仅首帧初始化一次）
    if (!soundTouchPtsInitialized_) {
        soundTouchInitialPts_    = inputPts;
        soundTouchOutputSamples_ = 0;
        soundTouchPtsInitialized_ = true;
    }

    const int16_t* inputSamples = reinterpret_cast<const int16_t*>(data);
    int inputSampleCount = size / (sizeof(int16_t) * inputChannels_);

    // 预留足够输出空间（低倍速时输出采样数 > 输入采样数）
    std::vector<int16_t> soundTouchOutputBuffer;
    int maxOutputSamples = inputSampleCount * 4;   // 支持低至 0.25x
    soundTouchOutputBuffer.resize(maxOutputSamples * inputChannels_);

    int outputSampleCount = soundTouchProcessor_->processSamples(
        inputSamples, inputSampleCount,
        soundTouchOutputBuffer.data(),
        static_cast<int>(soundTouchOutputBuffer.size() * sizeof(int16_t))
    );

    if (outputSampleCount > 0) {
        int outputSize = outputSampleCount * inputChannels_ * sizeof(int16_t);
        const uint8_t* outputData =
            reinterpret_cast<const uint8_t*>(soundTouchOutputBuffer.data());

        bool pushed = playProcessedData(outputData, outputSize);
        if (pushed) {
            // ==================== 精确 PTS 推算 ====================
            // SoundTouch 变速原理：
            // - speed=2.0: 输入 1000 采样 → 输出约 500 采样
            //   墙钟时长 = 500/采样率 秒（播放速度 2x，时长减半）
            //   媒体时长 = 1000/采样率 秒（原始媒体内容）
            //   关系：媒体时长 = 输出采样数 * speed / 采样率
            // 
            // - speed=0.5: 输入 1000 采样 → 输出约 2000 采样
            //   墙钟时长 = 2000/采样率 秒（播放速度 0.5x，时长加倍）
            //   媒体时长 = 1000/采样率 秒（原始媒体内容）
            //   关系：媒体时长 = 输出采样数 * speed / 采样率
            //
            // 公式：媒体 PTS = 初始 PTS + 输出采样数 * speed / 采样率
            // 这表示「已播放的媒体时长」，需要乘以 speed 将墙钟时长转换为媒体时长
            soundTouchOutputSamples_ += outputSampleCount;
            double speed = playbackSpeed_.load();
            double outputMediaPts = soundTouchInitialPts_
                + static_cast<double>(soundTouchOutputSamples_)
                  * speed
                  / static_cast<double>(inputSampleRate_);
            notifyAudioPush(outputMediaPts);
        }
        return pushed;
    }

    // SoundTouch 仍在内部积累数据，本次无输出。
    // 返回 false：告知调用方「时钟未更新」，避免上层错误地推进 lastPushPts，
    // 否则会在 SDL 空队列时造成时钟大幅高估，导致视频帧不等音频而超前播放。
    return false;

#else
    // 无 SoundTouch：直接推送
    bool pushed = playProcessedData(data, size);
    if (pushed) notifyAudioPush(inputPts);
    return pushed;
#endif

#else
    return false;
#endif
}

bool AudioOutput::playProcessedData(const uint8_t* data, int size) {
#ifdef HAS_SDL3
    if (!audioStream_ || !data || size <= 0) {
        return false;
    }
    
    // 缓冲区管理（根据播放速度调整）
    // 注意：SDL 队列中的字节数是 SoundTouch 处理后的数据，已经反映了变速
    // 因此缓冲区大小应该基于输出数据量，而不是输入数据量
    double speed = playbackSpeed_.load();
    
    // 计算输出数据的字节率（考虑 SoundTouch 变速）
    // SoundTouch 输出采样数 = 输入采样数 / speed（对于 2x 速度）
    // 但实际推送的数据是输出数据，所以字节率 = 输入字节率 / speed
    int inputBytesPerSecond = inputSampleRate_ * inputChannels_ * sizeof(int16_t);
    int outputBytesPerSecond = static_cast<int>(inputBytesPerSecond / speed);
    
    // 根据播放速度调整最大缓冲时间（媒体时间）
    // 2x 速度：150ms 媒体时间 = 75ms 墙钟时间（更小缓冲，减少延迟）
    // 0.5x 速度：400ms 媒体时间 = 800ms 墙钟时间（更大缓冲，避免断流）
    double maxBufferTimeMs = speed > 1.0 ? 150.0 : 400.0;
    maxBufferTimeMs = std::clamp(maxBufferTimeMs, 100.0, 500.0);
    
    // 转换为输出数据的字节数（考虑实际推送的数据量）
    int maxBufferBytes = static_cast<int>(outputBytesPerSecond * maxBufferTimeMs / 1000.0);
    
    int queuedBytes = SDL_GetAudioStreamQueued(audioStream_);
    
    // 缓冲区过满：短暂等待而不是丢弃
    // 倍速播放时减少等待时间，避免卡顿
    int maxWaitMs = speed > 1.0 ? 20 : 40;  // 倍速时最多等 20ms
    if (queuedBytes > maxBufferBytes) {
        int waitMs = 0;
        int waitStep = speed > 1.0 ? 2 : 5;  // 倍速时更频繁检查
        while (queuedBytes > maxBufferBytes && waitMs < maxWaitMs) {
            std::this_thread::sleep_for(std::chrono::milliseconds(waitStep));
            waitMs += waitStep;
            queuedBytes = SDL_GetAudioStreamQueued(audioStream_);
        }
        
        // 仍然过满，丢弃这帧（避免缓冲区溢出导致卡顿）
        // 但在倍速时，如果只是轻微超出，允许推送（避免频繁丢弃导致断流）
        if (queuedBytes > maxBufferBytes) {
            if (speed > 1.0 && queuedBytes < maxBufferBytes * 1.2) {
                // 倍速时，允许轻微超出（20%），避免频繁丢弃
                qDebug() << "[AudioOutput::playProcessedData] Buffer slightly full but push allowed:"
                         << "queued=" << queuedBytes << "bytes, max=" << maxBufferBytes << "bytes"
                         << ", speed=" << speed << "x";
            } else {
                qDebug() << "[AudioOutput::playProcessedData] Buffer too full, dropping frame:"
                         << "queued=" << queuedBytes << "bytes, max=" << maxBufferBytes << "bytes"
                         << ", speed=" << speed << "x";
                return false;
            }
        }
    }
    
    // 推送数据
    if (!SDL_PutAudioStreamData(audioStream_, data, size)) {
        qWarning() << "[AudioOutput::playProcessedData] Failed to push audio data:" << SDL_GetError();
        return false;
    }
    
    // 更新增益（仅在变化时）
    // 使用原子操作读取 lastSetGain_，避免与 setVolume/setMuted 的竞态条件
    float currentVolume = muted_.load() ? 0.0f : volume_.load();
    float lastGain = lastSetGain_.load(std::memory_order_acquire);
    if (std::abs(currentVolume - lastGain) > 0.001f) {
        if (SDL_SetAudioStreamGain(audioStream_, currentVolume)) {
            lastSetGain_.store(currentVolume, std::memory_order_release);
        }
    }
    
    return true;
#else
    return false;
#endif
}

void AudioOutput::pause() {
#ifdef HAS_SDL3
    if (audioDeviceId_ != 0) {
        if (audioStream_) {
            SDL_SetAudioStreamGain(audioStream_, 0.0f);
            lastSetGain_.store(0.0f);
        }
        SDL_PauseAudioDevice(audioDeviceId_);
        SDL_Delay(10);
    }
#endif
    
    pauseClock();
    qDebug() << "[AudioOutput::pause] Audio paused";
}

void AudioOutput::resume() {
#ifdef HAS_SDL3
    if (audioDeviceId_ != 0) {
        SDL_ResumeAudioDevice(audioDeviceId_);
        
        if (audioStream_) {
            float effectiveVolume = muted_.load() ? 0.0f : volume_.load();
            SDL_SetAudioStreamGain(audioStream_, effectiveVolume);
            lastSetGain_.store(effectiveVolume);
        }
    }
#endif
    
    resumeClock();
    qDebug() << "[AudioOutput::resume] Audio resumed";
}

void AudioOutput::clear() {
#ifdef HAS_SDL3
    if (audioStream_ && audioDeviceId_ != 0) {
        SDL_SetAudioStreamGain(audioStream_, 0.0f);
        lastSetGain_.store(0.0f);
        
        SDL_PauseAudioDevice(audioDeviceId_);
        SDL_Delay(10);
        SDL_ClearAudioStream(audioStream_);
    }
#endif
    
#ifdef HAS_SOUNDTOUCH
    // 清理 SoundTouch 内部缓冲
    if (soundTouchProcessor_) {
        soundTouchProcessor_->clear();
    }
    // 重置输出 PTS 追踪：新文件/停止时必须从零开始计数，
    // 否则旧的 soundTouchOutputSamples_ 会在新文件中产生错误的初始时钟偏移。
    soundTouchOutputSamples_  = 0;
    soundTouchPtsInitialized_ = false;
#endif
    
    resetClock();
    qDebug() << "[AudioOutput::clear] Audio buffer cleared, clock reset";
}

// ==================== 音量控制 ====================

void AudioOutput::setVolume(float volume) {
    volume = std::clamp(volume, 0.0f, 1.0f);
    volume_ = volume;
    
#ifdef HAS_SDL3
    if (audioStream_) {
        float effectiveVolume = muted_.load() ? 0.0f : volume;
        // 使用原子操作读取 lastSetGain_，避免与 playProcessedData 的竞态条件
        float lastGain = lastSetGain_.load(std::memory_order_acquire);
        if (std::abs(effectiveVolume - lastGain) > 0.001f) {
            if (SDL_SetAudioStreamGain(audioStream_, effectiveVolume)) {
                lastSetGain_.store(effectiveVolume, std::memory_order_release);
            }
        }
    }
#endif
}

void AudioOutput::setMuted(bool muted) {
    muted_ = muted;
    
#ifdef HAS_SDL3
    if (audioStream_) {
        float effectiveVolume = muted ? 0.0f : volume_.load();
        // 使用原子操作读取 lastSetGain_，避免与 playProcessedData 的竞态条件
        float lastGain = lastSetGain_.load(std::memory_order_acquire);
        if (std::abs(effectiveVolume - lastGain) > 0.001f) {
            if (SDL_SetAudioStreamGain(audioStream_, effectiveVolume)) {
                lastSetGain_.store(effectiveVolume, std::memory_order_release);
            }
        }
    }
#endif
}

// ==================== 倍速控制 ====================

bool AudioOutput::setPlaybackSpeed(double speed) {
    speed = std::clamp(speed, 0.25, 2.0);
    
    double oldSpeed = playbackSpeed_.load();
    if (std::abs(speed - oldSpeed) < 0.001) {
        return true;
    }

#ifdef HAS_SOUNDTOUCH
    // ==================== 速度切换时重置 PTS 追踪 ====================
    // 原因：soundTouchOutputSamples_ / sampleRate 是已播放的媒体时长
    //       切换速度后，SoundTouch 的输出采样率会变化，必须重新计数
    //       否则新旧速度下的输出采样数单位不一致，导致 PTS 计算错误
    // 收敛：重置后首次输出的 PTS 以 inputPts 为基准（非精确的输出 PTS），
    //       偏差 ≤ SoundTouch 内部延迟（约 5 帧 × 23ms ≈ 115ms），
    //       时钟会在数帧内自然收敛，不会产生持续漂移
    soundTouchOutputSamples_  = 0;
    soundTouchPtsInitialized_ = false;

#ifdef HAS_SDL3
    // ==================== 切速时清空 SDL 流缓冲区 ====================
    // SDL 缓冲区中残留的旧速度数据会被 getBufferDelay() 用新速度换算，导致
    // bufferDelay 错误（旧 1x 数据被当作新 2x 数据处理，误差 = 旧缓冲 × Δspeed）。
    // 清空后有极短（< SoundTouch 延迟 ≈ 100ms）静音，换取时钟立即精确。
    if (audioStream_) {
        SDL_ClearAudioStream(audioStream_);
    }
#endif

    // 若 SoundTouch 尚未创建（initialize 未被调用或初始化失败），在此补建
    if (!soundTouchProcessor_) {
        soundTouchProcessor_ = std::make_unique<SoundTouchProcessor>();
        if (!soundTouchProcessor_->initialize(inputSampleRate_, inputChannels_)) {
            qWarning() << "[AudioOutput::setPlaybackSpeed] SoundTouch initialization failed";
            soundTouchProcessor_.reset();
            playbackSpeed_.store(speed);
            return false;
        }
    }

    soundTouchProcessor_->setTempo(speed, 100);  // 100ms 渐变，避免切速时的音频卡顿
    playbackSpeed_.store(speed);
    qInfo() << "[AudioOutput::setPlaybackSpeed] Playback speed:" << speed << "x";
    return true;

#else
    // 无 SoundTouch：仅记录速度（无音频变速支持）
    playbackSpeed_.store(speed);
    return false;
#endif
}

// ==================== 音频时钟 ====================

void AudioOutput::notifyAudioPush(double pts) {
    // 更新时钟状态
    lastPushPts_.store(pts, std::memory_order_release);
    lastPushTime_ = std::chrono::steady_clock::now();
    hasPushedData_.store(true, std::memory_order_release);
    totalPushedFrames_.fetch_add(1, std::memory_order_relaxed);
    
    // 如果当前是停止状态，切换到播放状态
    if (playbackState_.load() == AudioPlaybackState::Stopped) {
        playbackState_.store(AudioPlaybackState::Playing, std::memory_order_release);
    }
    
    // 调试日志（每100帧）
    uint64_t frameCount = totalPushedFrames_.load();
    if (frameCount % 100 == 0) {
        double clockTime = getPlaybackTime();
        double bufferDelay = getBufferDelay();
        
        qDebug() << "[AudioOutput::notifyAudioPush] Audio clock:"
                 << "PTS=" << QString::number(pts, 'f', 3) << "s"
                 << ", clock=" << QString::number(clockTime, 'f', 3) << "s"
                 << ", bufferDelay=" << QString::number(bufferDelay * 1000, 'f', 1) << "ms"
                 << ", frames=" << frameCount;
    }
}

double AudioOutput::getPlaybackTime() const {
    // 如果使用系统时钟（无音频流模式）
    if (useSystemClock_.load()) {
        return getSystemClockTime();
    }
    
    // 音频驱动模式
    if (!hasValidClock()) {
        return 0.0;
    }
    
    double lastPts = lastPushPts_.load(std::memory_order_acquire);
    double bufferDelay = getBufferDelay();
    double playbackTime = lastPts - bufferDelay;
    // qDebug() << "playbackTime = "<< playbackTime;
    
    return std::max(0.0, playbackTime);
}

int64_t AudioOutput::getPlaybackTimeMs() const {
    return static_cast<int64_t>(getPlaybackTime() * 1000.0);
}

double AudioOutput::getBufferDelay() const {
#ifdef HAS_SDL3
    if (!audioStream_) {
        return 0.0;
    }

    // ==================== 墙钟延迟 → 媒体延迟换算 ====================
    // getQueuedTime() 返回「墙钟时长」；而 lastPushPts 是「媒体时间」。
    // SoundTouch 变速：1s 媒体音频 → 1/speed 秒墙钟输出。
    //   mediaDelay = wallClockDelay × speed
    // 不换算时 0.5x 会低估 50% bufferDelay，导致视频延迟画面滞后。
    double wallClockDelay = getQueuedTime() + SYSTEM_LATENCY;
    double speed = playbackSpeed_.load();
    return wallClockDelay * speed;
#else
    return 0.0;
#endif
}

double AudioOutput::getQueuedTime() const {
#ifdef HAS_SDL3
    if (!audioStream_) {
        return 0.0;
    }
    
    int queuedBytes = SDL_GetAudioStreamQueued(audioStream_);
    
    if (queuedBytes > 0 && inputSampleRate_ > 0 && inputChannels_ > 0) {
        int bytesPerSecond = inputSampleRate_ * inputChannels_ * sizeof(int16_t);
        double queuedTime = static_cast<double>(queuedBytes) / bytesPerSecond;
        return std::min(queuedTime, 1.0);  // 最大 1 秒
    }
    
    return 0.0;
#else
    return 0.0;
#endif
}

bool AudioOutput::hasValidClock() const {
    if (!hasPushedData_.load(std::memory_order_acquire)) {
        return false;
    }
    
    // 检查是否超时
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration<double>(now - lastPushTime_).count();
    
    return elapsed < AUDIO_TIMEOUT;
}

// ==================== 时钟状态控制 ====================

void AudioOutput::pauseClock() {
    AudioPlaybackState currentState = playbackState_.load();
    if (currentState != AudioPlaybackState::Playing) {
        return;
    }
    
    // 保存暂停时的时间
    pausedPts_.store(getPlaybackTime());
    
    playbackState_.store(AudioPlaybackState::Paused, std::memory_order_release);
    qDebug() << "[AudioOutput::pauseClock] Clock paused, PTS=" << pausedPts_.load();
}

void AudioOutput::resumeClock() {
    AudioPlaybackState currentState = playbackState_.load();
    if (currentState != AudioPlaybackState::Paused) {
        return;
    }

    // 如果使用系统时钟，从暂停位置恢复
    if (useSystemClock_.load()) {
        std::lock_guard<std::mutex> lock(systemClockMutex_);
        systemClockStartTime_ = std::chrono::steady_clock::now();
        systemClockStartPts_.store(pausedPts_.load());
    }
    
    playbackState_.store(AudioPlaybackState::Playing, std::memory_order_release);
    qDebug() << "[AudioOutput::resumeClock] Clock resumed, PTS=" << pausedPts_.load();
}

void AudioOutput::resetClock() {
    lastPushPts_.store(0.0, std::memory_order_release);
    pausedPts_.store(0.0, std::memory_order_release);
    totalPushedFrames_.store(0, std::memory_order_release);
    hasPushedData_.store(false, std::memory_order_release);
    playbackState_.store(AudioPlaybackState::Stopped, std::memory_order_release);
    lastPushTime_ = std::chrono::steady_clock::now();
    
    // 重置系统时钟
    {
        std::lock_guard<std::mutex> lock(systemClockMutex_);
        systemClockStartTime_ = std::chrono::steady_clock::now();
        systemClockStartPts_.store(0.0);
    }
    
    qDebug() << "[AudioOutput::resetClock] Clock reset, PTS:" << pausedPts_.load();;
}

// ==================== 系统时钟（无音频流时使用）====================

void AudioOutput::startSystemClock(double startPts) {
    std::lock_guard<std::mutex> lock(systemClockMutex_);
    
    systemClockStartTime_ = std::chrono::steady_clock::now();
    systemClockStartPts_.store(startPts);
    useSystemClock_.store(true);
    playbackState_.store(AudioPlaybackState::Playing, std::memory_order_release);
    
    qInfo() << "[AudioOutput::startSystemClock] System clock started, start PTS=" << startPts;
}

double AudioOutput::getSystemClockTime() const {
    AudioPlaybackState state = playbackState_.load();
    
    if (state == AudioPlaybackState::Stopped) {
        return 0.0;
    }
    
    if (state == AudioPlaybackState::Paused) {
        return pausedPts_.load();
    }
    
    // 计算经过时间
    std::lock_guard<std::mutex> lock(systemClockMutex_);
    
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        now - systemClockStartTime_).count();
    double elapsedSeconds = elapsed / 1000000.0;
    
    // 系统时钟不应用播放速度（保持真实时间）
    // 原因：纯视频流时，播放位置直接使用视频帧的 PTS，不受系统时钟影响
    // 播放速度由帧率控制（calculateFrameRateDelay）来应用
    double currentPts = systemClockStartPts_.load() + elapsedSeconds;
    
    return std::max(0.0, currentPts);
}

int64_t AudioOutput::getTimeSinceLastPush() const {
    if (!hasPushedData_.load()) {
        return -1;
    }
    
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - lastPushTime_).count();
    return elapsed;
}

// ==================== 兼容接口 ====================

void AudioOutput::setAudioClock(double clock) {
    // Seek 功能已移除，此方法不再执行任何操作
    Q_UNUSED(clock);
}

// ==================== 音频回调 ====================

void AudioOutput::audioCallback(void* userdata, uint8_t* stream, int len) {
    // SDL3 使用流推送模式，不使用回调
    Q_UNUSED(userdata);
    Q_UNUSED(stream);
    Q_UNUSED(len);
}

} // namespace AdvancedPlayer
