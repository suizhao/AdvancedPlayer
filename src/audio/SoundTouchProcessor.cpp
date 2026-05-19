/**
 * @file SoundTouchProcessor.cpp
 * @brief SoundTouch 音频变速不变调处理器实现
 */

#include "SoundTouchProcessor.h"
#include <QDebug>
#include <algorithm>
#include <cmath>

namespace AdvancedPlayer {

SoundTouchProcessor::SoundTouchProcessor() {
#ifdef HAS_SOUNDTOUCH
    soundTouchHandle_ = nullptr;
#endif
    inputBuffer_.reserve(CHUNK_SIZE * 2);  // 预分配空间
}

SoundTouchProcessor::~SoundTouchProcessor() {
    clear();
#ifdef HAS_SOUNDTOUCH
    if (soundTouchHandle_) {
        soundtouch_destroyInstance(soundTouchHandle_);
        soundTouchHandle_ = nullptr;
    }
#endif
}

bool SoundTouchProcessor::initialize(int sampleRate, int channels) {
    if (initialized_.load()) {
        qWarning() << "[SoundTouchProcessor::initialize] Already initialized";
        return true;
    }
    
    if (sampleRate <= 0 || channels <= 0) {
        qWarning() << "[SoundTouchProcessor::initialize] Invalid parameters: sampleRate="
                   << sampleRate << ", channels=" << channels;
        return false;
    }
    
#ifdef HAS_SOUNDTOUCH
    soundTouchHandle_ = soundtouch_createInstance();
    if (!soundTouchHandle_) {
        qCritical() << "[SoundTouchProcessor::initialize] Failed to create SoundTouch instance";
        return false;
    }
    
    // 设置采样率和声道数（必须与输入 PCM 严格一致）
    if (soundtouch_setSampleRate(soundTouchHandle_, static_cast<unsigned int>(sampleRate)) == 0) {
        qCritical() << "[SoundTouchProcessor::initialize] Failed to set sample rate";
        soundtouch_destroyInstance(soundTouchHandle_);
        soundTouchHandle_ = nullptr;
        return false;
    }
    
    if (soundtouch_setChannels(soundTouchHandle_, static_cast<unsigned int>(channels)) == 0) {
        qCritical() << "[SoundTouchProcessor::initialize] Failed to set channel count";
        soundtouch_destroyInstance(soundTouchHandle_);
        soundTouchHandle_ = nullptr;
        return false;
    }
    
    // 设置初始速度
    soundtouch_setTempo(soundTouchHandle_, 1.0f);
    
    sampleRate_ = sampleRate;
    channels_ = channels;
    initialized_.store(true);
    
    qInfo() << "[SoundTouchProcessor::initialize] Initialized: sampleRate="
            << sampleRate << ", channels=" << channels;
    return true;
#else
    qWarning() << "[SoundTouchProcessor::initialize] SoundTouch is not enabled";
    return false;
#endif
}

void SoundTouchProcessor::setTempo(double speed, int transitionMs) {
    speed = std::clamp(speed, 0.25, 2.0);
    transitionMs = std::max(10, std::min(transitionMs, 1000));  // 限制在 10-1000ms
    
    double oldTarget = targetTempo_.load();
    if (std::abs(speed - oldTarget) < 0.001) {
        return;  // 目标速度未变化
    }
    
    std::lock_guard<std::mutex> lock(transitionMutex_);
    
    targetTempo_.store(speed);
    transitionDurationMs_ = transitionMs;
    transitionStartTime_ = std::chrono::steady_clock::now();
    isTransitioning_ = true;
    
    qInfo() << "[SoundTouchProcessor::setTempo] Set target speed:" << speed
            << "x, transition:" << transitionMs << "ms";
}

void SoundTouchProcessor::updateTempoTransition() {
    if (!isTransitioning_) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(transitionMutex_);
    
    if (!isTransitioning_) {
        return;  // 双重检查
    }
    
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - transitionStartTime_).count();
    
    double target = targetTempo_.load();
    double current = currentTempo_.load();
    
    if (elapsed >= transitionDurationMs_) {
        // 渐变完成
        currentTempo_.store(target);
        isTransitioning_ = false;
        
#ifdef HAS_SOUNDTOUCH
        if (soundTouchHandle_) {
            soundtouch_setTempo(soundTouchHandle_, static_cast<float>(target));
        }
#endif
        
        qDebug() << "[SoundTouchProcessor::updateTempoTransition] Transition completed: tempo=" << target;
    } else {
        // 线性插值
        double progress = static_cast<double>(elapsed) / transitionDurationMs_;
        double newTempo = current + (target - current) * progress;
        currentTempo_.store(newTempo);
        
#ifdef HAS_SOUNDTOUCH
        if (soundTouchHandle_) {
            soundtouch_setTempo(soundTouchHandle_, static_cast<float>(newTempo));
        }
#endif
    }
}

int SoundTouchProcessor::processSamples(const int16_t* inputData, int inputSamples,
                                       int16_t* outputBuffer, int outputBufferSize) {
    if (!initialized_.load() || !inputData || !outputBuffer) {
        return 0;
    }
    
    // 更新渐变速度
    updateTempoTransition();
    
    // 计算输出缓冲区能容纳的最大采样数（单声道采样数）
    int maxOutputSamples = outputBufferSize / (sizeof(int16_t) * channels_);
    
    // 分块处理
    return processChunks(inputData, inputSamples, outputBuffer, maxOutputSamples);
}

int SoundTouchProcessor::processChunks(const int16_t* inputData, int inputSamples,
                                      int16_t* outputBuffer, int maxOutputSamples) {
#ifdef HAS_SOUNDTOUCH
    if (!soundTouchHandle_) {
        return 0;
    }
    
    int totalOutputSamples = 0;
    int inputOffset = 0;
    
    // 处理输入数据（每次处理 CHUNK_SIZE 采样）
    while (inputOffset < inputSamples && totalOutputSamples < maxOutputSamples) {
        int samplesToProcess = std::min(CHUNK_SIZE, inputSamples - inputOffset);
        
        // 更新渐变速度（每块处理前更新）
        updateTempoTransition();
        
        // 将输入数据送入 SoundTouch
        const int16_t* chunkData = inputData + (inputOffset * channels_);
        soundtouch_putSamples_i16(soundTouchHandle_, chunkData, 
                                   static_cast<unsigned int>(samplesToProcess));
        
        inputOffset += samplesToProcess;
        
        // 从 SoundTouch 获取处理后的数据
        int availableSamples = soundtouch_numSamples(soundTouchHandle_);
        if (availableSamples > 0) {
            int samplesToReceive = std::min(availableSamples, 
                                            maxOutputSamples - totalOutputSamples);
            
            int16_t* outputChunk = outputBuffer + (totalOutputSamples * channels_);
            unsigned int received = soundtouch_receiveSamples_i16(
                soundTouchHandle_, outputChunk, 
                static_cast<unsigned int>(samplesToReceive));
            
            totalOutputSamples += static_cast<int>(received);
        }
    }
    
    return totalOutputSamples;
#else
    Q_UNUSED(inputData);
    Q_UNUSED(inputSamples);
    Q_UNUSED(outputBuffer);
    Q_UNUSED(maxOutputSamples);
    return 0;
#endif
}

int SoundTouchProcessor::flush(int16_t* outputBuffer, int outputBufferSize) {
    if (!initialized_.load() || !outputBuffer) {
        return 0;
    }
    
#ifdef HAS_SOUNDTOUCH
    if (!soundTouchHandle_) {
        return 0;
    }
    
    // 刷新 SoundTouch 内部缓冲区
    soundtouch_flush(soundTouchHandle_);
    
    // 计算输出缓冲区能容纳的最大采样数
    int maxOutputSamples = outputBufferSize / (sizeof(int16_t) * channels_);
    
    // 获取所有剩余数据
    int totalOutputSamples = 0;
    while (totalOutputSamples < maxOutputSamples) {
        int availableSamples = soundtouch_numSamples(soundTouchHandle_);
        if (availableSamples == 0) {
            break;
        }
        
        int samplesToReceive = std::min(availableSamples, 
                                        maxOutputSamples - totalOutputSamples);
        
        int16_t* outputChunk = outputBuffer + (totalOutputSamples * channels_);
        unsigned int received = soundtouch_receiveSamples_i16(
            soundTouchHandle_, outputChunk, 
            static_cast<unsigned int>(samplesToReceive));
        
        totalOutputSamples += static_cast<int>(received);
    }
    
    return totalOutputSamples;
#else
    Q_UNUSED(outputBuffer);
    Q_UNUSED(outputBufferSize);
    return 0;
#endif
}

void SoundTouchProcessor::clear() {
#ifdef HAS_SOUNDTOUCH
    if (soundTouchHandle_) {
        soundtouch_clear(soundTouchHandle_);
    }
#endif
    
    inputBufferOffset_ = 0;
    inputBuffer_.clear();
}

int SoundTouchProcessor::numSamples() const {
#ifdef HAS_SOUNDTOUCH
    if (soundTouchHandle_) {
        return static_cast<int>(soundtouch_numSamples(soundTouchHandle_));
    }
#endif
    return 0;
}

bool SoundTouchProcessor::isEmpty() const {
#ifdef HAS_SOUNDTOUCH
    if (soundTouchHandle_) {
        return soundtouch_isEmpty(soundTouchHandle_) != 0;
    }
#endif
    return true;
}

} // namespace AdvancedPlayer
