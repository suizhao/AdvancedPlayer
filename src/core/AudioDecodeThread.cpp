/**
 * @file AudioDecodeThread.cpp
 * @brief 独立音频解码线程实现
 */

#include "AudioDecodeThread.h"
#include "LockFreePacketQueue.h"
#include "LockFreeAudioFrameQueue.h"
#include "RenderContext.h"
#include "PlaybackController.h"  // PlaybackState
#include "src/decoder/DecoderInterface.h"
#include "src/audio/AudioResampler.h"
#include <QDebug>
#include <chrono>

#ifdef HAS_FFMPEG
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/mem.h>
}
#endif

namespace AdvancedPlayer {

// ==================== 常量定义 ====================
namespace {
    // 队列满时的最大重试次数（根据背压控制动态调整）
    constexpr int MAX_PUSH_RETRIES = 500;
    // 基础重试间隔（毫秒）
    constexpr int BASE_RETRY_INTERVAL_MS = 1;
    // 背压控制阈值（队列使用率）
    constexpr double BACKPRESSURE_LOW_THRESHOLD = 0.5;   // 50% - 轻度背压
    constexpr double BACKPRESSURE_MEDIUM_THRESHOLD = 0.8; // 80% - 中度背压
    constexpr double BACKPRESSURE_HIGH_THRESHOLD = 0.95;  // 95% - 重度背压
    // 不同背压级别的等待时间（毫秒）
    constexpr int BACKPRESSURE_LOW_WAIT_MS = 2;      // 轻度：2ms
    constexpr int BACKPRESSURE_MEDIUM_WAIT_MS = 10;  // 中度：10ms
    constexpr int BACKPRESSURE_HIGH_WAIT_MS = 20;    // 重度：20ms
    // 暂停时的休眠间隔（毫秒）
    constexpr int PAUSE_SLEEP_MS = 10;
    // 队列空时的休眠间隔（毫秒）
    constexpr int EMPTY_QUEUE_SLEEP_MS = 1;
    // flush 时的最大帧数
    constexpr int MAX_FLUSH_FRAMES = 100;
}

// ==================== 构造与析构 ====================

AudioDecodeThread::AudioDecodeThread(QObject* parent)
    : QObject(parent) {
    qInfo() << "[AudioDecodeThread] Audio decode thread created";
}

AudioDecodeThread::~AudioDecodeThread() {
    stop();
    qInfo() << "[AudioDecodeThread] Audio decode thread destroyed";
}

// ==================== 线程控制 ====================

bool AudioDecodeThread::start(const AudioDecodeContext& ctx) {
    if (thread_) {
        qWarning() << "[AudioDecodeThread::start] Thread is already running";
        return false;
    }
    
    if (!ctx.isValid()) {
        qWarning() << "[AudioDecodeThread::start] Invalid context";
        emit errorOccurred("Invalid audio decode context");
        return false;
    }
    
    // 复制上下文
    ctx_ = ctx;
    
    // 启动线程
    thread_ = std::make_unique<std::jthread>([this](std::stop_token stoken) {
        decodeLoop(stoken);
    });
    
    qInfo() << "[AudioDecodeThread::start] Audio decode thread started"
            << ", streamIndex=" << ctx_.streamIndex
            << ", timeBase=" << ctx_.timeBase.num << "/" << ctx_.timeBase.den;
    
    return true;
}

void AudioDecodeThread::requestStop() {
    if (!thread_) {
        return;
    }
    qInfo() << "[AudioDecodeThread::requestStop] Request stop audio decode thread";
    
    // ==================== 快速停止优化 ====================
    // 为了快速关闭，需要先中止队列，让线程能够快速退出等待循环
    // 这样可以避免线程卡在 pushDataWithRetry 的重试循环中
    
    if (ctx_.frameQueue) {
        ctx_.frameQueue->abort();
    }
    if (ctx_.packetQueue) {
        ctx_.packetQueue->abort();
    }
    
    // 请求停止并等待（jthread 析构时自动 join）
    thread_->request_stop();
}

void AudioDecodeThread::joinAndReset() {
    if (!thread_) {
        return;
    }

    qInfo() << "[AudioDecodeThread::joinAndReset] Joining audio decode thread";
    thread_.reset();
    
    qInfo() << "[AudioDecodeThread::joinAndReset] Audio decode thread stopped";
}

void AudioDecodeThread::stop() {
    requestStop();
    joinAndReset();
}

bool AudioDecodeThread::isRunning() const {
    return thread_ != nullptr && thread_->joinable();
}

void AudioDecodeThread::flush() {
#ifdef HAS_FFMPEG
    if (!ctx_.decoder || !ctx_.frameQueue) {
        return;
    }
    
    int flushCount = 0;
    AVFrame* frame = nullptr;
    
    while ((frame = ctx_.decoder->flush()) != nullptr && flushCount < MAX_FLUSH_FRAMES) {
        processFrame(frame);
        flushCount++;
    }
    
    // 如果达到限制，继续释放剩余的 flush 帧，避免资源泄漏
    if (flushCount >= MAX_FLUSH_FRAMES) {
        qWarning() << "[AudioDecodeThread::flush] Reached max flush count:" << MAX_FLUSH_FRAMES
                   << ", continue releasing remaining frames";
        while ((frame = ctx_.decoder->flush()) != nullptr) {
            av_frame_free(&frame);
        }
    }
    
    qDebug() << "[AudioDecodeThread::flush] Flush completed, processed" << flushCount << "frames";
#endif
}

// ==================== 解码循环 ====================

void AudioDecodeThread::decodeLoop(std::stop_token stoken) {
    bool eofHandled = false;
    
    while (!stoken.stop_requested()) {
        if (eofHandled) {
            // EOF 后不立即自然退出，统一由外部 stop() 回收线程
            std::this_thread::sleep_for(std::chrono::milliseconds(PAUSE_SLEEP_MS));
            continue;
        }

        // ===== 检查队列是否被中止，如果被中止则立即退出 =====
        if (ctx_.packetQueue && ctx_.packetQueue->isAborted()) {
            break;
        }
        
        // ===== 暂停检查 =====
        if (!ctx_.playbackState || ctx_.playbackState->load() != PlaybackState::Playing) {
            std::this_thread::sleep_for(std::chrono::milliseconds(PAUSE_SLEEP_MS));
            continue;
        }
        
        // ===== 解码处理 =====
        if (ctx_.packetQueue && ctx_.decoder) {
            bool eof = decodePacket(stoken);
            if (eof) {
                eofHandled = true;
                continue;
            }
        }
        
        // ===== 队列空时短暂休眠 =====
        if (ctx_.packetQueue && ctx_.packetQueue->empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(EMPTY_QUEUE_SLEEP_MS));
        }
    }
    
    // 退出日志由 stop()（主线程）统一输出，避免 worker 退出路径调用 Qt 日志
}

bool AudioDecodeThread::decodePacket(std::stop_token stoken) {
#ifdef HAS_FFMPEG
    if (!ctx_.packetQueue || !ctx_.decoder || !ctx_.frameQueue) {
        return false;
    }
    
    // 在开始解码前检查停止请求，避免在资源清理时使用解码器
    if (stoken.stop_requested()) {
        return false;
    }
    
    // 检查队列是否被中止
    if (ctx_.packetQueue->isAborted()) {
        return false;
    }
    
    AVPacket* packet = nullptr;
    if (!ctx_.packetQueue->pop(&packet)) {
        return false;  // 队列空
    }
    
    // 再次检查停止请求（在获取 packet 后）
    if (stoken.stop_requested()) {
        av_packet_free(&packet);
        return false;
    }
    
    // ===== EOF 空包检测 =====
    if (LockFreePacketQueue::isEofMarker(packet)) {
        // 刷新解码器缓冲区
        AVFrame* frame = nullptr;
        while ((frame = ctx_.decoder->flush()) != nullptr) {
            if (stoken.stop_requested()) {
                // 停止请求时，释放当前帧并继续释放剩余的 flush 帧
                av_frame_free(&frame);
                // 继续释放所有剩余的 flush 帧，避免资源泄漏
                while ((frame = ctx_.decoder->flush()) != nullptr) {
                    av_frame_free(&frame);
                }
                break;
            }
            processFrame(frame);
        }
        
        // 推入 EOF 空帧（带重试机制，确保 EOF 标记不会丢失）
        if (!stoken.stop_requested() && ctx_.frameQueue) {
            // 重试推入 EOF 标记，最多重试 10 次，每次等待 10ms
            const int maxRetries = 10;
            const int retryIntervalMs = 10;
            bool eofPushed = false;
            
            for (int retry = 0; retry < maxRetries && !stoken.stop_requested(); ++retry) {
                if (ctx_.frameQueue->pushEofMarker()) {
                    eofPushed = true;
                    break;
                }
                
                if (retry < maxRetries - 1) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(retryIntervalMs));
                }
            }
            
            if (!eofPushed) {
                // EOF 标记推送失败时保持静默，避免 worker 线程日志开销
            }
        }
        
        return true;  // 收到 EOF
    }
    
    // ===== 正常解码 =====
    // 在调用解码器前再次检查停止请求，避免在资源清理时使用解码器
    if (stoken.stop_requested()) {
        av_packet_free(&packet);
        return false;
    }
    
    AVFrame* frame = ctx_.decoder->decode(packet);
    
    // 释放 packet
    av_packet_free(&packet);
    
    if (!frame) {
        return false;  // 解码失败或需要更多数据
    }
    
    // 处理帧前检查停止请求
    if (stoken.stop_requested()) {
        av_frame_free(&frame);
        return false;
    }
    
    processFrame(frame);
    return false;  // 正常解码
#else
    Q_UNUSED(stoken);
    return false;
#endif
}

void AudioDecodeThread::processFrame(AVFrame* frame) {
#ifdef HAS_FFMPEG
    if (!frame || !ctx_.frameQueue || !ctx_.resampler) {
        if (frame) {
            av_frame_free(&frame);
        }
        return;
    }
    
    // ===== 流索引验证 =====
    if (ctx_.streamIndex < 0 || 
        static_cast<unsigned int>(ctx_.streamIndex) >= ctx_.nbStreams) {
        // worker 线程静默丢弃异常帧，避免退出阶段触发 Qt 日志路径
        av_frame_free(&frame);
        return;
    }
    
    // ===== 计算时间戳 =====
    double pts = frame->pts * av_q2d(ctx_.timeBase);

    // qInfo() << "111111111111[AudioResampler::resample]   输入: " << frame->nb_samples <<
    //     "采样 "<< av_get_sample_fmt_name(static_cast<AVSampleFormat>(frame->format));
    
    // ===== 重采样 =====
    uint8_t* outputBuffer[1] = {nullptr};
    int outputSize = ctx_.resampler->resample(frame, outputBuffer);
    
    // 释放原始帧
    av_frame_free(&frame);
    
    // 检查重采样结果
    if (outputSize <= 0 || !outputBuffer[0]) {
        // worker 线程静默丢弃异常帧，避免退出阶段触发 Qt 日志路径
        if (outputBuffer[0]) {
            av_free(outputBuffer[0]);
        }
        return;
    }
    
    // ===== 推入队列 =====
    if (!pushDataWithRetry(outputBuffer[0], outputSize, pts)) {
        av_free(outputBuffer[0]);
    }
    // 成功推入后，所有权转移给队列，不再释放
#else
    if (frame) {
        av_frame_free(&frame);
    }
#endif
}

bool AudioDecodeThread::pushDataWithRetry(uint8_t* data, int size, double pts) {
    if (!ctx_.frameQueue) {
        return false;
    }
    
    int retryCount = 0;
    
    while (retryCount < MAX_PUSH_RETRIES) {
        // 尝试推入
        if (ctx_.frameQueue->push(data, size, pts)) {
            return true;  // 成功
        }
        
        // 检查是否被中止
        if (ctx_.frameQueue->isAborted()) {
            return false;
        }
        
        // ==================== 背压控制 ====================
        // 根据队列使用率动态调整等待时间，避免内存溢出
        size_t queueSize = ctx_.frameQueue->size();
        size_t queueCapacity = ctx_.frameQueue->capacity();
        
        if (queueCapacity > 0) {
            double usageRatio = static_cast<double>(queueSize) / static_cast<double>(queueCapacity);
            int waitTimeMs = BASE_RETRY_INTERVAL_MS;
            
            // 根据使用率选择等待时间
            if (usageRatio >= BACKPRESSURE_HIGH_THRESHOLD) {
                // 重度背压：队列接近满载，较长等待
                waitTimeMs = BACKPRESSURE_HIGH_WAIT_MS;
            } else if (usageRatio >= BACKPRESSURE_MEDIUM_THRESHOLD) {
                // 中度背压：队列使用率较高，中等等待
                waitTimeMs = BACKPRESSURE_MEDIUM_WAIT_MS;
            } else if (usageRatio >= BACKPRESSURE_LOW_THRESHOLD) {
                // 轻度背压：队列使用率中等，短暂等待
                waitTimeMs = BACKPRESSURE_LOW_WAIT_MS;
            }
            // usageRatio < BACKPRESSURE_LOW_THRESHOLD: 使用基础等待时间
            
            std::this_thread::sleep_for(std::chrono::milliseconds(waitTimeMs));
        } else {
            // 容量为0（异常情况），使用基础等待时间
            std::this_thread::sleep_for(std::chrono::milliseconds(BASE_RETRY_INTERVAL_MS));
        }
        
        ++retryCount;
    }
    
    // 达到最大重试次数，记录警告
    size_t queueSize = ctx_.frameQueue->size();
    size_t queueCapacity = ctx_.frameQueue->capacity();
    double usageRatio = queueCapacity > 0 
        ? static_cast<double>(queueSize) / static_cast<double>(queueCapacity) 
        : 0.0;
    
    qWarning() << "[AudioDecodeThread::pushDataWithRetry] Timed out, queue may be full"
               << "(usage:" << (usageRatio * 100.0) << "%,"
               << "size:" << queueSize << "/" << queueCapacity << ")";
    return false;
}

} // namespace AdvancedPlayer
