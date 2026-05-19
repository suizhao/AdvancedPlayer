#include "LockFreePacketQueue.h"
#include <QDebug>

#ifdef HAS_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
}
#endif

namespace AdvancedPlayer {

// ==================== 构造与析构 ====================

LockFreePacketQueue::LockFreePacketQueue(size_t capacity, const QString& name)
    : buffer_(capacity)
    , name_(name.isEmpty() ? QStringLiteral("LockFreePacketQueue") : name) {
    
    qInfo() << "[LockFreePacketQueue]" << name_
            << "created, capacity:" << buffer_.capacity() << "(lock-free queue)";
}

LockFreePacketQueue::~LockFreePacketQueue() {
    clear();
    qDebug() << "[LockFreePacketQueue]" << name_ << "destroyed";
}

// ==================== 核心操作 ====================

bool LockFreePacketQueue::push(AVPacket* packet) {
    if (!packet) {
        qWarning() << "[LockFreePacketQueue::push]" << name_
                   << "null packet is not allowed, use pushEofMarker()";
        return false;
    }
    
    if (!buffer_.tryPush(packet)) {
        // 队列满或已中止，所有权未转移
        return false;
    }

    return true;  // 所有权已转移
}

bool LockFreePacketQueue::pop(AVPacket** packet) {
    if (!packet) {
        qWarning() << "[LockFreePacketQueue::pop]" << name_ << "output parameter is null";
        return false;
    }
    
    AVPacket* pkt = nullptr;
    if (!buffer_.tryPop(pkt)) {
        return false;  // 队列空或已中止
    }
    
    *packet = pkt;  // 可能是 nullptr（EOF 标记）
    return true;
}

// ==================== 状态查询 ====================

size_t LockFreePacketQueue::size() const {
    return buffer_.size();
}

// ==================== 控制操作 ====================

void LockFreePacketQueue::clear() {
#ifdef HAS_FFMPEG
    buffer_.clearWithCleanup([](AVPacket*& packet) {
        if (packet) {
            av_packet_free(&packet);
            packet = nullptr;
        }
    });
    
    qDebug() << "[LockFreePacketQueue::clear]" << name_ << "queue cleared";
#else
    buffer_.clear();
#endif
}

// ==================== EOF 标记 ====================

bool LockFreePacketQueue::pushEofMarker() {
    // EOF 使用 nullptr 表示
    AVPacket* eofMarker = nullptr;
    
    if (buffer_.tryPush(eofMarker)) {
        return true;
    } else {
        // 队列满或已中止，返回 false（由调用者决定是否重试）
        return false;
    }
}

} // namespace AdvancedPlayer
