#include "LockFreeVideoFrameQueue.h"
#include <QDebug>

#ifdef HAS_FFMPEG
extern "C" {
#include <libavutil/frame.h>
}
#endif

namespace AdvancedPlayer {

// ==================== 构造与析构 ====================

LockFreeVideoFrameQueue::LockFreeVideoFrameQueue(size_t capacity, const QString& name)
    : buffer_(capacity)
    , name_(name.isEmpty() ? QStringLiteral("LockFreeVideoFrameQueue") : name) {
    
    qInfo() << "[LockFreeVideoFrameQueue]" << name_
            << "created, capacity:" << buffer_.capacity() << "(lock-free queue)";
}

LockFreeVideoFrameQueue::~LockFreeVideoFrameQueue() {
    clear();
    qDebug() << "[LockFreeVideoFrameQueue]" << name_ << "destroyed";
}

// ==================== 核心操作 ====================

bool LockFreeVideoFrameQueue::push(AVFrame* frame, double pts) {
    if (!frame) {
        qWarning() << "[LockFreeVideoFrameQueue::push]" << name_
                   << "null frame is not allowed, use pushEofMarker()";
        return false;
    }
    
    VideoFrameNode node(frame, pts);
    if (!buffer_.tryPush(std::move(node))) {
        // 队列满或已中止，所有权未转移
        return false;
    }
    
    return true;  // 所有权已转移
}

bool LockFreeVideoFrameQueue::pop(AVFrame** frame, double* pts) {
    if (!frame || !pts) {
        qWarning() << "[LockFreeVideoFrameQueue::pop]" << name_ << "output parameter is null";
        return false;
    }
    
    VideoFrameNode node;
    if (!buffer_.tryPop(node)) {
        return false;  // 队列空或已中止
    }
    
    *frame = node.frame;  // 可能是 nullptr（EOF 标记）
    *pts = node.pts;
    
    // 清空 node 防止析构时出问题（移动语义已处理）
    node.frame = nullptr;
    
    return true;
}

bool LockFreeVideoFrameQueue::peek(AVFrame** frame, double* pts) const {
    if (!frame || !pts) {
        return false;
    }
    
    VideoFrameNode node;
    if (!buffer_.peek(node)) {
        return false;
    }
    
    *frame = node.frame;
    *pts = node.pts;
    return true;
}

// ==================== 状态查询 ====================

size_t LockFreeVideoFrameQueue::size() const {
    return buffer_.size();
}

// ==================== 控制操作 ====================

void LockFreeVideoFrameQueue::clear() {
#ifdef HAS_FFMPEG
    buffer_.clearWithCleanup([](VideoFrameNode& node) {
        if (node.frame) {
            av_frame_free(&node.frame);
        }
        node.frame = nullptr;
        node.pts = 0.0;
    });
    
    qDebug() << "[LockFreeVideoFrameQueue::clear]" << name_ << "queue cleared";
#else
    buffer_.clear();
#endif
}

// ==================== EOF 标记 ====================

bool LockFreeVideoFrameQueue::pushEofMarker() {
    // EOF 使用 nullptr + pts=-1 表示
    VideoFrameNode eofNode(nullptr, -1.0);
    
    if (buffer_.tryPush(std::move(eofNode))) {
        return true;
    }
    return false;
}

// ==================== 辅助接口 ====================

bool LockFreeVideoFrameQueue::containsPts(double targetPts) const {
    auto range = getPtsRange();
    
    // 队列为空
    if (range.first == 0.0 && range.second == 0.0 && buffer_.empty()) {
        return false;
    }
    
    // 检查目标 PTS 是否在范围内
    return targetPts >= range.first && targetPts <= range.second;
}

int LockFreeVideoFrameQueue::discardBefore(double targetPts) {
#ifdef HAS_FFMPEG
    int discardCount = 0;
    
    // 遍历队列，计算需要丢弃的帧数
    buffer_.forEach([&](const VideoFrameNode& node, size_t) {
        // 跳过 EOF 标记
        if (node.frame == nullptr) {
            return false;  // 停止遍历
        }
        
        // 如果 PTS >= 目标，停止计数
        if (node.pts >= targetPts) {
            return false;
        }
        
        ++discardCount;
        return true;  // 继续遍历
    });
    
    if (discardCount == 0) {
        return 0;
    }
    
    // 执行丢弃操作
    size_t actualDiscarded = buffer_.discardN(discardCount, [](VideoFrameNode& node) {
        if (node.frame) {
            av_frame_free(&node.frame);
        }
        node.frame = nullptr;
        node.pts = 0.0;
    });
    
    qDebug() << "[LockFreeVideoFrameQueue::discardBefore]" << name_
             << "discarded" << actualDiscarded << "frames (target PTS:" << targetPts << "s)";
    
    return static_cast<int>(actualDiscarded);
#else
    Q_UNUSED(targetPts);
    return 0;
#endif
}

std::pair<double, double> LockFreeVideoFrameQueue::getPtsRange() const {
    double minPts = 0.0;
    double maxPts = 0.0;
    bool firstFrame = true;
    
    buffer_.forEach([&](const VideoFrameNode& node, size_t) {
        // 跳过 EOF 标记
        if (node.frame == nullptr) {
            return true;  // 继续遍历
        }
        
        if (firstFrame) {
            minPts = node.pts;
            maxPts = node.pts;
            firstFrame = false;
        } else {
            if (node.pts < minPts) minPts = node.pts;
            if (node.pts > maxPts) maxPts = node.pts;
        }
        
        return true;  // 继续遍历
    });
    
    return {minPts, maxPts};
}

} // namespace AdvancedPlayer
