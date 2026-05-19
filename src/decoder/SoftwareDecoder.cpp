#include "SoftwareDecoder.h"
#include <QDebug>

#ifdef HAS_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/dict.h>
}
#endif

namespace AdvancedPlayer {

SoftwareDecoder::SoftwareDecoder(AVCodecContext* codecCtx)
    : codecCtx_(codecCtx) {
}

SoftwareDecoder::~SoftwareDecoder() {
#ifdef HAS_FFMPEG
    if (frame_) {
        av_frame_free(&frame_);
    }
    qDebug() << "[SoftwareDecoder::~SoftwareDecoder] Software decoder cleaned up";
#endif
}

bool SoftwareDecoder::initialize(AVDictionary** options) {
    qInfo() << "[SoftwareDecoder::initialize] Initializing software decoder";
    
#ifdef HAS_FFMPEG
    if (!codecCtx_) {
        qWarning() << "[SoftwareDecoder::initialize] Decoder context is null";
        return false;
    }
    
    // 打开解码器
    // 如果传入了选项字典，使用它；否则创建空字典
    AVDictionary* openOptions = nullptr;
    if (options && *options) {
        // 复制选项字典（因为 avcodec_open2 会修改字典）
        av_dict_copy(&openOptions, *options, 0);
        
        // 输出选项信息用于调试
        AVDictionaryEntry* entry = nullptr;
        qInfo() << "[SoftwareDecoder::initialize] Decoder options:";
        while ((entry = av_dict_get(*options, "", entry, AV_DICT_IGNORE_SUFFIX))) {
            qInfo() << "  " << entry->key << "=" << entry->value;
        }
    }
    int ret = avcodec_open2(codecCtx_, codecCtx_->codec, &openOptions);

    // 检查未使用的选项（如果有，说明某些选项可能未被识别）
    if (openOptions) {
        AVDictionaryEntry* entry = nullptr;
        bool hasUnused = false;
        while ((entry = av_dict_get(openOptions, "", entry, AV_DICT_IGNORE_SUFFIX))) {
            if (!hasUnused) {
                qWarning() << "[SoftwareDecoder::initialize] The following options were not recognized by decoder:";
                hasUnused = true;
            }
            qWarning() << "  Unrecognized: " << entry->key << "=" << entry->value;
        }
        av_dict_free(&openOptions);
    }
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        qWarning() << "[SoftwareDecoder::initialize] Failed to open software decoder:" << errbuf;
        return false;
    }
    
    qInfo() << "[SoftwareDecoder::initialize] Software decoder initialized";
    return true;
#else
    qWarning() << "[SoftwareDecoder::initialize] FFmpeg is not enabled, software decoding unavailable";
    return false;
#endif
}

AVFrame* SoftwareDecoder::decode(const AVPacket* packet) {
#ifdef HAS_FFMPEG
    if (!codecCtx_) {
        return nullptr;
    }
    
    // ==================== 正确的解码流程（遵循FFmpeg规范）====================
    // 1. 先尝试接收已解码的帧（处理EAGAIN情况）
    // 2. 然后发送新数据包
    // 3. 最后再次尝试接收帧
    
    // 分配帧缓冲区
    if (!frame_) {
        frame_ = av_frame_alloc();
        if (!frame_) {
            qWarning() << "[SoftwareDecoder::decode] Failed to allocate AVFrame";
            return nullptr;
        }
    }
    
    // ===== 步骤1：先尝试接收已解码的帧 =====
    // 如果解码器内部有缓冲的帧，先取出来
    av_frame_unref(frame_);
    int ret = avcodec_receive_frame(codecCtx_, frame_);
    
    if (ret == 0) {
        // 成功接收到帧，处理并返回
        // 注意：此时数据包还未发送，会在下次调用时发送
        // 这是正常的，因为解码器可能有多帧缓冲
        AVFrame* result = createFrameReference();
        if (result) {
            return result;
        }
        // 如果处理失败，继续尝试发送数据包
    } else if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
        // 接收帧时出现错误（非EAGAIN/EOF）
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        
        // 检测参考帧丢失错误
        bool isRefFrameError = (strstr(errbuf, "ref") != nullptr || 
                               strstr(errbuf, "reference") != nullptr ||
                               strstr(errbuf, "POC") != nullptr);
        
        if (isRefFrameError) {
            static int receiveRefErrorCount = 0;
            receiveRefErrorCount++;
            
            if (receiveRefErrorCount <= 3) {
                qWarning() << "[SoftwareDecoder::decode] Reference frame loss detected while receiving frame:" << errbuf
                          << ", flushing decoder buffers (attempt" << receiveRefErrorCount << ")";
                // 刷新解码器缓冲区
                avcodec_flush_buffers(codecCtx_);
            }
        } else {
            qWarning() << "[SoftwareDecoder::decode] Failed to receive decoded frame:" << errbuf;
        }
        // 继续尝试发送数据包，可能能恢复
    }
    // EAGAIN 表示需要更多输入数据，这是正常的，继续发送数据包
    // EOF 表示解码结束，也是正常的
    
    // ===== 步骤2：发送数据包到解码器 =====
    if (packet) {
        ret = avcodec_send_packet(codecCtx_, packet);
        if (ret < 0) {
            if (ret == AVERROR(EAGAIN)) {
                // EAGAIN：解码器缓冲区已满，需要先接收帧
                // 这种情况理论上不应该发生，因为我们已经先尝试接收了
                // 但如果发生了，说明解码器状态异常，尝试接收一次
                qDebug() << "[SoftwareDecoder::decode] send_packet returned EAGAIN, trying to receive frame";
                av_frame_unref(frame_);
                ret = avcodec_receive_frame(codecCtx_, frame_);
                if (ret == 0) {
                    // 成功接收到帧
                    return createFrameReference();
                }
                // 如果还是EAGAIN，说明解码器状态异常，记录警告
                if (ret == AVERROR(EAGAIN)) {
                    static int eagainCount = 0;
                    eagainCount++;
                    if (eagainCount <= 3) {
                        qWarning() << "[SoftwareDecoder::decode] Abnormal decoder state:"
                                   << "both send_packet and receive_frame returned EAGAIN (attempt" << eagainCount << ")";
                    }
                }
                // 数据包未发送成功，返回nullptr（调用者应该保留数据包以便重试）
                return nullptr;
            } else if (ret == AVERROR_EOF) {
                // EOF：解码器已刷新，不能再发送数据包
                return nullptr;
            } else {
                // 其他错误
                char errbuf[AV_ERROR_MAX_STRING_SIZE];
                av_strerror(ret, errbuf, sizeof(errbuf));
                
                // 检测参考帧丢失错误（HEVC常见问题）
                bool isRefFrameError = (strstr(errbuf, "ref") != nullptr || 
                                       strstr(errbuf, "reference") != nullptr ||
                                       strstr(errbuf, "POC") != nullptr);
                
                static int errorCount = 0;
                static int refFrameErrorCount = 0;
                errorCount++;
                
                if (isRefFrameError) {
                    refFrameErrorCount++;
                    // 参考帧丢失：尝试刷新解码器缓冲区
                    if (refFrameErrorCount <= 3) {
                        qWarning() << "[SoftwareDecoder::decode] Reference frame loss error detected:" << errbuf
                                  << ", trying to flush decoder buffers (attempt" << refFrameErrorCount << ")";
                        
                        // 刷新解码器：清空内部缓冲区，丢弃损坏的参考帧
                        avcodec_flush_buffers(codecCtx_);
                        
                        // 注意：刷新后，当前数据包应该被丢弃，等待下一个关键帧
                        // 返回 nullptr 让调用者知道需要跳过这个数据包
                        return nullptr;
                    } else if (refFrameErrorCount == 4) {
                        qWarning() << "[SoftwareDecoder::decode] Reference frame loss keeps happening,"
                                  << "please check whether the video file is corrupted";
                    }
                } else {
                    // 其他类型的错误
                    if (errorCount <= 5) {
                        qWarning() << "[SoftwareDecoder::decode] Failed to send packet to decoder:" << errbuf
                                  << "(this is error #" << errorCount << ", later errors will use debug level)";
                    }
                }
                return nullptr;
            }
        }
    }
    
    // ===== 步骤3：尝试接收新解码的帧 =====
    av_frame_unref(frame_);
    ret = avcodec_receive_frame(codecCtx_, frame_);
    
    if (ret == 0) {
        // 成功接收到帧
        return createFrameReference();
    } else if (ret == AVERROR(EAGAIN)) {
        // 需要更多数据，这是正常的
        return nullptr;
    } else if (ret == AVERROR_EOF) {
        // 解码结束
        return nullptr;
    } else if (ret < 0) {
        // 其他错误
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        
        // 检测参考帧丢失错误
        bool isRefFrameError = (strstr(errbuf, "ref") != nullptr || 
                               strstr(errbuf, "reference") != nullptr ||
                               strstr(errbuf, "POC") != nullptr);
        
        if (isRefFrameError) {
            static int receiveRefErrorCount = 0;
            receiveRefErrorCount++;
            
            if (receiveRefErrorCount <= 3) {
                qWarning() << "[SoftwareDecoder::decode] Reference frame loss detected while receiving frame:" << errbuf
                          << ", flushing decoder buffers (attempt" << receiveRefErrorCount << ")";
                // 刷新解码器缓冲区
                avcodec_flush_buffers(codecCtx_);
            }
        } else {
            qWarning() << "[SoftwareDecoder::decode] Failed to receive decoded frame:" << errbuf;
        }
        return nullptr;
    }
    
    // 不应该到达这里
    return nullptr;
#else
    return nullptr;
#endif
}

AVFrame* SoftwareDecoder::flush() {
#ifdef HAS_FFMPEG
    // 发送空数据包以刷新解码器
    return decode(nullptr);
#else
    return nullptr;
#endif
}

AVFrame* SoftwareDecoder::createFrameReference() {
#ifdef HAS_FFMPEG
    if (!frame_) {
        return nullptr;
    }
    
    // ==================== 高效返回帧的引用以支持所有权转移 ====================
    // 解码器内部重用 frame_ 缓冲区，使用引用计数而非深拷贝
    // FFmpeg 的帧使用 AVBufferRef 引用计数，av_frame_ref() 只增加引用计数，不拷贝像素数据
    // 引用计数机制确保：当解码器下次调用 av_frame_unref(frame_) 时，数据不会被释放
    // 因为队列中的帧仍在引用数据缓冲区，只有当队列释放帧时，数据才会真正释放
    AVFrame* refFrame = av_frame_alloc();
    if (!refFrame) {
        qWarning() << "[SoftwareDecoder::createFrameReference] Failed to allocate AVFrame";
        return nullptr;
    }
    
    int ret = av_frame_ref(refFrame, frame_);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        qWarning() << "[SoftwareDecoder::createFrameReference] Failed to reference frame:" << errbuf;
        av_frame_free(&refFrame);
        return nullptr;
    }
    
    // 调用者获得引用的所有权，负责释放（释放时会自动减少引用计数）
    return refFrame;
#else
    return nullptr;
#endif
}

} // namespace AdvancedPlayer

