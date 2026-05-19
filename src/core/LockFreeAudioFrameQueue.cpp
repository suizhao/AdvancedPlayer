#include "LockFreeAudioFrameQueue.h"
#include <QDebug>
#include <cstdlib>  // for free()

#ifdef HAS_FFMPEG
extern "C" {
#include <libavutil/mem.h>  // for av_free()
}
#endif

namespace AdvancedPlayer {

// ==================== 构造与析构 ====================

LockFreeAudioFrameQueue::LockFreeAudioFrameQueue(size_t capacity, const QString& name)
    : buffer_(capacity)
    , name_(name.isEmpty() ? QStringLiteral("LockFreeAudioFrameQueue") : name) {
    
    qInfo() << "[LockFreeAudioFrameQueue]" << name_
            << "created, capacity:" << buffer_.capacity() << "(lock-free queue)";
}

LockFreeAudioFrameQueue::~LockFreeAudioFrameQueue() {
    clear();
    qDebug() << "[LockFreeAudioFrameQueue]" << name_ << "destroyed";
}

// ==================== 核心操作 ====================

bool LockFreeAudioFrameQueue::push(uint8_t* data, int size, double pts) {
    if (!data) {
        qWarning() << "[LockFreeAudioFrameQueue::push]" << name_
                   << "null data is not allowed, use pushEofMarker()";
        return false;
    }
    
    AudioFrameNode node(data, size, pts);
    if (!buffer_.tryPush(std::move(node))) {
        // 队列满或已中止，所有权未转移
        return false;
    }
    
    return true;  // 所有权已转移
}

bool LockFreeAudioFrameQueue::pop(uint8_t** data, int* size, double* pts) {
    if (!data || !size || !pts) {
        qWarning() << "[LockFreeAudioFrameQueue::pop]" << name_ << "output parameter is null";
        return false;
    }
    
    AudioFrameNode node;
    if (!buffer_.tryPop(node)) {
        return false;  // 队列空或已中止
    }
    
    *data = node.data;  // 可能是 nullptr（EOF 标记）
    *size = node.size;
    *pts = node.pts;
    
    // 清空 node 防止析构时出问题（移动语义已处理）
    node.data = nullptr;
    
    return true;
}

bool LockFreeAudioFrameQueue::peek(uint8_t** data, int* size, double* pts) const {
    if (!data || !size || !pts) {
        return false;
    }
    
    AudioFrameNode node;
    if (!buffer_.peek(node)) {
        return false;
    }
    
    *data = node.data;
    *size = node.size;
    *pts = node.pts;
    return true;
}

// ==================== 状态查询 ====================

size_t LockFreeAudioFrameQueue::size() const {
    return buffer_.size();
}

// ==================== 控制操作 ====================

void LockFreeAudioFrameQueue::clear() {
#ifdef HAS_FFMPEG
    buffer_.clearWithCleanup([](AudioFrameNode& node) {
        if (node.data) {
            // 音频数据使用 av_malloc 分配，必须用 av_free 释放
            av_free(node.data);
        }
        node.data = nullptr;
        node.size = 0;
        node.pts = 0.0;
    });
#else
    buffer_.clearWithCleanup([](AudioFrameNode& node) {
        if (node.data) {
            free(node.data);
        }
        node.data = nullptr;
        node.size = 0;
        node.pts = 0.0;
    });
#endif
    
    qDebug() << "[LockFreeAudioFrameQueue::clear]" << name_ << "queue cleared";
}

// ==================== EOF 标记 ====================

bool LockFreeAudioFrameQueue::pushEofMarker() {
    // EOF 使用 nullptr + size=0 + pts=-1 表示
    AudioFrameNode eofNode(nullptr, 0, -1.0);
    
    if (buffer_.tryPush(std::move(eofNode))) {
        return true;
    }
    return false;
}

// ==================== 辅助接口 ====================

bool LockFreeAudioFrameQueue::containsPts(double targetPts) const {
    auto range = getPtsRange();
    
    // 队列为空
    if (range.first == 0.0 && range.second == 0.0 && buffer_.empty()) {
        return false;
    }
    
    // 检查目标 PTS 是否在范围内
    return targetPts >= range.first && targetPts <= range.second;
}

int LockFreeAudioFrameQueue::discardBefore(double targetPts) {
    int discardCount = 0;
    
    // 遍历队列，计算需要丢弃的帧数
    buffer_.forEach([&](const AudioFrameNode& node, size_t) {
        // 跳过 EOF 标记
        if (node.data == nullptr) {
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
#ifdef HAS_FFMPEG
    size_t actualDiscarded = buffer_.discardN(discardCount, [](AudioFrameNode& node) {
        if (node.data) {
            av_free(node.data);
        }
        node.data = nullptr;
        node.size = 0;
        node.pts = 0.0;
    });
#else
    size_t actualDiscarded = buffer_.discardN(discardCount, [](AudioFrameNode& node) {
        if (node.data) {
            free(node.data);
        }
        node.data = nullptr;
        node.size = 0;
        node.pts = 0.0;
    });
#endif
    
    qDebug() << "[LockFreeAudioFrameQueue::discardBefore]" << name_
             << "discarded" << actualDiscarded << "frames (target PTS:" << targetPts << "s)";
    
    return static_cast<int>(actualDiscarded);
}

std::pair<double, double> LockFreeAudioFrameQueue::getPtsRange() const {
    double minPts = 0.0;
    double maxPts = 0.0;
    bool firstFrame = true;
    
    buffer_.forEach([&](const AudioFrameNode& node, size_t) {
        // 跳过 EOF 标记
        if (node.data == nullptr) {
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
