/**
 * @file DemuxThread.cpp
 * @brief 解复用线程实现（重新设计版本）
 */

 #include "DemuxThread.h"
 #include "LockFreePacketQueue.h"
 #include "RenderContext.h"
 #include "PlaybackController.h"  // PlaybackState
 #include <QDebug>
 #include <chrono>
 
 #ifdef HAS_FFMPEG
 extern "C" {
 #include <libavformat/avformat.h>
 #include <libavcodec/avcodec.h>
 }
 #endif
 
 namespace AdvancedPlayer {
 
 // ==================== 构造与析构 ====================
 
 DemuxThread::DemuxThread(QObject* parent)
     : QObject(parent)
 {
    qInfo() << "[DemuxThread] Created";
 }
 
 DemuxThread::~DemuxThread()
 {
     stop();
    qInfo() << "[DemuxThread] Destroyed";
 }
 
 bool DemuxThread::start(const DemuxContext& ctx)
 {
     // 检查当前状态
     DemuxState expected = DemuxState::Idle;
     // 只从Idle和Stopped状态开始
     // 逻辑：如果 state_ == expected(Idle)，则更新为 Running，返回true；
     //       否则，expected 会被更新为 state_ 的实际值，返回false
     if (!state_.compare_exchange_strong(expected, DemuxState::Running)) {
         if (expected == DemuxState::Stopped) {
             state_.store(DemuxState::Running);
         } else {
            qWarning() << "[DemuxThread::start] Failed to start, current state:"
                        << getDemuxStateName(expected);
             return false;
         }
     }
     
     // 验证上下文
     if (!ctx.isValid()) {
        qCritical() << "[DemuxThread::start] Invalid demux context";
         state_.store(DemuxState::Idle);
        emit errorOccurred("Invalid demux context");
         return false;
     }
     
     // 复制上下文
     ctx_ = ctx;
     
     // 启动线程
     thread_ = std::make_unique<std::jthread>([this](std::stop_token stoken) {
         demuxLoop(stoken);
     });
 
    qInfo() << "[DemuxThread::start] Demux thread started"
            << "| videoStreamIndex:" << ctx_.videoStreamIndex
            << "| audioStreamIndex:" << ctx_.audioStreamIndex
            << "| isNetworkStream:" << ctx_.strategy.isNetworkStream;
     
     return true;
 }
 
void DemuxThread::requestStop()
 {
     if (!thread_) {
         return;
     }
   qInfo() << "[DemuxThread::requestStop] Request stop demux thread";
     
     // 设置停止状态
     state_.store(DemuxState::Stopping);
     
     // 中断队列，唤醒可能的等待
     if (ctx_.videoPacketQueue) {
         ctx_.videoPacketQueue->abort();
     }
     if (ctx_.audioPacketQueue) {
         ctx_.audioPacketQueue->abort();
     }
     
     // 唤醒条件变量
     {
         std::lock_guard<std::mutex> lock(mutex_);
         cv_.notify_all();
     }
     
     // 请求停止（通过 stop_token）
     thread_->request_stop();
}

void DemuxThread::joinAndReset()
{
    if (!thread_) {
        return;
    }

   qInfo() << "[DemuxThread::joinAndReset] Joining demux thread";
     thread_.reset();
     
     state_.store(DemuxState::Stopped);
     
   qInfo() << "[DemuxThread::joinAndReset] Demux thread stopped";
}

void DemuxThread::stop()
{
    requestStop();
    joinAndReset();
 }
 
 bool DemuxThread::isRunning() const
 {
     DemuxState s = state_.load();
     return s == DemuxState::Running || s == DemuxState::Paused;
 }
 
 // ==================== 解复用循环 ====================
 
 void DemuxThread::demuxLoop(std::stop_token stoken)
 {
 #ifdef HAS_FFMPEG
    bool eofHandled = false;
     
     // 分配数据包
     AVPacket* packet = av_packet_alloc();
     if (!packet) {
         state_.store(DemuxState::Stopped);
         return;
     }
     
     // 主循环
     while (!stoken.stop_requested()) {
         // 等待可运行状态（处理暂停）
         if (!waitForRunnable(stoken)) {
             break;
         }
         
         // 检查格式上下文
         if (!ctx_.formatCtx) {
             break;
         }
 
         // 读取数据包
         int ret = readPacket(packet);
         
         // 处理读取结果
        if (ret == AVERROR_EOF) {
            // 仅处理一次 EOF，随后进入等待态，由外部 stop() 统一回收线程
            // 这样可以避免线程在 EOF 处自然退出带来的不稳定退出路径
            if (!eofHandled) {
                handleEof(stoken);
                eofHandled = true;
                state_.store(DemuxState::Paused);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
         } else if (ret < 0) {
             if (!handleReadError(ret)) {
                 break;
             }
             continue;
         }
         
         // 分发数据包
         if (!dispatchPacket(packet, stoken)) {
             av_packet_free(&packet);
             packet = nullptr;
             break;
         }
 
         // 分配新数据包
         packet = nullptr;
         packet = av_packet_alloc();
         if (!packet) {
             break;
         }
     }
     
     // 清理
     if (packet) {
         av_packet_free(&packet);
         packet = nullptr;
     }
     
   state_.store(DemuxState::Stopped);
 #else
     state_.store(DemuxState::Stopped);
 #endif
 }
 
 bool DemuxThread::waitForRunnable(std::stop_token stoken)
 {
     while (!stoken.stop_requested()) {
         DemuxState current = state_.load();
         
         // Running 状态：可以继续
         if (current == DemuxState::Running) {
             // 再检查播放状态
             if (ctx_.playbackState) {
                 PlaybackState playState = ctx_.playbackState->load();
                 if (playState == PlaybackState::Playing) {
                     return true;  // 可以运行
                 }
                 // 暂停状态：等待条件变量
             }
         }
         
         // Paused 或播放暂停：等待
         if (current == DemuxState::Paused ||
             (ctx_.playbackState && 
              ctx_.playbackState->load() != PlaybackState::Playing)) {
             
             std::unique_lock<std::mutex> lock(mutex_);
             
             // 使用 wait_for 避免死锁，同时支持 stop_token
             cv_.wait_for(lock, 
                          std::chrono::milliseconds(ctx_.strategy.pauseCheckIntervalMs),
                          [this, &stoken]() {
                              if (stoken.stop_requested()) return true;
                             DemuxState s = state_.load();
                             if (s == DemuxState::Stopping || s == DemuxState::Stopped) return true;
                             if (ctx_.playbackState &&
                                  ctx_.playbackState->load() == PlaybackState::Playing) {
                                  return true;
                              }
                              return false;
                          });
             continue;
         }
         
         // Stopping/Stopped：退出
         if (current == DemuxState::Stopping || current == DemuxState::Stopped) {
             return false;
         }
     }
     
     return false;
 }
 
 int DemuxThread::readPacket(AVPacket* packet)
 {
 #ifdef HAS_FFMPEG
     return av_read_frame(ctx_.formatCtx, packet);
 #else
     return -1;
 #endif
 }
 
 bool DemuxThread::dispatchPacket(AVPacket* packet, std::stop_token stoken)
 {
 #ifdef HAS_FFMPEG
     // 判断目标队列
     LockFreePacketQueue* targetQueue = nullptr;
     
     if (packet->stream_index == ctx_.videoStreamIndex && ctx_.videoPacketQueue) {
         targetQueue = ctx_.videoPacketQueue;
     } else if (packet->stream_index == ctx_.audioStreamIndex && ctx_.audioPacketQueue) {
         targetQueue = ctx_.audioPacketQueue;
     } else {
         // 其他流：直接丢弃
         av_packet_free(&packet);
         packet = nullptr;
         return true;
     }
     
     // 推入队列
     return pushPacketWithBackpressure(targetQueue, packet, stoken);
 #else
     return false;
 #endif
 }
 
 bool DemuxThread::pushPacketWithBackpressure(LockFreePacketQueue* queue,
                                               AVPacket* packet,
                                               std::stop_token stoken)
 {
     if (!queue) return false;
     
     const int maxRetries = ctx_.strategy.maxPushRetries;
     const int basePushRetryIntervalMs = ctx_.strategy.basePushRetryIntervalMs;
     const int maxRetryIntervalMs = ctx_.strategy.maxRetryIntervalMs;
     int retryCount = 0;
     
     while (!stoken.stop_requested() && retryCount < maxRetries) {
         // 检查队列是否被中断
         if (queue->isAborted()) {
             return false;
         }
         
         // 尝试推入
         if (queue->push(packet)) {
             return true;
         }

         // endbackTest
         // break;
         
         // 队列满，记录重试
         retryCount++;
         
        // 背压等待（指数退避）
        // 避免 1 << retryCount 在大重试次数时触发未定义行为（位移溢出）
        const int cappedExponent = std::min(retryCount, 10);  // 2^10=1024，足够达到 maxRetryIntervalMs
        const int backoffMultiplier = (1 << cappedExponent);
        const int retryIntervalMs = std::min(basePushRetryIntervalMs * backoffMultiplier, maxRetryIntervalMs);
        std::this_thread::sleep_for(std::chrono::milliseconds(retryIntervalMs));
     }
     
     // 超时
    (void)retryCount;
     
 #ifdef HAS_FFMPEG
     av_packet_free(&packet);
     // bool is_key_frame = (packet->flags & AV_PKT_FLAG_KEY) != 0;
     // if(!is_key_frame){
     //     av_packet_free(&packet);
     //     packet = nullptr;
     // }
 #endif
     return true;  // 返回 true 表示继续运行（只是丢弃了这个包）
 }
 
 bool DemuxThread::pushEofMarkerWithBackpressure(LockFreePacketQueue* queue,
                                                   std::stop_token stoken)
 {
     if (!queue) return false;
     
     // EOF 标记使用更长的超时时间，确保最终能推入
     // 使用普通数据包重试次数的 2 倍，确保 EOF 标记不会丢失
     const int maxRetries = ctx_.strategy.maxPushRetries * 2;
     const int basePushRetryIntervalMs = ctx_.strategy.basePushRetryIntervalMs;
     const int maxRetryIntervalMs = ctx_.strategy.maxRetryIntervalMs;
     int retryCount = 0;
     
     while (!stoken.stop_requested() && retryCount < maxRetries) {
         // 检查队列是否被中断
         if (queue->isAborted()) {
             return false;
         }
         
         // 尝试推入 EOF 标记
         if (queue->pushEofMarker()) {
             return true;
         }
         
         // 队列满，记录重试
         retryCount++;
         
        // 背压等待（指数退避）
        // 避免 1 << retryCount 在大重试次数时触发未定义行为（位移溢出）
        const int cappedExponent = std::min(retryCount, 10);
        const int backoffMultiplier = (1 << cappedExponent);
        const int retryIntervalMs = std::min(basePushRetryIntervalMs * backoffMultiplier, maxRetryIntervalMs);
        std::this_thread::sleep_for(std::chrono::milliseconds(retryIntervalMs));
     }
     
     // 超时：EOF 标记推入失败
    (void)retryCount;
     
     return false;  // EOF 标记推入失败
 }
 
 void DemuxThread::handleEof(std::stop_token stoken)
 {
 #ifdef HAS_FFMPEG
     // 向两个队列推入 EOF 标记（带背压控制）
     bool videoEofSuccess = true;
     bool audioEofSuccess = true;
     
     if (ctx_.videoPacketQueue) {
         videoEofSuccess = pushEofMarkerWithBackpressure(ctx_.videoPacketQueue, stoken);
     }
     
     if (ctx_.audioPacketQueue) {
         audioEofSuccess = pushEofMarkerWithBackpressure(ctx_.audioPacketQueue, stoken);
     }
    (void)videoEofSuccess;
    (void)audioEofSuccess;
     
    // 注意：EOF 通知当前未被上层消费，避免在工作线程退出路径触发额外 Qt 元对象调度
    // （该路径已出现线程退出阶段崩溃，先做最小化隔离）
 #endif
 }
 
 bool DemuxThread::handleReadError(int errorCode)
 {
 #ifdef HAS_FFMPEG
     char errbuf[AV_ERROR_MAX_STRING_SIZE];
     av_strerror(errorCode, errbuf, sizeof(errbuf));
     
     if (errorCode == AVERROR(EAGAIN)) {
         // 需要更多数据：短暂等待后重试
         std::this_thread::sleep_for(std::chrono::milliseconds(10));
         return true;
     }
     return false;
     
 #else
     return false;
 #endif
 }
 
 } // namespace AdvancedPlayer
 
