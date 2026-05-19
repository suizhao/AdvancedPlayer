/**
 * @file PlaybackController.cpp
 * @brief 播放控制器实现（简化版，无独立时钟组件）
 */

#include "PlaybackController.h"
#include "VideoRenderThread.h"
#include "AudioRenderThread.h"
#include "RenderContext.h"
#include "src/decoder/HardwareDecoder.h"
#include "src/decoder/SoftwareDecoder.h"
#include "src/audio/AudioOutput.h"
#include "src/audio/AudioResampler.h"
#include "src/video/VideoOutput.h"
#include "src/ui/SettingsManager.h"
#include "LockFreePacketQueue.h"
#include "LockFreeVideoFrameQueue.h"
#include "LockFreeAudioFrameQueue.h"
#include "DemuxThread.h"
#include "VideoDecodeThread.h"
#include "AudioDecodeThread.h"
#include "DecodeContext.h"
#include "DemuxContext.h"
#include "DemuxThread.h"
#include <QDebug>
#include <QTimer>
#include <QFileInfo>
#include <QDir>
#include <QDirIterator>
#include <cmath>
#include <limits>
// #include <chrono>

#ifdef HAS_FFMPEG
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/time.h>
#include <libavutil/imgutils.h>
#include <libavutil/dict.h>
#include <libavutil/log.h>
#include <libavutil/mathematics.h>
#include <libavutil/opt.h>
}
#endif

#ifdef HAS_SDL3
#include <SDL3/SDL.h>
#endif

namespace AdvancedPlayer {

void AVFormatContextDeleter::operator()(AVFormatContext* ctx) const {
#ifdef HAS_FFMPEG
    if (ctx) {
        avformat_close_input(&ctx);
    }
#else
    (void)ctx;
#endif
}

void AVCodecContextDeleter::operator()(AVCodecContext* ctx) const {
#ifdef HAS_FFMPEG
    if (ctx) {
        avcodec_free_context(&ctx);
    }
#else
    (void)ctx;
#endif
}

int PlaybackController::ffmpegInterruptCallback(void* opaque) {
    if (!opaque) {
        return 0;
    }
    auto* controller = static_cast<PlaybackController*>(opaque);
    return controller->ioInterruptRequested_.load(std::memory_order_acquire) ? 1 : 0;
}

// ==================== 构造与析构 ====================

PlaybackController::PlaybackController(QObject* parent)
    : QObject(parent) {
    // ==================== 无锁队列体系 ====================
    videoPacketQueue_ = std::make_unique<LockFreePacketQueue>(256, QStringLiteral("VideoPacketQueue"));
    audioPacketQueue_ = std::make_unique<LockFreePacketQueue>(256, QStringLiteral("AudioPacketQueue"));
    audioFrameQueue_ = std::make_unique<LockFreeAudioFrameQueue>(128, QStringLiteral("AudioFrameQueue"));
    videoFrameQueue_ = std::make_unique<LockFreeVideoFrameQueue>(64, QStringLiteral("VideoFrameQueue"));
    
    // 创建线程
    demuxThread_ = std::make_unique<DemuxThread>(this);
    videoDecodeThread_ = std::make_unique<VideoDecodeThread>(this);
    audioDecodeThread_ = std::make_unique<AudioDecodeThread>(this);
    videoRenderThread_ = std::make_unique<VideoRenderThread>(this);
    audioRenderThread_ = std::make_unique<AudioRenderThread>(this);
    
    // 连接视频渲染线程信号
    connect(videoRenderThread_.get(), &VideoRenderThread::positionChanged,
            this, [this](int64_t positionMs) {
                position_ = positionMs;
                emit positionChanged(positionMs);
            });
    
    connect(videoRenderThread_.get(), &VideoRenderThread::playbackFinished,
            this, [this]() {
                // 使用原子操作确保只处理一次播放结束事件
                // 这样可以避免视频和音频线程都发出信号时重复处理
                bool expected = false;
                if (playbackFinishedEmitted_.compare_exchange_strong(expected, true)) {
                    qInfo() << "[PlaybackController] Playback finished (all streams ended)";
                    state_.store(PlaybackState::Stopped);
                    position_ = 0;
                    emit stateChanged(state_.load());
                    emit positionChanged(0);
                } else {
                    qDebug() << "[PlaybackController] Playback finished signal already handled, ignoring duplicate";
                }
            });
    
    connect(videoRenderThread_.get(), &VideoRenderThread::error,
            this, &PlaybackController::errorOccurred);

    ensureFrameReadyConnection();
    
    // 连接音频播放线程信号
    connect(audioRenderThread_.get(), &AudioRenderThread::positionChanged,
            this, [this](int64_t positionMs) {
                position_ = positionMs;
                emit positionChanged(positionMs);
            });
    
    connect(audioRenderThread_.get(), &AudioRenderThread::playbackFinished,
            this, [this]() {
                // 使用原子操作确保只处理一次播放结束事件
                // 这样可以避免视频和音频线程都发出信号时重复处理
                bool expected = false;
                if (playbackFinishedEmitted_.compare_exchange_strong(expected, true)) {
                    qInfo() << "[PlaybackController] Playback finished (all streams ended)";
                    state_ = PlaybackState::Stopped;
                    position_ = 0;
                    emit stateChanged(state_.load());
                    emit positionChanged(0);
                } else {
                    qDebug() << "[PlaybackController] Playback finished signal already handled, ignoring duplicate";
                }
            });
    
    connect(audioRenderThread_.get(), &AudioRenderThread::error,
            this, &PlaybackController::errorOccurred);
    
    qInfo() << "[PlaybackController] Playback controller created";
}

PlaybackController::~PlaybackController() {
    closeMedia();
    qInfo() << "[PlaybackController::~PlaybackController] Destroyed";
}

// ==================== 媒体控制 ====================

bool PlaybackController::openMedia(const QString& mediaPath, const bool isNetStream) {
    qInfo() << "[PlaybackController::openMedia] Opening media:" << mediaPath;
    isClosing_.store(false, std::memory_order_release);
    ensureFrameReadyConnection();
    
    isNetworkStream_ = isNetStream;
    state_ = PlaybackState::Loading;
    emit stateChanged(state_.load());
    
#ifdef HAS_FFMPEG
    // 设置 FFmpeg 日志级别：抑制信息性警告（如 AAC duration 估算警告）
    // AV_LOG_WARNING (24) 会显示警告，AV_LOG_ERROR (16) 只显示错误
    // 这里设置为 AV_LOG_ERROR，抑制 "Estimating duration from bitrate" 这类警告
    // av_log_set_level(AV_LOG_ERROR);
    
    QByteArray mediaPathBytes = mediaPath.toUtf8();
    AVDictionary* options = nullptr;
    
    if (isNetworkStream_) {
        // ========== 网络流配置（支持Seek的点播）==========

        // 探测：平衡快速启动与索引完整性
        av_dict_set_int(&options, "probesize", 2097152, 0);         // 2MB
        av_dict_set_int(&options, "analyzeduration", 500000, 0);    // 0.5秒
        av_dict_set_int(&options, "fpsprobesize", 10, 0);           // 10帧
        av_dict_set_int(&options, "max_probe_packets", 100, 0);     // 100包
        //av_dict_set_int(&options, "duration_probesize", 0, 0);       // 默认0

        // fflags：保留索引用于Seek，不加速
        av_dict_set(&options, "fflags", "+genpts+discardcorrupt", 0);

        // Seek优化
        av_dict_set_int(&options, "seek2any", 1, 0);

        // 缓冲与延迟（平衡流畅与响应）
        av_dict_set_int(&options, "max_delay", 1000000, 0);           // 1秒
        av_dict_set_int(&options, "rtbufsize", 15728640, 0);          // 15MB预缓冲
        av_dict_set_int(&options, "indexmem", 4194304, 0);            // 4MB内存索引，用于HTTP点播

        // 错误检测（适度容错，网络不稳定）
        av_dict_set(&options, "err_detect", "+crccheck+bitstream+buffer", 0);

        // 时间戳处理
        av_dict_set_int(&options, "skip_estimate_duration_from_pts", 1, 0); // 需要准确时长

        // ========== 协议特定选项（FFmpeg会自动忽略不适用的）=========
        av_dict_set(&options, "timeout", "20000000", 0);              // 20s连接超时
        av_dict_set(&options, "stimeout", "20000000", 0);             // 20s socket超时
        av_dict_set(&options, "buffer_size", "16777216", 0);          // 16MB网络缓冲，HTTP IO接收缓存
        av_dict_set(&options, "user_agent", "AdvancedPlayer/1.0", 0); // HTTP User-Agent
        av_dict_set(&options, "reconnect", "1", 0);                   // 开启自动重连
        av_dict_set(&options, "reconnect_streamed", "1", 0);          // 流式重连
        av_dict_set(&options, "reconnect_max_streams", "10", 0);      // 最多重连10次
        av_dict_set(&options, "rtsp_transport", "tcp", 0);            // RTSP用TCP
        av_dict_set(&options, "rtsp_flags", "prefer_tcp", 0);         // RTSP优先TCP
        av_dict_set(&options, "rtsp_timeout", "5000000", 0);          // RTSP 5秒心跳

    } else {
        // ========== 本地文件配置（最大化精度）==========

        // 探测
        av_dict_set_int(&options, "probesize", 52428800, 0);            // 50MB
        av_dict_set_int(&options, "formatprobesize", 2097152, 0);
        av_dict_set_int(&options, "analyzeduration", 10000000, 0);       // 10秒
        av_dict_set_int(&options, "fpsprobesize", 100, 0);
        av_dict_set_int(&options, "max_probe_packets", 5000, 0);
        av_dict_set_int(&options, "duration_probesize", 52428800, 0);       // 默认0
        av_dict_set_int(&options, "max_ts_probe", 500, 0);

        // fflags：快速seek + 生成PTS + 容错
        av_dict_set(&options, "fflags", "+genpts+fastseek+discardcorrupt", 0);

        // 索引与Seek
        av_dict_set_int(&options, "indexmem", 52428800, 0); // 50MB
        av_dict_set_int(&options, "seek2any", 1, 0);

        // 严格错误检测（本地文件可以严格）
        av_dict_set(&options, "err_detect", "+crccheck+bitstream+buffer+careful+compliant+aggressive", 0);
    }
    // 时间戳
    av_dict_set_int(&options, "correct_ts_overflow", 1, 0);
    av_dict_set_int(&options, "avoid_negative_ts", AVFMT_AVOID_NEG_TS_AUTO, 0);

    // AVDictionaryEntry *entry = NULL;
    // // 循环遍历：第一个参数是字典，第二个是上一个遍历的键（NULL 表示从第一个开始），
    // // 第三个是匹配模式（空表示匹配所有），第四个是遍历标志（AV_DICT_IGNORE_SUFFIX 忽略后缀匹配）
    // while ((entry = av_dict_get(options, "", entry, AV_DICT_IGNORE_SUFFIX))) {
    //     // entry->key: 字典的键
    //     // entry->value: 字典的值
    //     qInfo() << QStringLiteral("键: %1\t值: %2").arg(entry->key, 15).arg(entry->value);
    // }
    // qInfo() <<"====================================\n\n";
    
    // auto start = std::chrono::steady_clock::now();
    AVFormatContext* openedFormatCtx = avformat_alloc_context();
    if (!openedFormatCtx) {
        qWarning() << "[PlaybackController::openMedia] Failed to allocate format context";
        state_ = PlaybackState::Error;
        return false;
    }
    openedFormatCtx->interrupt_callback.callback = &PlaybackController::ffmpegInterruptCallback;
    openedFormatCtx->interrupt_callback.opaque = this;
    ioInterruptRequested_.store(false, std::memory_order_release);

    int ret = avformat_open_input(&openedFormatCtx, mediaPathBytes.constData(), nullptr, &options);
    av_dict_free(&options);
    // auto end = std::chrono::steady_clock::now();
    // qInfo()<< std::chrono::duration<double, std::milli> (end - start).count();
    // av_dict_free(&options);
    // qInfo() <<"====================================\n\n";
    
    if (ret < 0) {
        if (openedFormatCtx) {
            avformat_close_input(&openedFormatCtx);
        }
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        qWarning() << "[PlaybackController::openMedia] Failed to open media:" << errbuf;
        state_ = PlaybackState::Error;
        return false;
    }
    formatCtx_.reset(openedFormatCtx);
    
    ret = avformat_find_stream_info(formatCtx_.get(), nullptr);

    if (ret < 0) {
        cleanupFFmpegResources();
        state_ = PlaybackState::Error;
        return false;
    }
    
    // 查找流
    videoStreamIndex_ = -1;
    audioStreamIndex_ = -1;
    for (unsigned int i = 0; i < formatCtx_->nb_streams; ++i) {
        AVCodecParameters* codecpar = formatCtx_->streams[i]->codecpar;
        
        if (codecpar->codec_type == AVMEDIA_TYPE_VIDEO && videoStreamIndex_ < 0) {
            videoStreamIndex_ = i;
        } else if (codecpar->codec_type == AVMEDIA_TYPE_AUDIO && audioStreamIndex_ < 0) {
            audioStreamIndex_ = i;
        }
    }
    
    if (videoStreamIndex_ < 0 && audioStreamIndex_ < 0) {
        cleanupFFmpegResources();
        state_ = PlaybackState::Error;
        return false;
    }
    
    // 初始化解码器
    if (videoStreamIndex_ >= 0) {
        initializeVideoDecoder();
    }
    
    if (audioStreamIndex_ >= 0) {
        if (!initializeAudioDecoder()) {
            audioStreamIndex_ = -1;
        }
    }
    
    // 纯视频流：创建 AudioOutput 用于系统时钟（帧率控制）
    if (videoStreamIndex_ >= 0 && audioStreamIndex_ < 0 && !audioOutput_) {
#ifdef HAS_SDL3
        // 使用默认参数创建 AudioOutput（纯视频流不需要实际音频输出）
        audioOutput_ = std::make_unique<AudioOutput>();
        // 使用默认采样率初始化（实际不会播放音频，仅用于系统时钟）
        if (!audioOutput_->initialize(44100, 2, SDL_AUDIO_S16)) {
            qWarning() << "[PlaybackController::openMedia] Video-only stream: AudioOutput init failed, frame-rate control may be unavailable";
            audioOutput_.reset();
        } else {
            qInfo() << "[PlaybackController::openMedia] Video-only stream: AudioOutput created for system clock";
        }
#endif
    }
    
    // 获取时长（优先从流信息获取，更准确）
    int64_t durationMs = 0;
    
    // 优先从容器格式获取（最准确）
    if (formatCtx_->duration != AV_NOPTS_VALUE) {
        durationMs = formatCtx_->duration * 1000 / AV_TIME_BASE;
    }
    
    // 如果容器时长无效，尝试从流信息获取
    if (durationMs <= 0) {
        // 优先使用视频流时长
        if (videoStreamIndex_ >= 0) {
            AVStream* videoStream = formatCtx_->streams[videoStreamIndex_];
            if (videoStream->duration != AV_NOPTS_VALUE && videoStream->time_base.den > 0) {
                durationMs = av_rescale_q(videoStream->duration, videoStream->time_base, 
                                         {1, 1000});
            }
        }
        
        // 如果视频流时长无效，尝试音频流时长
        if (durationMs <= 0 && audioStreamIndex_ >= 0) {
            AVStream* audioStream = formatCtx_->streams[audioStreamIndex_];
            if (audioStream->duration != AV_NOPTS_VALUE && audioStream->time_base.den > 0) {
                durationMs = av_rescale_q(audioStream->duration, audioStream->time_base, 
                                         {1, 1000});
            }
        }
    }
    
    duration_ = durationMs;
    emit durationChanged(duration_.load());
    
    currentFilePath_ = mediaPath;
    position_ = 0;
    emit positionChanged(0);
    
    state_ = PlaybackState::Stopped;
    emit stateChanged(state_.load());
    
    qInfo() << "[PlaybackController::openMedia] Media opened successfully";
    return true;
#else
    state_ = PlaybackState::Error;
    return false;
#endif
}

void PlaybackController::closeMedia() {
    qInfo() << "[PlaybackController::closeMedia] Closing media: begin";
    isClosing_.store(true, std::memory_order_release);
    ioInterruptRequested_.store(true, std::memory_order_release);

    // 先切断跨线程视频帧投递，避免关闭阶段仍有 queued frameReady 触发 UI 路径
    if (frameReadyConnection_) {
        QObject::disconnect(frameReadyConnection_);
        frameReadyConnection_ = {};
    }
    
    state_ = PlaybackState::Stopped;
    emit stateChanged(state_.load());
    position_ = 0;
    emit positionChanged(0);
    
    // ==================== 关闭流程：严格的线程安全顺序 ====================
    // 
    // 正确顺序：
    //   1. 中止队列   → 唤醒所有在队列上阻塞的线程
    //   2. 停止线程   → 通过 jthread::reset() 发出 stop_token 并 join，确保完全退出
    //   3. flush 解码器缓冲区 → 此时无任何线程在访问 codec context，安全
    //   4. 清理 FFmpeg 资源
    //
    // 【常见错误】avcodec_flush_buffers 在线程 join 之前调用 → FFmpeg codec context
    // 完全不是线程安全的，decode/flush 并发必然导致段错误（CUDA 硬件解码器尤甚）
    
    qInfo() << "[PlaybackController::closeMedia] Stage 1/6: abort queues";
    // 1. 先中止所有队列，让线程能够快速退出等待/重试循环
    if (videoPacketQueue_) videoPacketQueue_->abort();
    if (audioPacketQueue_) audioPacketQueue_->abort();
    if (videoFrameQueue_) videoFrameQueue_->abort();
    if (audioFrameQueue_) audioFrameQueue_->abort();
    
    qInfo() << "[PlaybackController::closeMedia] Stage 2/6: stop worker threads";
    // 2. 两阶段停机：
    //    Phase A: 广播 requestStop，避免边 stop 边析构导致的退出竞态
    //    Phase B: 再统一 join/reset
    requestStopAllWorkerThreads();
    joinAllWorkerThreads();
    
    qInfo() << "[PlaybackController::closeMedia] Stage 3/6: flush codec buffers";
    // 3. 所有线程已完全退出，现在可以安全地 flush 解码器缓冲区
    //    目的：清除解码器内部引用计数，防止 "Missing reference picture" 和 CUDA 错误
#ifdef HAS_FFMPEG
    if (videoCodecCtx_) {
        avcodec_flush_buffers(videoCodecCtx_.get());
        qDebug() << "[PlaybackController::closeMedia] Video decoder buffers flushed (thread-safe)";
    }
    if (audioCodecCtx_) {
        avcodec_flush_buffers(audioCodecCtx_.get());
        qDebug() << "[PlaybackController::closeMedia] Audio decoder buffers flushed (thread-safe)";
    }
#endif

    qInfo() << "[PlaybackController::closeMedia] Stage 4/6: pause and clear audio output";
    // 4. 暂停并清空音频缓冲区（不修改用户的音量设置）
    {
        std::lock_guard<std::mutex> lock(resourceMutex_);
        if (audioOutput_) {
            audioOutput_->pause();
            audioOutput_->clear();
        }
    }
    
    qInfo() << "[PlaybackController::closeMedia] Stage 5/6: clear queues";
    // 5. 清空队列（释放资源）
    if (videoPacketQueue_) videoPacketQueue_->clear();
    if (audioPacketQueue_) audioPacketQueue_->clear();
    if (videoFrameQueue_) videoFrameQueue_->clear();
    if (audioFrameQueue_) audioFrameQueue_->clear();

    qInfo() << "[PlaybackController::closeMedia] Stage 6/6: cleanup FFmpeg resources";
    // 6. 清理 FFmpeg 资源
    cleanupFFmpegResources();
    
    duration_ = 0;
    emit durationChanged(0);
    currentFilePath_.clear();
    ioInterruptRequested_.store(false, std::memory_order_release);
    isClosing_.store(false, std::memory_order_release);
    qInfo() << "[PlaybackController::closeMedia] Closing media: done";
}

void PlaybackController::closeMediaAsync() {
    if (closeAsyncInProgress_) {
        qInfo() << "[PlaybackController::closeMediaAsync] Close already in progress, skip duplicate request";
        return;
    }

    qInfo() << "[PlaybackController::closeMediaAsync] Closing media asynchronously: begin";
    closeAsyncInProgress_ = true;
    closeAsyncJoinStep_ = 0;
    isClosing_.store(true, std::memory_order_release);
    ioInterruptRequested_.store(true, std::memory_order_release);

    if (frameReadyConnection_) {
        QObject::disconnect(frameReadyConnection_);
        frameReadyConnection_ = {};
    }

    state_ = PlaybackState::Stopped;
    emit stateChanged(state_.load());
    position_ = 0;
    emit positionChanged(0);

    if (videoPacketQueue_) videoPacketQueue_->abort();
    if (audioPacketQueue_) audioPacketQueue_->abort();
    if (videoFrameQueue_) videoFrameQueue_->abort();
    if (audioFrameQueue_) audioFrameQueue_->abort();

    requestStopAllWorkerThreads();
    QTimer::singleShot(0, this, &PlaybackController::continueCloseMediaAsync);
}

void PlaybackController::play() {
    if (state_ == PlaybackState::Playing) return;
    
    qInfo() << "[PlaybackController::play] Start playback";
    
    // 设置 AudioOutput 时钟状态并恢复音频设备
    {
        std::lock_guard<std::mutex> lock(resourceMutex_);
        if (audioOutput_) {
            if (state_ == PlaybackState::Paused) {
                // 从暂停恢复
                audioOutput_->resume();
            } else {
                // 从停止状态开始播放
                double startPts = position_.load() / 1000.0;
                
                if (audioStreamIndex_ >= 0) {
                    // 有音频：使用音频驱动时钟
                    audioOutput_->setUseSystemClock(false);
                } else {
                    // 无音频：使用系统时钟
                    audioOutput_->setUseSystemClock(true);
                    audioOutput_->startSystemClock(startPts);
                }
                
                // 同步当前速度到 AudioOutput（SoundTouch 唯一模式）。
                // 切换文件后 AudioOutput 可能被重建或缓冲区已清空，
                // 必须重新激活 SoundTouch tempo，否则速度会回落到初始值。
                double currentSpeed = playbackSpeed_.load();
                audioOutput_->setPlaybackSpeed(currentSpeed);
                
                // 恢复 SDL 音频设备
                audioOutput_->resume();
            }
        }
    }
    
    state_ = PlaybackState::Playing;
    emit stateChanged(state_.load());
    
    startAllThread();
}

void PlaybackController::pause() {
    if (state_ != PlaybackState::Playing) return;
    
    qInfo() << "[PlaybackController::pause] Pause";
    
    state_ = PlaybackState::Paused;
    
    {
        std::lock_guard<std::mutex> lock(resourceMutex_);
        if (audioOutput_) {
            audioOutput_->pause();
        }
    }
    
    emit stateChanged(state_.load());
}

void PlaybackController::stop() {
    qInfo() << "[PlaybackController::stop] Stop";
    
    state_ = PlaybackState::Stopped;
    
    {
        std::lock_guard<std::mutex> lock(resourceMutex_);
        if (audioOutput_) {
            audioOutput_->pause();
            audioOutput_->clear();
        }
    }
    
    // 与 closeMedia 保持一致：先 requestStop 全量广播，再统一 join/reset
    requestStopAllWorkerThreads();
    joinAllWorkerThreads();
    
    if (videoPacketQueue_) videoPacketQueue_->clear();
    if (audioPacketQueue_) audioPacketQueue_->clear();
    if (videoFrameQueue_) videoFrameQueue_->clear();
    if (audioFrameQueue_) audioFrameQueue_->clear();
    
    position_ = 0;
    emit stateChanged(state_.load());
    emit positionChanged(0);
}

bool PlaybackController::seekTo(int64_t positionMs) {
#ifdef HAS_FFMPEG
    if (!formatCtx_) {
        return false;
    }
    if (state_.load() == PlaybackState::Loading || isClosing_.load(std::memory_order_acquire)) {
        return false;
    }

    const int64_t mediaDurationMs = duration_.load();
    if (mediaDurationMs > 0) {
        positionMs = std::clamp<int64_t>(positionMs, 0, mediaDurationMs);
    } else if (positionMs < 0) {
        positionMs = 0;
    }

    const bool wasPlaying = (state_.load() == PlaybackState::Playing);
    const int64_t targetTs = av_rescale_q(positionMs, AVRational{1, 1000}, AV_TIME_BASE_Q);
    constexpr int seekFlags = AVSEEK_FLAG_BACKWARD;

    qInfo() << "[PlaybackController::seekTo] Begin seek to" << positionMs << "ms";

    // 1) 中断阻塞读，并停止所有 worker，保证 seek 与读/解码不并发
    ioInterruptRequested_.store(true, std::memory_order_release);
    if (videoPacketQueue_) videoPacketQueue_->abort();
    if (audioPacketQueue_) audioPacketQueue_->abort();
    if (videoFrameQueue_) videoFrameQueue_->abort();
    if (audioFrameQueue_) audioFrameQueue_->abort();

    requestStopAllWorkerThreads();
    joinAllWorkerThreads();

    // 2) 执行底层 seek（保持中断打开，避免卡死）
    int ret = avformat_seek_file(formatCtx_.get(), -1,
                                 std::numeric_limits<int64_t>::lowest(),
                                 targetTs,
                                 std::numeric_limits<int64_t>::max(),
                                 seekFlags);
    if (ret < 0) {
        ret = av_seek_frame(formatCtx_.get(), -1, targetTs, seekFlags);
    }
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        qWarning() << "[PlaybackController::seekTo] Seek failed:" << errbuf;
        if (videoPacketQueue_) videoPacketQueue_->resume();
        if (audioPacketQueue_) audioPacketQueue_->resume();
        if (videoFrameQueue_) videoFrameQueue_->resume();
        if (audioFrameQueue_) audioFrameQueue_->resume();
        ioInterruptRequested_.store(false, std::memory_order_release);
        if (wasPlaying) {
            startAllThread();
        }
        return false;
    }

    // 3) flush 解码器内部缓冲
    if (videoCodecCtx_) {
        avcodec_flush_buffers(videoCodecCtx_.get());
    }
    if (audioCodecCtx_) {
        avcodec_flush_buffers(audioCodecCtx_.get());
    }

    // 4) flush 队列并恢复可读写状态
    if (videoPacketQueue_) {
        videoPacketQueue_->clear();
        videoPacketQueue_->resume();
    }
    if (audioPacketQueue_) {
        audioPacketQueue_->clear();
        audioPacketQueue_->resume();
    }
    if (videoFrameQueue_) {
        videoFrameQueue_->clear();
        videoFrameQueue_->resume();
    }
    if (audioFrameQueue_) {
        audioFrameQueue_->clear();
        audioFrameQueue_->resume();
    }

    // 5) 重置时钟/EOF状态
    {
        std::lock_guard<std::mutex> lock(resourceMutex_);
        if (audioOutput_) {
            audioOutput_->clear();
            if (audioStreamIndex_ >= 0) {
                audioOutput_->setUseSystemClock(false);
            } else {
                audioOutput_->setUseSystemClock(true);
                audioOutput_->startSystemClock(static_cast<double>(positionMs) / 1000.0);
            }
            if (wasPlaying) {
                audioOutput_->resume();
            } else {
                audioOutput_->pause();
            }
        }
    }
    videoEofReceived_.store(false, std::memory_order_release);
    audioEofReceived_.store(false, std::memory_order_release);
    playbackFinishedEmitted_.store(false, std::memory_order_release);

    position_.store(positionMs, std::memory_order_release);
    emit positionChanged(positionMs);

    // 6) 关闭中断并按原状态恢复 demux/decode/render
    ioInterruptRequested_.store(false, std::memory_order_release);
    if (wasPlaying) {
        startAllThread();
    }

    qInfo() << "[PlaybackController::seekTo] Seek completed to" << positionMs << "ms";
    return true;
#else
    Q_UNUSED(positionMs);
    return false;
#endif
}

// ==================== 倍速/音量控制 ====================

void PlaybackController::setPlaybackSpeed(double speed) {
    speed = std::clamp(speed, 0.25, 2.0);
    
    double oldSpeed = playbackSpeed_.load();
    if (std::abs(speed - oldSpeed) < 0.001) return;
    
    qInfo() << "[PlaybackController::setPlaybackSpeed] Set playback speed:" << speed << "x";
    playbackSpeed_ = speed;
    
    {
        std::lock_guard<std::mutex> lock(resourceMutex_);
        if (audioOutput_) {
            audioOutput_->setPlaybackSpeed(speed);
        }
    }
}

void PlaybackController::setVolume(float volume) {
    volume = std::clamp(volume, 0.0f, 1.0f);
    
    // 注意：AudioOutput 内部是原子变量，不需要外部锁
    // 避免与渲染线程的锁竞争导致 UI 卡顿
    if (audioOutput_) {
        audioOutput_->setVolume(volume);
    }
}

float PlaybackController::getVolume() const {
    // AudioOutput::getVolume() 返回原子变量，线程安全
    if (audioOutput_) return audioOutput_->getVolume();
    return 1.0f;
}

void PlaybackController::setMuted(bool muted) {
    // AudioOutput 内部是原子变量，不需要外部锁
    if (audioOutput_) audioOutput_->setMuted(muted);
}

bool PlaybackController::isMuted() const {
    // AudioOutput::isMuted() 返回原子变量，线程安全
    if (audioOutput_) return audioOutput_->isMuted();
    return false;
}

// ==================== 媒体信息 ====================

AVFrame* PlaybackController::getCurrentFrame() {
#ifdef HAS_FFMPEG
    if (videoRenderThread_) return videoRenderThread_->getCurrentFrameClone();
#endif
    return nullptr;
}

int PlaybackController::getVideoWidth() const {
#ifdef HAS_FFMPEG
    if (videoCodecCtx_) return videoCodecCtx_->width;
#endif
    return 0;
}

int PlaybackController::getVideoHeight() const {
#ifdef HAS_FFMPEG
    if (videoCodecCtx_) return videoCodecCtx_->height;
#endif
    return 0;
}

QString PlaybackController::getVideoCodec() const {
#ifdef HAS_FFMPEG
    if (videoCodecCtx_ && videoCodecCtx_->codec) {
        return QString::fromLatin1(videoCodecCtx_->codec->name);
    }
#endif
    return QString();
}

double PlaybackController::getFrameRate() const {
#ifdef HAS_FFMPEG
    if (formatCtx_ && videoStreamIndex_ >= 0) {
        AVRational frameRate = formatCtx_->streams[videoStreamIndex_]->avg_frame_rate;
        if (frameRate.den != 0) {
            return static_cast<double>(frameRate.num) / frameRate.den;
        }
    }
#endif
    return 0.0;
}

int64_t PlaybackController::getVideoBitrate() const {
#ifdef HAS_FFMPEG
    if (formatCtx_ && videoStreamIndex_ >= 0) {
        return formatCtx_->streams[videoStreamIndex_]->codecpar->bit_rate;
    }
#endif
    return 0;
}

QString PlaybackController::getAudioCodec() const {
#ifdef HAS_FFMPEG
    if (audioCodecCtx_ && audioCodecCtx_->codec) {
        return QString::fromLatin1(audioCodecCtx_->codec->name);
    }
#endif
    return QString();
}

int64_t PlaybackController::getAudioBitrate() const {
#ifdef HAS_FFMPEG
    if (formatCtx_ && audioStreamIndex_ >= 0) {
        return formatCtx_->streams[audioStreamIndex_]->codecpar->bit_rate;
    }
#endif
    return 0;
}

int PlaybackController::getAudioSampleRate() const {
#ifdef HAS_FFMPEG
    if (audioCodecCtx_) return audioCodecCtx_->sample_rate;
#endif
    return 0;
}

int PlaybackController::getAudioChannels() const {
#ifdef HAS_FFMPEG
    if (audioCodecCtx_) return audioCodecCtx_->ch_layout.nb_channels;
#endif
    return 0;
}

void PlaybackController::setVideoOutput(VideoOutput* videoOutput) {
    if (videoOutput_ == videoOutput) return;

    videoOutput_ = videoOutput;

    qInfo() << "[PlaybackController::setVideoOutput] VideoOutput set";
}

// ==================== 内部方法 ====================

void PlaybackController::startAllThread() {
    // 检查渲染线程是否已在运行
    bool videoRunning = videoRenderThread_ && videoRenderThread_->isRunning();
    bool audioRunning = audioRenderThread_ && audioRenderThread_->isRunning();
    if (videoRunning || audioRunning) return;
    
    // 恢复队列
    if (videoPacketQueue_) videoPacketQueue_->resume();
    if (audioPacketQueue_) audioPacketQueue_->resume();
    
    // 启动解复用线程
    if (demuxThread_ && formatCtx_ && !demuxThread_->isRunning()) {
        DemuxContext demuxCtx{};
        demuxCtx.formatCtx = formatCtx_.get();
        demuxCtx.videoStreamIndex = videoStreamIndex_;
        demuxCtx.audioStreamIndex = audioStreamIndex_;
        demuxCtx.videoPacketQueue = videoPacketQueue_.get();
        demuxCtx.audioPacketQueue = audioPacketQueue_.get();
        demuxCtx.playbackState = &state_;
        demuxCtx.strategy = isNetworkStream_
            ? DemuxStrategy::forNetworkStream() 
            : DemuxStrategy::forLocalFile();
        
        demuxThread_->start(demuxCtx);
    }
    
    // 启动视频解码线程
    if (videoDecodeThread_ && formatCtx_ && videoDecoder_ &&
        videoStreamIndex_ >= 0 && !videoDecodeThread_->isRunning()) {
        
        VideoDecodeContext videoCtx;
        videoCtx.packetQueue = videoPacketQueue_.get();
        videoCtx.frameQueue = videoFrameQueue_.get();
        videoCtx.decoder = videoDecoder_.get();
        videoCtx.playbackState = &state_;
        videoCtx.extractStreamInfo(formatCtx_.get(), videoStreamIndex_);
        
        videoDecodeThread_->start(videoCtx);
    }
    
    // 启动音频解码线程
    if (audioDecodeThread_ && formatCtx_ && audioDecoder_ && audioResampler_ &&
        audioStreamIndex_ >= 0 && !audioDecodeThread_->isRunning()) {
        
        AudioDecodeContext audioCtx;
        audioCtx.packetQueue = audioPacketQueue_.get();
        audioCtx.frameQueue = audioFrameQueue_.get();
        audioCtx.decoder = audioDecoder_.get();
        audioCtx.resampler = audioResampler_.get();
        audioCtx.playbackState = &state_;
        audioCtx.extractStreamInfo(formatCtx_.get(), audioStreamIndex_);
        
        audioDecodeThread_->start(audioCtx);
    }
    
    // 启动渲染线程
    RenderContext ctx = createRenderContext();
    if (!ctx.isValid()) {
        qWarning() << "[PlaybackController::startAllThread] Invalid render context, cannot start render threads";
        qWarning() << "[PlaybackController::startAllThread] videoStreamIndex:" << videoStreamIndex_
                   << ", audioStreamIndex:" << audioStreamIndex_
                   << ", videoFrameQueue:" << (videoFrameQueue_ ? "valid" : "invalid")
                   << ", audioFrameQueue:" << (audioFrameQueue_ ? "valid" : "invalid")
                   << ", videoOutput:" << (videoOutput_ ? "valid" : "invalid");
        return;
    }
    
    // 重置 EOF 状态和播放结束标志
    videoEofReceived_.store(false);
    audioEofReceived_.store(false);
    playbackFinishedEmitted_.store(false);
    
    // 启动视频渲染线程
    if (ctx.hasVideoStream() && videoRenderThread_) {
        videoRenderThread_->setSyncConfig(syncConfig_);
        if (!videoRenderThread_->start(ctx, syncConfig_, &videoEofReceived_, &audioEofReceived_)) {
            qWarning() << "[PlaybackController::startAllThread] Failed to start video render thread";
        } else {
            qInfo() << "[PlaybackController::startAllThread] Video render thread started";
        }
    }
    
    // 启动音频播放线程
    if (ctx.hasAudioStream() && ctx.audioOutput && audioRenderThread_) {
        if (!audioRenderThread_->start(ctx, &videoEofReceived_, &audioEofReceived_)) {
            qWarning() << "[PlaybackController::startAllThread] Failed to start audio render thread";
        } else {
            qInfo() << "[PlaybackController::startAllThread] Audio render thread started";
        }
    }
}

RenderContext PlaybackController::createRenderContext() {
    RenderContext ctx;
    
    ctx.videoFrameQueue = videoFrameQueue_.get();
    ctx.audioFrameQueue = audioFrameQueue_.get();
    ctx.audioOutput = audioOutput_.get();
    ctx.playbackState = &state_;
    ctx.videoStreamIndex = videoStreamIndex_;
    ctx.audioStreamIndex = audioStreamIndex_;
    ctx.frameRate = getFrameRate() > 0 ? getFrameRate() : 30.0;
    ctx.resourceMutex = &resourceMutex_;
    ctx.enableVideoFrameForwarding = (videoOutput_ != nullptr);
    
    // 如果 VideoOutput 无效，记录警告（但允许继续，因为可能是纯音频流）
    if (!videoOutput_ && videoStreamIndex_ >= 0) {
        qWarning() << "[PlaybackController::createRenderContext] Warning: VideoOutput is invalid while video stream exists,"
                   << "this may cause video frame queue buildup and no rendering";
    }
    
    qDebug() << "[PlaybackController::createRenderContext] Render context created"
             << ", videoStreamIndex:" << ctx.videoStreamIndex
             << ", audioStreamIndex:" << ctx.audioStreamIndex
             << ", videoOutput:" << (videoOutput_ ? "valid" : "invalid")
             << ", videoFrameQueue:" << (ctx.videoFrameQueue ? "valid" : "invalid");
    
    return ctx;
}

bool PlaybackController::initializeVideoDecoder() {
#ifdef HAS_FFMPEG
    if (videoStreamIndex_ < 0 || !formatCtx_) return false;
    
    AVStream* videoStream = formatCtx_->streams[videoStreamIndex_];
    AVCodecParameters* codecpar = videoStream->codecpar;

    const AVCodec* codec = avcodec_find_decoder(codecpar->codec_id);
    if (!codec) {
        qWarning() << "[PlaybackController::initializeVideoDecoder] Decoder not found, codec_id=" << codecpar->codec_id;
        return false;
    }else{
        qInfo() << "[PlaybackController::initializeVideoDecoder] Decoder found:" << codec->name;
    }
    
    videoCodecCtx_.reset(avcodec_alloc_context3(codec));
    if (!videoCodecCtx_) {
        qWarning() << "[PlaybackController::initializeVideoDecoder] Failed to allocate decoder context";
        return false;
    }
    
    // 复制参数到上下文（包括 extradata）
    if (avcodec_parameters_to_context(videoCodecCtx_.get(), codecpar) < 0) {
        qWarning() << "[PlaybackController::initializeVideoDecoder] Failed to copy parameters to decoder context";
        videoCodecCtx_.reset();
        return false;
    }

    // ==================== 调试开关：强制软解 ====================
    const QString forceSwDecodeEnv = qEnvironmentVariable("ADVANCEDPLAYER_FORCE_SW_DECODE").trimmed();
    const bool forceSoftwareDecode =
        (forceSwDecodeEnv == "1") ||
        (forceSwDecodeEnv.compare("true", Qt::CaseInsensitive) == 0) ||
        (forceSwDecodeEnv.compare("yes", Qt::CaseInsensitive) == 0) ||
        (forceSwDecodeEnv.compare("on", Qt::CaseInsensitive) == 0);

    // ==================== 根据分辨率动态配置解码器缓冲区 ====================
    int width = codecpar->width;
    int height = codecpar->height;
    // 通过 AVDictionary 设置解码器选项
    AVDictionary* codecOptions = nullptr;
    av_dict_set(&codecOptions, "thread_type", "+slice+frame", 0); // 帧级和切片级并行

    if (codecpar->codec_id == AV_CODEC_ID_HEVC) {
        // 允许跳过损坏的帧，而不是完全停止解码
        av_dict_set(&codecOptions, "skip_frame", "default", 0);
        av_dict_set(&codecOptions, "skip_idct", "default", 0);
    }
    av_dict_set(&codecOptions, "flags2", "fast", 0); // 允许非规范解码,加快速度

    // 启用错误恢复（允许解码器在遇到错误时继续）
    av_dict_set(&codecOptions, "err_detect", "aggressive", 0);

    if (forceSoftwareDecode) {
        // 调试模式：单线程软解，便于排查硬解/并发相关崩溃
        av_dict_set(&codecOptions, "threads", "1", 0);
        qWarning() << "[PlaybackController::initializeVideoDecoder] Debug mode enabled: force software decode + single thread";
    } else {
        av_dict_set_int(&codecOptions, "threads", 4, 0);
    }

    // av_dict_set(&codecOptions, "color_primaries", "bt2020", 0);
    // av_dict_set(&codecOptions, "color_trc", "smpte2084", 0);
    // av_dict_set(&codecOptions, "colorspace", "bt2020nc", 0);
    
    // 尝试硬件解码
    bool hardwareAccelEnabled = SettingsManager::instance().hardwareAccelerationEnabled() && !forceSoftwareDecode;
    
    if (hardwareAccelEnabled) {
        auto availableAccels = HardwareDecoder::detectAvailableAccelerators();
        // 硬件解码器额外分配的帧缓冲区数量
        int extraHwFrames = 4;
        if (width >= 3840 || height >= 2160) {
            extraHwFrames = 8;  // 4K其他编码: 8个额外帧
        } else if (width >= 1920 || height >= 1080) {
            extraHwFrames = 6;  // 2K其他编码: 6个额外帧
        }
        if (codecpar->codec_id == AV_CODEC_ID_HEVC) {
            extraHwFrames = 12;
        }
        av_dict_set_int(&codecOptions, "extra_hw_frames", extraHwFrames, 0);
        for (const auto& accelType : availableAccels) {
            videoDecoder_ = createDecoder<HardwareDecoder>(videoCodecCtx_.get(), accelType);
            if (initializeDecoder(videoDecoder_, "", &codecOptions)) {
                qInfo() << "[PlaybackController] Hardware decoder initialized, type:"
                        << QString::fromStdString(toString(accelType));
                // 如果找到 CUDA/NVDEC，直接使用，不再尝试其他硬件解码器
                if (accelType == HardwareAccelType::NVDEC || accelType == HardwareAccelType::CUDA) {
                    qInfo() << "[PlaybackController] CUDA/NVDEC found, skipping other hardware decoders";
                    av_dict_free(&codecOptions);
                    return true;
                }
                av_dict_free(&codecOptions);
                return true;
            }
            videoDecoder_.reset();
        }
    }
    
    // 回退到软件解码
    av_dict_set_int(&codecOptions, "extra_hw_frames", 0, 0);
    videoDecoder_ = createDecoder<SoftwareDecoder>(videoCodecCtx_.get());
    if (initializeDecoder(videoDecoder_, "", &codecOptions)) {
        qInfo() << "[PlaybackController] Software decoder initialized";
        av_dict_free(&codecOptions);
        return true;
    }
    
    av_dict_free(&codecOptions);
    videoCodecCtx_.reset();
    return false;
#else
    return false;
#endif
}

bool PlaybackController::initializeAudioDecoder() {
#ifdef HAS_FFMPEG
    if (audioStreamIndex_ < 0 || !formatCtx_) return false;
    
    AVStream* audioStream = formatCtx_->streams[audioStreamIndex_];
    AVCodecParameters* codecpar = audioStream->codecpar;
    
    const AVCodec* codec = avcodec_find_decoder(codecpar->codec_id);
    if (!codec) return false;
    
    audioCodecCtx_.reset(avcodec_alloc_context3(codec));
    if (!audioCodecCtx_) return false;
    
    if (avcodec_parameters_to_context(audioCodecCtx_.get(), codecpar) < 0) {
        audioCodecCtx_.reset();
        return false;
    }
    
    AVDictionary* options = nullptr;
    av_dict_set(&options, "threads", "auto", 0);

    audioDecoder_ = createDecoder<SoftwareDecoder>(audioCodecCtx_.get());
    if (!initializeDecoder(audioDecoder_, "", &options)) {
        audioCodecCtx_.reset();
        return false;
    }

    
#ifdef HAS_SDL3
    int sampleRate = codecpar->sample_rate;
    int channels = codecpar->ch_layout.nb_channels;
    
    // AudioOutput 复用
    if (audioOutput_ && audioOutput_->isInitialized()) {
        if (!audioOutput_->reinitialize(sampleRate, channels, SDL_AUDIO_S16)) {
            audioOutput_.reset();
            audioOutput_ = std::make_unique<AudioOutput>();
            if (!audioOutput_->initialize(sampleRate, channels, SDL_AUDIO_S16)) {
                return false;
            }
        }
    } else {
        audioOutput_ = std::make_unique<AudioOutput>();
        if (!audioOutput_->initialize(sampleRate, channels, SDL_AUDIO_S16)) {
            return false;
        }
    }
    
    // 初始化重采样器
    int actualSampleRate = audioOutput_->getSampleRate();
    int actualChannels = audioOutput_->getChannels();
    
    //复用resampler
    if (audioResampler_ && audioResampler_->isInitialized()) {
        if (!audioResampler_->reconfigure(
                sampleRate, channels, codecpar->format,
                actualSampleRate, actualChannels, AV_SAMPLE_FMT_S16)) {
            audioResampler_.reset();
            audioResampler_ = std::make_unique<AudioResampler>(
                sampleRate, channels, codecpar->format,
                actualSampleRate, actualChannels, AV_SAMPLE_FMT_S16
            );
        }
    } else {
        audioResampler_ = std::make_unique<AudioResampler>(
            sampleRate, channels, codecpar->format,
            actualSampleRate, actualChannels, AV_SAMPLE_FMT_S16
        );
    }
#endif
    
    av_dict_free(&options);
    qInfo() << "[PlaybackController] Audio decoder initialized";
    return true;
#else
    return false;
#endif
}

void PlaybackController::requestStopAllWorkerThreads() {
    // Demux -> Decode -> Render（先停生产端，再停消费端）
    if (demuxThread_) demuxThread_->requestStop();
    if (videoDecodeThread_) videoDecodeThread_->requestStop();
    if (audioDecodeThread_) audioDecodeThread_->requestStop();
    if (videoRenderThread_) videoRenderThread_->requestStop();
    if (audioRenderThread_) audioRenderThread_->requestStop();
}

void PlaybackController::joinAllWorkerThreads() {
    // 回收顺序与 requestStop 保持一致
    if (demuxThread_) demuxThread_->joinAndReset();
    if (videoDecodeThread_) videoDecodeThread_->joinAndReset();
    if (audioDecodeThread_) audioDecodeThread_->joinAndReset();
    if (videoRenderThread_) videoRenderThread_->joinAndReset();
    if (audioRenderThread_) audioRenderThread_->joinAndReset();
}

void PlaybackController::continueCloseMediaAsync() {
    if (!closeAsyncInProgress_) {
        return;
    }

    switch (closeAsyncJoinStep_) {
        case 0:
            if (demuxThread_) demuxThread_->joinAndReset();
            break;
        case 1:
            if (videoDecodeThread_) videoDecodeThread_->joinAndReset();
            break;
        case 2:
            if (audioDecodeThread_) audioDecodeThread_->joinAndReset();
            break;
        case 3:
            if (videoRenderThread_) videoRenderThread_->joinAndReset();
            break;
        case 4:
            if (audioRenderThread_) audioRenderThread_->joinAndReset();
            break;
        default:
            qInfo() << "[PlaybackController::closeMediaAsync] Stage 3/6: flush codec buffers";
#ifdef HAS_FFMPEG
            if (videoCodecCtx_) {
                avcodec_flush_buffers(videoCodecCtx_.get());
            }
            if (audioCodecCtx_) {
                avcodec_flush_buffers(audioCodecCtx_.get());
            }
#endif

            qInfo() << "[PlaybackController::closeMediaAsync] Stage 4/6: pause and clear audio output";
            {
                std::lock_guard<std::mutex> lock(resourceMutex_);
                if (audioOutput_) {
                    audioOutput_->pause();
                    audioOutput_->clear();
                }
            }

            qInfo() << "[PlaybackController::closeMediaAsync] Stage 5/6: clear queues";
            if (videoPacketQueue_) videoPacketQueue_->clear();
            if (audioPacketQueue_) audioPacketQueue_->clear();
            if (videoFrameQueue_) videoFrameQueue_->clear();
            if (audioFrameQueue_) audioFrameQueue_->clear();

            qInfo() << "[PlaybackController::closeMediaAsync] Stage 6/6: cleanup FFmpeg resources";
            cleanupFFmpegResources();

            duration_ = 0;
            emit durationChanged(0);
            currentFilePath_.clear();
            ioInterruptRequested_.store(false, std::memory_order_release);
            isClosing_.store(false, std::memory_order_release);
            closeAsyncInProgress_ = false;
            qInfo() << "[PlaybackController::closeMediaAsync] Closing media asynchronously: done";
            emit closeMediaCompleted();
            return;
    }

    closeAsyncJoinStep_++;
    QTimer::singleShot(0, this, &PlaybackController::continueCloseMediaAsync);
}

void PlaybackController::ensureFrameReadyConnection() {
    if (frameReadyConnection_ || !videoRenderThread_) {
        return;
    }

    frameReadyConnection_ = connect(videoRenderThread_.get(), &VideoRenderThread::frameReady,
            this, [this](AVFrame* frame) {
                if (!frame) {
                    qWarning() << "[PlaybackController] frameReady received null frame";
                    return;
                }

                // 关闭流程中不再向 UI 投递视频帧，避免析构窗口跨线程访问
                if (isClosing_.load(std::memory_order_acquire)) {
                    av_frame_free(&frame);
                    return;
                }

                QPointer<VideoOutput> videoOutput;
                {
                    std::lock_guard<std::mutex> lock(resourceMutex_);
                    videoOutput = videoOutput_;
                }

                if (videoOutput) {
                    videoOutput->updateFrame(frame);
                } else {
                    qWarning() << "[PlaybackController] frameReady dropped: videoOutput is null";
                }

                av_frame_free(&frame);
            }, Qt::QueuedConnection);
}

void PlaybackController::cleanupFFmpegResources() {
#ifdef HAS_FFMPEG
    // ==================== 正确的资源释放顺序 ====================
    // 顺序很重要：先销毁解码器，再释放 codec context，最后关闭 format context
    // 这样可以确保所有引用计数正确递减，避免资源泄漏
    
    qInfo() << "[PlaybackController::cleanupFFmpegResources] Start cleaning FFmpeg resources";
    
    // 1. 销毁解码器（解码器析构时会清理内部资源，包括硬件设备上下文）
    //    对于硬件解码器，析构函数会先清理 codecCtx_->hw_device_ctx 引用，再释放 hwDeviceCtx_
    if (videoDecoder_) {
        qDebug() << "[PlaybackController::cleanupFFmpegResources] Destroying video decoder";
        videoDecoder_.reset();
        qDebug() << "[PlaybackController::cleanupFFmpegResources] Video decoder destroyed";
    }
    if (audioDecoder_) {
        qDebug() << "[PlaybackController::cleanupFFmpegResources] Destroying audio decoder";
        audioDecoder_.reset();
        qDebug() << "[PlaybackController::cleanupFFmpegResources] Audio decoder destroyed";
    }
    
    // 2. 释放 codec context（此时解码器已完全清理，不会访问参考帧）
    //    avcodec_free_context 会自动释放 codecCtx_->hw_device_ctx（如果还存在）
    if (videoCodecCtx_) {
        qDebug() << "[PlaybackController::cleanupFFmpegResources] Releasing video codec context";
        videoCodecCtx_.reset();
        qDebug() << "[PlaybackController::cleanupFFmpegResources] Video codec context released";
    }
    if (audioCodecCtx_) {
        qDebug() << "[PlaybackController::cleanupFFmpegResources] Releasing audio codec context";
        audioCodecCtx_.reset();
        qDebug() << "[PlaybackController::cleanupFFmpegResources] Audio codec context released";
    }
    
    // 3. 最后关闭格式上下文
    if (formatCtx_) {
        qDebug() << "[PlaybackController::cleanupFFmpegResources] Closing format context";
        formatCtx_.reset();
        qDebug() << "[PlaybackController::cleanupFFmpegResources] Format context closed";
    }
    
    videoStreamIndex_ = -1;
    audioStreamIndex_ = -1;
    
    qInfo() << "[PlaybackController::cleanupFFmpegResources] FFmpeg resource cleanup completed";
#endif
}

void PlaybackController::clearBuffers() {
    if (videoPacketQueue_) videoPacketQueue_->clear();
    if (audioPacketQueue_) audioPacketQueue_->clear();
    if (audioFrameQueue_) audioFrameQueue_->clear();
    if (videoFrameQueue_) videoFrameQueue_->clear();
    
    {
        std::lock_guard<std::mutex> lock(resourceMutex_);
        if (audioOutput_) audioOutput_->clear();
    }
}

} // namespace AdvancedPlayer
