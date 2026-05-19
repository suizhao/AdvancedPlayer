#include "AudioResampler.h"
#include <QDebug>

#ifdef HAS_FFMPEG
extern "C" {
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
}
#endif

namespace AdvancedPlayer {

AudioResampler::AudioResampler(int srcRate, int srcChannels, int srcFormat,
                               int dstRate, int dstChannels, int dstFormat)
    : srcRate_(srcRate), srcChannels_(srcChannels), srcFormat_(srcFormat),
      dstRate_(dstRate), dstChannels_(dstChannels), dstFormat_(dstFormat) {
    
    qInfo() << "[AudioResampler::AudioResampler] Creating audio resampler:"
            << "src=" << srcRate << "Hz," << srcChannels << "ch"
            << "dst=" << dstRate << "Hz," << dstChannels << "ch";
    
#ifdef HAS_FFMPEG
    // 验证输出格式是否为交错格式（非平面格式）以兼容 SDL3
    if (av_sample_fmt_is_planar(static_cast<AVSampleFormat>(dstFormat))) {
        qCritical() << "[AudioResampler::AudioResampler] Critical: target format is planar!";
        qCritical() << "[AudioResampler::AudioResampler] SDL3 requires interleaved format (e.g. AV_SAMPLE_FMT_S16, not AV_SAMPLE_FMT_S16P)";
        qCritical() << "[AudioResampler::AudioResampler] Current target format:" << av_get_sample_fmt_name(static_cast<AVSampleFormat>(dstFormat));
        // 这会导致音频损坏或产生噪音
    } else {
        qDebug() << "[AudioResampler::AudioResampler] Output format is interleaved (SDL3 compatible)";
    }
    // FFmpeg 7.1 使用新的 channel_layout API
    // 创建重采样上下文
    swrCtx_ = swr_alloc();
    if (!swrCtx_) {
        qWarning() << "[AudioResampler::AudioResampler] Failed to allocate SwrContext";
        return;
    }
    
    // 设置输入参数
    AVChannelLayout src_ch_layout, dst_ch_layout;
    av_channel_layout_default(&src_ch_layout, srcChannels);
    av_channel_layout_default(&dst_ch_layout, dstChannels);
    
    av_opt_set_chlayout(swrCtx_, "in_chlayout", &src_ch_layout, 0);
    av_opt_set_int(swrCtx_, "in_sample_rate", srcRate, 0);
    av_opt_set_sample_fmt(swrCtx_, "in_sample_fmt", static_cast<AVSampleFormat>(srcFormat), 0);
    
    // 设置输出参数
    av_opt_set_chlayout(swrCtx_, "out_chlayout", &dst_ch_layout, 0);
    av_opt_set_int(swrCtx_, "out_sample_rate", dstRate, 0);
    av_opt_set_sample_fmt(swrCtx_, "out_sample_fmt", static_cast<AVSampleFormat>(dstFormat), 0);
    
    // 设置重采样质量以减少量化噪音和伪影
    // 在转换为较低位深度时使用三角高通抖动以获得更好的质量
    av_opt_set_int(swrCtx_, "dither_method", 2, 0);  // SWR_DITHER_TRIANGULAR_HIGHPASS
    // 设置滤波器大小以获得更好的质量（默认值为 16，增加到 32 以获得更高质量）
    av_opt_set_int(swrCtx_, "filter_size", 32, 0);
    // 设置线性插值的相位偏移
    av_opt_set_int(swrCtx_, "phase_shift", 10, 0);
    
    // 初始化重采样器
    if (swr_init(swrCtx_) < 0) {
        qWarning() << "[AudioResampler::AudioResampler] Failed to initialize resampler";
        swr_free(&swrCtx_);
        swrCtx_ = nullptr;
        return;
    }
    
    qInfo() << "[AudioResampler::AudioResampler] Audio resampler initialized";
    
    // 清理 channel_layout
    av_channel_layout_uninit(&src_ch_layout);
    av_channel_layout_uninit(&dst_ch_layout);
#else
    qWarning() << "[AudioResampler::AudioResampler] FFmpeg is not enabled, resampler unavailable";
#endif
}

AudioResampler::~AudioResampler() {
#ifdef HAS_FFMPEG
    if (swrCtx_) {
        swr_free(&swrCtx_);
    }
    qDebug() << "[AudioResampler::~AudioResampler] Audio resampler cleaned up";
#endif
}

int AudioResampler::resample(AVFrame* inputFrame, uint8_t** outputData) {
#ifdef HAS_FFMPEG
    if (!swrCtx_ || !inputFrame) {
        return 0;
    }
    
    // 计算输出采样数
    int64_t delay = swr_get_delay(swrCtx_, inputFrame->sample_rate);
    int outSamples = av_rescale_rnd(delay + inputFrame->nb_samples,
                                     dstRate_, inputFrame->sample_rate, 
                                     AV_ROUND_UP);
    
    // ==================== 分配输出缓冲区 ====================
    // 计算所需的缓冲区大小
    int bufferSize = av_samples_get_buffer_size(nullptr, dstChannels_, outSamples,
                                                static_cast<AVSampleFormat>(dstFormat_), 1);
    if (bufferSize < 0) {
        qWarning() << "[AudioResampler::resample] Failed to calculate buffer size:" << bufferSize;
        return 0;
    }
    
    // 分配输出缓冲区
    uint8_t* buffer = static_cast<uint8_t*>(av_malloc(bufferSize));
    if (!buffer) {
        qWarning() << "[AudioResampler::resample] Failed to allocate output buffer";
        return 0;
    }
    
    // 设置输出指针
    uint8_t* tempOutput[1] = {buffer};
    
    // 执行重采样
    int converted = swr_convert(swrCtx_, 
                                tempOutput, outSamples,
                                const_cast<const uint8_t**>(inputFrame->data),
                                inputFrame->nb_samples);
    
    if (converted < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(converted, errbuf, sizeof(errbuf));
        qWarning() << "[AudioResampler::resample] Resample failed:" << errbuf;
        av_free(buffer);
        return 0;
    }
    
    if (converted == 0) {
        qWarning() << "[AudioResampler::resample] Resample produced 0 samples";
        av_free(buffer);
        return 0;
    }
    
    // 计算实际输出数据大小
    int outSize = av_samples_get_buffer_size(nullptr, dstChannels_, converted,
                                             static_cast<AVSampleFormat>(dstFormat_), 1);
    
    if (outSize < 0) {
        qWarning() << "[AudioResampler::resample] Failed to calculate output buffer size:" << outSize;
        av_free(buffer);
        return 0;
    }
    
    // ==================== 设置输出指针（转移所有权）====================
    *outputData = buffer;
    
    // 调试：记录第一次成功重采样
    static bool firstResample = false;
    if (!firstResample) {
        int bytesPerSample = av_get_bytes_per_sample(static_cast<AVSampleFormat>(dstFormat_));
        int expectedSize = converted * dstChannels_ * bytesPerSample;
        bool isPlanar = av_sample_fmt_is_planar(static_cast<AVSampleFormat>(dstFormat_));
        
        qInfo() << "[AudioResampler::resample] First audio resample succeeded:";
        qInfo() << "[AudioResampler::resample]   Input: " << inputFrame->nb_samples << "samples " << av_get_sample_fmt_name(static_cast<AVSampleFormat>(inputFrame->format));
        qInfo() << "[AudioResampler::resample]   Output: " << converted << "samples, " << outSize << "bytes";
        qInfo() << "[AudioResampler::resample]   Format: " << av_get_sample_fmt_name(static_cast<AVSampleFormat>(dstFormat_))
                << (isPlanar ? " (planar)" : " (interleaved)");
        qInfo() << "[AudioResampler::resample]   Bytes per sample: " << bytesPerSample
                << ", channels: " << dstChannels_;
        qInfo() << "[AudioResampler::resample]   Expected size: " << expectedSize << "bytes, actual: " << outSize << "bytes";
        
        if (isPlanar) {
            qCritical() << "[AudioResampler::resample] Error: output format is planar but SDL3 needs interleaved format!";
        }
        
        if (expectedSize != outSize) {
            qWarning() << "[AudioResampler::resample] Size mismatch: expected" << expectedSize << "but got" << outSize;
        }
        
        firstResample = true;
    }
    
    return outSize;
#else
    return 0;
#endif
}

int AudioResampler::flush(uint8_t** outputData) {
#ifdef HAS_FFMPEG
    if (!swrCtx_) {
        return 0;
    }
    
    // Flush remaining data in resampler
    int outSamples = swr_convert(swrCtx_, outputData, 4096, nullptr, 0);
    if (outSamples < 0) {
        return 0;
    }
    
    if (outSamples == 0) {
        return 0; // 没有剩余数据
    }
    
    // 计算输出数据大小
    int outSize = av_samples_get_buffer_size(nullptr, dstChannels_, outSamples,
                                             static_cast<AVSampleFormat>(dstFormat_), 1);
    return outSize;
#else
    return 0;
#endif
}

bool AudioResampler::reconfigure(int srcRate, int srcChannels, int srcFormat,
                                  int dstRate, int dstChannels, int dstFormat) {
    qInfo() << "[AudioResampler::reconfigure] Trying to reconfigure resampler";
    
#ifdef HAS_FFMPEG
    // ==================== 资源复用优化 ====================
    // 检查是否可以复用当前的重采样器（格式完全相同）
    
    bool formatSame = (srcRate_ == srcRate && srcChannels_ == srcChannels && 
                       srcFormat_ == srcFormat && dstRate_ == dstRate && 
                       dstChannels_ == dstChannels && dstFormat_ == dstFormat);
    
    if (formatSame && swrCtx_) {
        // ========== 快速路径：格式相同，只刷新内部状态 ==========
        qInfo() << "[AudioResampler::reconfigure] Same format, reusing existing resampler (fast path)";
        
        // 清空重采样器内部缓冲区（丢弃残留数据）
        swr_convert(swrCtx_, nullptr, 0, nullptr, 0); // 强制触发内部状态刷新

        // 处理延迟样本，一般场景极小？
        int64_t delay = swr_get_delay(swrCtx_, dstRate);
        if (delay > 0 && delay < INT_MAX) { // 避免delay过大导致内存溢出
            uint8_t* silence = nullptr;
            int silence_size = av_samples_get_buffer_size(nullptr, dstChannels, delay,
                                                          static_cast<AVSampleFormat>(dstFormat), 1);
            if (silence_size > 0) {
                // 检查内存分配是否成功
                int ret = av_samples_alloc(&silence, nullptr, dstChannels, delay,
                                           static_cast<AVSampleFormat>(dstFormat), 1);
                if (ret >= 0 && silence != nullptr) {
                    swr_convert(swrCtx_, &silence, delay, nullptr, 0);
                    av_freep(&silence);
                } else {
                    qWarning() << "[AudioResampler::reconfigure] Failed to allocate silent buffer, delay=" << delay;
                }
            }
            swr_drop_output(swrCtx_, delay);
        }

        swr_set_compensation(swrCtx_, 0, 1);  // 重置补偿
        av_opt_set_int(swrCtx_, "reset_timestamp", 1, 0); // 强制重置时间戳

        if (swr_init(swrCtx_) < 0) {
            qWarning() << "[AudioResampler::reconfigure] Re-init after reset failed, fallback to rebuild";
            // 重置失败则销毁重建（兜底）
            swr_free(&swrCtx_);
            swrCtx_ = nullptr;
        } else {
            qInfo() << "[AudioResampler::reconfigure] Resampler fast-reset completed";
            return true;
        }
    }
    
    // ========== 慢速路径：格式不同，需要重新创建 ==========
    qInfo() << "[AudioResampler::reconfigure] Format changed, rebuilding resampler (slow path)";
    qInfo() << "[AudioResampler::reconfigure]   Old format: src=" << srcRate_ << "Hz/" << srcChannels_ << "ch"
            << " -> dst=" << dstRate_ << "Hz/" << dstChannels_ << "ch";
    qInfo() << "[AudioResampler::reconfigure]   New format: src=" << srcRate << "Hz/" << srcChannels << "ch"
            << " -> dst=" << dstRate << "Hz/" << dstChannels << "ch";
    
    // 销毁旧的重采样器
    if (swrCtx_) {
        swr_free(&swrCtx_);
        swrCtx_ = nullptr;
    }
    
    // 验证输出格式是否为交错格式（非平面格式）以兼容 SDL3
    if (av_sample_fmt_is_planar(static_cast<AVSampleFormat>(dstFormat))) {
        qCritical() << "[AudioResampler::reconfigure] Critical: target format is planar!";
        return false;
    }
    
    // 创建新的重采样上下文
    swrCtx_ = swr_alloc();
    if (!swrCtx_) {
        qWarning() << "[AudioResampler::reconfigure] Failed to allocate SwrContext";
        return false;
    }
    
    // 设置参数
    AVChannelLayout src_ch_layout, dst_ch_layout;
    av_channel_layout_default(&src_ch_layout, srcChannels);
    av_channel_layout_default(&dst_ch_layout, dstChannels);
    
    av_opt_set_chlayout(swrCtx_, "in_chlayout", &src_ch_layout, 0);
    av_opt_set_int(swrCtx_, "in_sample_rate", srcRate, 0);
    av_opt_set_sample_fmt(swrCtx_, "in_sample_fmt", static_cast<AVSampleFormat>(srcFormat), 0);
    
    av_opt_set_chlayout(swrCtx_, "out_chlayout", &dst_ch_layout, 0);
    av_opt_set_int(swrCtx_, "out_sample_rate", dstRate, 0);
    av_opt_set_sample_fmt(swrCtx_, "out_sample_fmt", static_cast<AVSampleFormat>(dstFormat), 0);
    
    // 设置重采样质量
    av_opt_set_int(swrCtx_, "dither_method", 2, 0);
    av_opt_set_int(swrCtx_, "filter_size", 32, 0);
    av_opt_set_int(swrCtx_, "phase_shift", 10, 0);
    
    // 初始化
    if (swr_init(swrCtx_) < 0) {
        qWarning() << "[AudioResampler::reconfigure] Failed to initialize resampler";
        swr_free(&swrCtx_);
        swrCtx_ = nullptr;
        av_channel_layout_uninit(&src_ch_layout);
        av_channel_layout_uninit(&dst_ch_layout);
        return false;
    }
    
    // 更新保存的格式参数
    srcRate_ = srcRate;
    srcChannels_ = srcChannels;
    srcFormat_ = srcFormat;
    dstRate_ = dstRate;
    dstChannels_ = dstChannels;
    dstFormat_ = dstFormat;
    
    av_channel_layout_uninit(&src_ch_layout);
    av_channel_layout_uninit(&dst_ch_layout);
    
    qInfo() << "[AudioResampler::reconfigure] Resampler rebuilt";
    return true;
#else
    Q_UNUSED(srcRate); Q_UNUSED(srcChannels); Q_UNUSED(srcFormat);
    Q_UNUSED(dstRate); Q_UNUSED(dstChannels); Q_UNUSED(dstFormat);
    return false;
#endif
}

} // namespace AdvancedPlayer

