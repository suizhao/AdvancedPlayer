/**
 * @file VideoDecodeThread.cpp
 * @brief 独立视频解码线程实现
 */

#include "VideoDecodeThread.h"
#include "LockFreePacketQueue.h"
#include "LockFreeVideoFrameQueue.h"
#include "RenderContext.h"
#include "PlaybackController.h"  // PlaybackState
#include "src/decoder/DecoderInterface.h"
#include <QDebug>
#include <chrono>

#ifdef HAS_FFMPEG
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/pixdesc.h>
}
#endif

namespace AdvancedPlayer {

// ==================== 常量定义 ====================
namespace {
    // 队列满时的最大重试总时间（毫秒）
    constexpr int MAX_PUSH_TIMEOUT_MS = 200;
    // 指数退避：初始间隔（微秒，0.1ms）
    constexpr int INITIAL_BACKOFF_US = 100;
    // 指数退避：最大间隔（毫秒，16ms）
    constexpr int MAX_BACKOFF_MS = 16;
    // 指数退避：退避因子（每次翻倍）
    constexpr double BACKOFF_MULTIPLIER = 2.0;
    // 暂停时的休眠间隔（毫秒）
    constexpr int PAUSE_SLEEP_MS = 10;
    // 队列空时的休眠间隔（毫秒）
    // 增加此值可以降低解码速度，给解码器更多时间保留参考帧
    constexpr int EMPTY_QUEUE_SLEEP_MS = 5;  // 从1ms增加到5ms
    // flush 时的最大帧数
    constexpr int MAX_FLUSH_FRAMES = 100;
    // 成功解码一帧后的延迟（毫秒）
    // 添加此延迟可以降低解码速度，避免参考帧被过早释放
    constexpr int DECODE_SUCCESS_DELAY_MS = 2;  // 成功解码后延迟2ms
    // 解码失败或需要更多数据时的延迟（毫秒）
    constexpr int DECODE_FAIL_DELAY_MS = 1;  // 解码失败后延迟1ms
}

// ==================== 构造与析构 ====================

VideoDecodeThread::VideoDecodeThread(QObject* parent)
    : QObject(parent) {
    qInfo() << "[VideoDecodeThread] Video decode thread created";
}

VideoDecodeThread::~VideoDecodeThread() {
    stop();
    qInfo() << "[VideoDecodeThread] Video decode thread destroyed";
}

// ==================== 线程控制 ====================

bool VideoDecodeThread::start(const VideoDecodeContext& ctx) {
    if (thread_) {
        qWarning() << "[VideoDecodeThread::start] Thread is already running";
        return false;
    }
    
    if (!ctx.isValid()) {
        qWarning() << "[VideoDecodeThread::start] Invalid context";
        emit errorOccurred("Invalid video decode context");
        return false;
    }
    
    // 复制上下文
    ctx_ = ctx;
    
    // 启动线程
    thread_ = std::make_unique<std::jthread>([this](std::stop_token stoken) {
        decodeLoop(stoken);
    });
    
    qInfo() << "[VideoDecodeThread::start] Video decode thread started"
            << ", streamIndex=" << ctx_.streamIndex
            << ", timeBase=" << ctx_.timeBase.num << "/" << ctx_.timeBase.den;
    
    return true;
}

void VideoDecodeThread::requestStop() {
    if (!thread_) {
        return;
    }
    qInfo() << "[VideoDecodeThread::requestStop] Request stop video decode thread";
    
    // ==================== 快速停止优化 ====================
    // 为了快速关闭，需要先中止队列，让线程能够快速退出等待循环
    // 这样可以避免线程卡在 pushFrameWithRetry 的重试循环中
    
    if (ctx_.frameQueue) {
        ctx_.frameQueue->abort();
    }
    if (ctx_.packetQueue) {
        ctx_.packetQueue->abort();
    }
    
    // 请求停止并等待（jthread 析构时自动 join）
    thread_->request_stop();
}

void VideoDecodeThread::joinAndReset() {
    if (!thread_) {
        return;
    }

    qInfo() << "[VideoDecodeThread::joinAndReset] Joining video decode thread";
    thread_.reset();
    
    qInfo() << "[VideoDecodeThread::joinAndReset] Video decode thread stopped";
}

void VideoDecodeThread::stop() {
    requestStop();
    joinAndReset();
}

bool VideoDecodeThread::isRunning() const {
    return thread_ != nullptr && thread_->joinable();
}

void VideoDecodeThread::flush() {
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
        qWarning() << "[VideoDecodeThread::flush] Reached max flush count:" << MAX_FLUSH_FRAMES
                   << ", continue releasing remaining frames";
        while ((frame = ctx_.decoder->flush()) != nullptr) {
            av_frame_free(&frame);
        }
    }
    
    qDebug() << "[VideoDecodeThread::flush] Flush completed, processed" << flushCount << "frames";
#endif
}

// ==================== 解码循环 ====================

void VideoDecodeThread::decodeLoop(std::stop_token stoken) {
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

bool VideoDecodeThread::decodePacket(std::stop_token stoken) {
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
    // 在调用解码器前再次检查停止请求，避免在资源清理时使用硬件解码器
    if (stoken.stop_requested()) {
        av_packet_free(&packet);
        return false;
    }
    
    AVFrame* frame = ctx_.decoder->decode(packet);
    
    // 释放 packet
    av_packet_free(&packet);
    
    if (!frame) {
        // 解码失败或需要更多数据，稍微延迟一下再继续
        // 这样可以降低解码速度，给解码器更多时间处理参考帧
        std::this_thread::sleep_for(std::chrono::milliseconds(DECODE_FAIL_DELAY_MS));
        return false;  // 解码失败或需要更多数据
    }
    
    // 处理帧前检查停止请求
    if (stoken.stop_requested()) {
        av_frame_free(&frame);
        return false;
    }
    
    processFrame(frame);
    
    // 成功解码一帧后，稍微延迟一下再处理下一个数据包
    // 这样可以降低解码速度，避免参考帧被过早释放
    // 对于HEVC等需要大量参考帧的编码，这个延迟特别重要
    std::this_thread::sleep_for(std::chrono::milliseconds(DECODE_SUCCESS_DELAY_MS));
    
    return false;  // 正常解码
#else
    Q_UNUSED(stoken);
    return false;
#endif
}

void VideoDecodeThread::processFrame(AVFrame* frame) {
#ifdef HAS_FFMPEG
    if (!frame || !ctx_.frameQueue) {
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
    
    // ===== PTS 处理 =====
    if (frame->pts == AV_NOPTS_VALUE) {
        if (frame->pkt_dts != AV_NOPTS_VALUE) {
            frame->pts = frame->pkt_dts;
        } else {
            // worker 线程静默丢弃异常帧，避免退出阶段触发 Qt 日志路径
            av_frame_free(&frame);
            return;
        }
    }
    
    // ===== 计算时间戳 =====
    double pts = frame->pts * av_q2d(ctx_.timeBase);
    
    // ===== 帧有效性验证 =====
    if (frame->format < 0 || frame->format == AV_PIX_FMT_NONE) {
        // worker 线程静默丢弃异常帧，避免退出阶段触发 Qt 日志路径
        av_frame_free(&frame);
        return;
    }
    
    if (frame->width <= 0 || frame->height <= 0) {
        // worker 线程静默丢弃异常帧，避免退出阶段触发 Qt 日志路径
        av_frame_free(&frame);
        return;
    }
    
    // ===== 推入队列 =====
    // push 成功后，frame 所有权转移给队列；失败则由当前函数释放
    if (!pushFrameWithRetry(frame, pts)) {
        av_frame_free(&frame);
    }
#else
    if (frame) {
        av_frame_free(&frame);
    }
#endif
}

bool VideoDecodeThread::pushFrameWithRetry(AVFrame* frame, double pts) {
    if (!ctx_.frameQueue) {
        return false;
    }
    
    // ===== 指数退避重试策略 =====
    // 初始间隔：0.1ms（快速响应短暂拥堵）
    // 最大间隔：16ms（降低长期拥堵时的CPU占用）
    // 退避因子：2.0（每次翻倍）
    
    auto startTime = std::chrono::steady_clock::now();
    int backoffUs = INITIAL_BACKOFF_US;  // 从 0.1ms 开始
    int retryCount = 0;
    
    while (true) {
        // 尝试推入
        if (ctx_.frameQueue->push(frame, pts)) {
            return true;  // 成功
        }
        
        // 检查是否被中止
        if (ctx_.frameQueue->isAborted()) {
            return false;
        }
        
        // 检查总超时时间
        auto elapsed = std::chrono::steady_clock::now() - startTime;
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
        if (elapsedMs >= MAX_PUSH_TIMEOUT_MS) {
            return false;
        }
        
        // 指数退避休眠（使用可中断的休眠，避免长时间阻塞）
        if (backoffUs < MAX_BACKOFF_MS * 1000) {
            // 使用微秒精度休眠（初始阶段更精确）
            std::this_thread::sleep_for(std::chrono::microseconds(backoffUs));
            // 指数增长，但不超过最大值
            backoffUs = static_cast<int>(backoffUs * BACKOFF_MULTIPLIER);
            if (backoffUs > MAX_BACKOFF_MS * 1000) {
                backoffUs = MAX_BACKOFF_MS * 1000;
            }
        } else {
            // 达到最大间隔后，固定使用最大间隔
            std::this_thread::sleep_for(std::chrono::milliseconds(MAX_BACKOFF_MS));
        }
        
        ++retryCount;
    }
}

} // namespace AdvancedPlayer
