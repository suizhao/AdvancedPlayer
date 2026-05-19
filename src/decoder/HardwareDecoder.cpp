#include "HardwareDecoder.h"
#include <QDebug>

// ===================================================================
// 平台特定的条件编译配置
// ===================================================================

// 检测操作系统平台
#if defined(_WIN32) || defined(_WIN64)
    #define PLATFORM_WINDOWS
#elif defined(__APPLE__) && defined(__MACH__)
    #define PLATFORM_MACOS
    #include <TargetConditionals.h>
#elif defined(__linux__)
    #define PLATFORM_LINUX
#endif

// FFmpeg 硬件加速支持
#ifdef HAS_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
#include <libavutil/opt.h>
#include <libavutil/dict.h>
}

// 平台特定的硬件加速支持
#ifdef PLATFORM_WINDOWS
    // Windows 平台支持：D3D11VA, D3D12VA, DXVA2, NVDEC, QSV
    #define HW_ACCEL_D3D11VA_AVAILABLE
    #define HW_ACCEL_D3D12VA_AVAILABLE
    #define HW_ACCEL_DXVA2_AVAILABLE
#endif

#ifdef PLATFORM_MACOS
    // macOS 平台支持：VideoToolbox
    #define HW_ACCEL_VIDEOTOOLBOX_AVAILABLE
#endif

#ifdef PLATFORM_LINUX
    // Linux 平台支持：VAAPI, NVDEC, QSV
    #define HW_ACCEL_VAAPI_AVAILABLE
#endif

// NVIDIA NVDEC 和 Intel QSV 在多个平台上可用
#if defined(PLATFORM_WINDOWS) || defined(PLATFORM_LINUX)
    #define HW_ACCEL_NVDEC_AVAILABLE
    #define HW_ACCEL_QSV_AVAILABLE
#endif

#endif // HAS_FFMPEG

// ===================================================================

namespace AdvancedPlayer {

HardwareDecoder::HardwareDecoder(AVCodecContext* codecCtx, HardwareAccelType accelType)
    : codecCtx_(codecCtx), accelType_(accelType) {
}

HardwareDecoder::~HardwareDecoder() {
#ifdef HAS_FFMPEG
    // ==================== 正确的资源清理顺序 ====================
    // 1. 先清理 codecCtx_ 中的硬件设备上下文引用（如果 codecCtx_ 仍然有效）
    //    这确保在释放 hwDeviceCtx_ 之前，codecCtx_ 不再持有引用
    if (codecCtx_ && codecCtx_->hw_device_ctx) {
        av_buffer_unref(&codecCtx_->hw_device_ctx);
        codecCtx_->hw_device_ctx = nullptr;
        codecCtx_->get_format = nullptr;
        codecCtx_->opaque = nullptr;
        qDebug() << "[HardwareDecoder::~HardwareDecoder] Cleared hardware device context reference in codecCtx";
    }
    
    // 2. 清理解码器内部帧资源
    if (frame_) {
        av_frame_free(&frame_);
        frame_ = nullptr;
    }
    if (cpuFrame_) {
        av_frame_free(&cpuFrame_);
        cpuFrame_ = nullptr;
    }
    
    // 3. 最后释放硬件设备上下文（此时 codecCtx_ 已不再持有引用）
    if (hwDeviceCtx_) {
        av_buffer_unref(&hwDeviceCtx_);
        hwDeviceCtx_ = nullptr;
    }
    
    qInfo() << "[HardwareDecoder::~HardwareDecoder] Hardware decoder resources fully cleaned";
#endif
}

bool HardwareDecoder::initialize(AVDictionary** options) {
    qInfo() << "[HardwareDecoder::initialize] Initializing hardware decoder:" << QString::fromStdString(toString(accelType_));
    
#ifdef HAS_FFMPEG
    if (!codecCtx_) {
        qWarning() << "[HardwareDecoder::initialize] Decoder context is null";
        return false;
    }
    
    // 1. 创建硬件设备上下文
    if (!initHardwareDevice()) {
        qWarning() << "[HardwareDecoder::initialize] Failed to create hardware device context";
        // 如果存在，清理硬件设备上下文
        if (hwDeviceCtx_) {
            av_buffer_unref(&hwDeviceCtx_);
            hwDeviceCtx_ = nullptr;
        }
        // 重置编解码器上下文设置以避免问题
        codecCtx_->hw_device_ctx = nullptr;
        codecCtx_->get_format = nullptr;
        codecCtx_->opaque = nullptr;
        return false;
    }
    
    // 2. 设置硬件解码回调
    codecCtx_->get_format = getHwFormat;
    codecCtx_->opaque = this;
    
    // 3. 打开解码器
    // 如果传入了选项字典，使用它；否则创建空字典
    AVDictionary* openOptions = nullptr;
    if (options && *options) {
        // 复制选项字典（因为 avcodec_open2 会修改字典）
        av_dict_copy(&openOptions, *options, 0);
        
        // 输出选项信息用于调试
        AVDictionaryEntry* entry = nullptr;
        qInfo() << "[HardwareDecoder::initialize] Decoder options:";
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
                qWarning() << "[HardwareDecoder::initialize] The following options were not recognized by decoder:";
                hasUnused = true;
            }
            qWarning() << "  Unrecognized: " << entry->key << "=" << entry->value;
        }
        av_dict_free(&openOptions);
    }
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        qWarning() << "[HardwareDecoder::initialize] Failed to open hardware decoder:" << errbuf;
        
        // 失败时清理
        if (hwDeviceCtx_) {
            av_buffer_unref(&hwDeviceCtx_);
            hwDeviceCtx_ = nullptr;
        }
        codecCtx_->hw_device_ctx = nullptr;
        codecCtx_->get_format = nullptr;
        codecCtx_->opaque = nullptr;
        return false;
    }
    
    qInfo() << "[HardwareDecoder::initialize] Hardware decoder initialized";
    return true;
#else
    qWarning() << "[HardwareDecoder::initialize] FFmpeg is not enabled, hardware decoding unavailable";
    return false;
#endif
}

bool HardwareDecoder::initHardwareDevice() {
#ifdef HAS_FFMPEG
    enum AVHWDeviceType type = AV_HWDEVICE_TYPE_NONE;
    
    // 将硬件加速类型映射到 FFmpeg 类型（使用平台特定的条件编译）
    switch (accelType_) {
        case HardwareAccelType::D3D11VA:
#ifdef HW_ACCEL_D3D11VA_AVAILABLE
            type = AV_HWDEVICE_TYPE_D3D11VA;
#else
            qWarning() << "[HardwareDecoder::initHardwareDevice] D3D11VA is unavailable on current platform";
            return false;
#endif
            break;
            
        case HardwareAccelType::DXVA2:
#ifdef HW_ACCEL_DXVA2_AVAILABLE
            type = AV_HWDEVICE_TYPE_DXVA2;
#else
            qWarning() << "[HardwareDecoder::initHardwareDevice] DXVA2 is unavailable on current platform";
            return false;
#endif
            break;
            
        case HardwareAccelType::VAAPI:
#ifdef HW_ACCEL_VAAPI_AVAILABLE
            type = AV_HWDEVICE_TYPE_VAAPI;
#else
            qWarning() << "[HardwareDecoder::initHardwareDevice] VAAPI is unavailable on current platform";
            return false;
#endif
            break;
            
        case HardwareAccelType::VideoToolbox:
#ifdef HW_ACCEL_VIDEOTOOLBOX_AVAILABLE
            type = AV_HWDEVICE_TYPE_VIDEOTOOLBOX;
#else
            qWarning() << "[HardwareDecoder::initHardwareDevice] VideoToolbox is unavailable on current platform";
            return false;
#endif
            break;
            
        case HardwareAccelType::NVDEC:
#ifdef HW_ACCEL_NVDEC_AVAILABLE
            type = AV_HWDEVICE_TYPE_CUDA;
#else
            qWarning() << "[HardwareDecoder::initHardwareDevice] NVDEC is unavailable on current platform";
            return false;
#endif
            break;
            
        case HardwareAccelType::QSV:
#ifdef HW_ACCEL_QSV_AVAILABLE
            type = AV_HWDEVICE_TYPE_QSV;
#else
            qWarning() << "[HardwareDecoder::initHardwareDevice] QSV is unavailable on current platform";
            return false;
#endif
            break;
            
        case HardwareAccelType::D3D12VA:
#ifdef HW_ACCEL_D3D12VA_AVAILABLE
            type = AV_HWDEVICE_TYPE_D3D12VA;
#else
            qWarning() << "[HardwareDecoder::initHardwareDevice] D3D12VA is unavailable on current platform";
            return false;
#endif
            break;
            
        case HardwareAccelType::CUDA:
#ifdef HW_ACCEL_NVDEC_AVAILABLE
            // CUDA 和 NVDEC 使用相同的硬件设备类型
            type = AV_HWDEVICE_TYPE_CUDA;
#else
            qWarning() << "[HardwareDecoder::initHardwareDevice] CUDA is unavailable on current platform";
            return false;
#endif
            break;
            
        case HardwareAccelType::None:
            // None 不应该到达这里（应该在创建解码器前处理）
            qWarning() << "[HardwareDecoder::initHardwareDevice] Invalid hardware acceleration type (None)";
            return false;
            
        default:
            qWarning() << "[HardwareDecoder::initHardwareDevice] Unsupported hardware acceleration type";
            return false;
    }
    
    // 创建硬件设备上下文
    int ret = av_hwdevice_ctx_create(&hwDeviceCtx_, type, nullptr, nullptr, 0);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        qWarning() << "[HardwareDecoder::initHardwareDevice] Failed to create hardware device context:" << errbuf;
        // 确保失败时 hwDeviceCtx_ 为 null
        hwDeviceCtx_ = nullptr;
        return false;
    }
    
    // 将硬件设备上下文与解码器关联
    codecCtx_->hw_device_ctx = av_buffer_ref(hwDeviceCtx_);
    if (!codecCtx_->hw_device_ctx) {
        qWarning() << "[HardwareDecoder::initHardwareDevice] Failed to reference hardware device context";
        av_buffer_unref(&hwDeviceCtx_);
        hwDeviceCtx_ = nullptr;
        return false;
    }
    
    qInfo() << "[HardwareDecoder::initHardwareDevice] Hardware device initialized:" << av_hwdevice_get_type_name(type);
    return true;
#else
    qWarning() << "[HardwareDecoder::initHardwareDevice] FFmpeg is not enabled";
    return false;
#endif
}

AVFrame* HardwareDecoder::decode(const AVPacket* packet) {
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
            qWarning() << "[HardwareDecoder::decode] Failed to allocate AVFrame";
            return nullptr;
        }
    }
    
    // ===== 步骤1：先尝试接收已解码的帧 =====
    // 如果解码器内部有缓冲的帧，先取出来
    // 
    // 关键：av_frame_unref() 只释放 frame_ 的引用，不会影响解码器内部的参考帧
    // 解码器内部的参考帧由解码器自己管理，使用独立的缓冲区
    // 我们返回的帧是独立的，不会影响解码器内部的参考帧管理
    av_frame_unref(frame_);
    int ret = avcodec_receive_frame(codecCtx_, frame_);
    
    if (ret == 0) {
        // 成功接收到帧，处理并返回
        // 注意：此时数据包还未发送，会在下次调用时发送
        // 这是正常的，因为解码器可能有多帧缓冲
        AVFrame* result = processDecodedFrame();
        if (result) {
            return result;
        }
        // 如果处理失败，继续尝试发送数据包
    } else if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
        // 接收帧时出现错误（非EAGAIN/EOF）
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        qWarning() << "[HardwareDecoder::decode] Failed to receive decoded frame:" << errbuf;
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
                qDebug() << "[HardwareDecoder::decode] send_packet returned EAGAIN, trying to receive frame";
                av_frame_unref(frame_);
                ret = avcodec_receive_frame(codecCtx_, frame_);
                if (ret == 0) {
                    // 成功接收到帧
                    return processDecodedFrame();
                }
                // 如果还是EAGAIN，说明解码器状态异常，记录警告
                if (ret == AVERROR(EAGAIN)) {
                    static int eagainCount = 0;
                    eagainCount++;
                    if (eagainCount <= 3) {
                        qWarning() << "[HardwareDecoder::decode] Abnormal decoder state:"
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
                        qWarning() << "[HardwareDecoder::decode] Reference frame loss error detected:" << errbuf
                                  << ", trying to flush decoder buffers (attempt" << refFrameErrorCount << ")";
                        
                        // 刷新解码器：清空内部缓冲区，丢弃损坏的参考帧
                        avcodec_flush_buffers(codecCtx_);
                        
                        // 注意：刷新后，当前数据包应该被丢弃，等待下一个关键帧
                        // 返回 nullptr 让调用者知道需要跳过这个数据包
                        return nullptr;
                    } else if (refFrameErrorCount == 4) {
                        qWarning() << "[HardwareDecoder::decode] Reference frame loss keeps happening,"
                                  << "please check file integrity or switch to software decoding";
                    }
                } else {
                    // 其他类型的错误
                    if (errorCount <= 5) {
                        qWarning() << "[HardwareDecoder::decode] Failed to send packet to decoder:" << errbuf
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
        return processDecodedFrame();
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
                qWarning() << "[HardwareDecoder::decode] Reference frame loss detected while receiving frame:" << errbuf
                          << ", flushing decoder buffers (attempt" << receiveRefErrorCount << ")";
                // 刷新解码器缓冲区
                avcodec_flush_buffers(codecCtx_);
            }
        } else {
            qWarning() << "[HardwareDecoder::decode] Failed to receive decoded frame:" << errbuf;
        }
        return nullptr;
    }
    
    // 不应该到达这里
    return nullptr;
#else
    return nullptr;
#endif
}

AVFrame* HardwareDecoder::flush() {
#ifdef HAS_FFMPEG
    // 发送空数据包以刷新解码器
    return decode(nullptr);
#else
    return nullptr;
#endif
}

std::string HardwareDecoder::getName() const {
    return "HardwareDecoder[" + toString(accelType_) + "]";
}

AVFrame* HardwareDecoder::transferToCPU(AVFrame* hwFrame) {
#ifdef HAS_FFMPEG
    if (!hwFrame) {
        return nullptr;
    }
    
    // 检查是否真的是硬件帧
    if (hwFrame->hw_frames_ctx == nullptr) {
        // 不是硬件帧，直接返回
        return hwFrame;
    }
    
    if (!cpuFrame_) {
        cpuFrame_ = av_frame_alloc();
        if (!cpuFrame_) {
            qWarning() << "[HardwareDecoder::transferToCPU] Failed to allocate CPU frame";
            return nullptr;
        }
    }
    
    // ==================== 关键：硬件帧参考帧管理 ====================
    // 硬件解码器需要硬件帧作为参考帧，而不是 CPU 帧
    // 如果我们在传输到 CPU 后立即释放硬件帧，解码器可能找不到参考帧
    // 
    // 解决方案：在传输到 CPU 之前，先增加硬件帧的引用计数
    // 这样即使 cpuFrame_ 被重用，硬件帧也不会被释放
    // 硬件帧会一直保持，直到所有引用都被释放
    
    // 在重用前取消引用 cpuFrame 以释放旧数据（对 FFmpeg 7.1 很重要）
    // 注意：这里只释放 CPU 帧的引用，硬件帧的引用由解码器内部管理
    av_frame_unref(cpuFrame_);
    
    // ==================== 保持10bit精度 ====================
    // 如果硬件输出10bit格式（P010），保持10bit精度而不是降级到8bit
    // av_hwframe_transfer_data 会根据硬件格式自动选择合适的CPU格式：
    // - P010 (硬件) -> YUV420P10LE (CPU)
    // - NV12 (硬件) -> YUV420P (CPU)
    // 我们显式设置目标格式以确保精度不丢失
    
    AVPixelFormat hwFormat = static_cast<AVPixelFormat>(hwFrame->format);
    if (hwFormat == AV_PIX_FMT_P010) {
        // 10bit格式：保持10bit精度
        cpuFrame_->format = AV_PIX_FMT_YUV420P10LE;
        qDebug() << "[HardwareDecoder::transferToCPU] Detected 10-bit format, keeping precision: P010 -> YUV420P10LE";
    }
    // 其他格式让 av_hwframe_transfer_data 自动选择（通常是8bit）
    
    // 将数据从 GPU 传输到 CPU
    // 注意：av_hwframe_transfer_data 会自动设置 format、width、height
    // 如果上面设置了format，它会使用该格式；否则自动选择
    int ret = av_hwframe_transfer_data(cpuFrame_, hwFrame, 0);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        qWarning() << "[HardwareDecoder::transferToCPU] Failed to transfer hardware frame to CPU:" << errbuf;
        return nullptr;
    }
    
    // 从硬件帧复制元数据属性到 CPU 帧
    // 注意：av_frame_copy_props() 复制 pts、side data 等，但不复制 width/height
    // width/height 已由 av_hwframe_transfer_data() 设置
    ret = av_frame_copy_props(cpuFrame_, hwFrame);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        qWarning() << "[HardwareDecoder::transferToCPU] Failed to copy frame properties:" << errbuf;
        // 无论如何继续，数据已经传输
    }
    
    // 验证帧属性是否正确设置（FFmpeg 7.1）
    static bool formatLogged = false;
    if (!formatLogged) {
        AVPixelFormat hwPixFmt = static_cast<AVPixelFormat>(hwFrame->format);
        AVPixelFormat cpuPixFmt = static_cast<AVPixelFormat>(cpuFrame_->format);
        
        qInfo() << "[HardwareDecoder::transferToCPU] === Hardware frame transfer completed (FFmpeg 7.1) ===";
        qInfo() << "[HardwareDecoder::transferToCPU]   Hardware format:"
                << av_get_pix_fmt_name(hwPixFmt)
                << "  CPU 格式:" << av_get_pix_fmt_name(cpuPixFmt);
        qInfo() << "[HardwareDecoder::transferToCPU]   Hardware size:" << hwFrame->width << "x" << hwFrame->height
                << "  CPU size:" << cpuFrame_->width << "x" << cpuFrame_->height;
        qInfo() << "[HardwareDecoder::transferToCPU]   PTS: hardware=" << hwFrame->pts << " CPU=" << cpuFrame_->pts;
        
        // 检测10bit格式
        bool is10bit = (hwPixFmt == AV_PIX_FMT_P010) || 
                       (cpuPixFmt == AV_PIX_FMT_YUV420P10LE) ||
                       (cpuPixFmt == AV_PIX_FMT_YUV420P10BE) ||
                       (cpuPixFmt == AV_PIX_FMT_YUV422P10LE) ||
                       (cpuPixFmt == AV_PIX_FMT_YUV422P10BE) ||
                       (cpuPixFmt == AV_PIX_FMT_YUV444P10LE) ||
                       (cpuPixFmt == AV_PIX_FMT_YUV444P10BE);
        
        if (is10bit) {
            qInfo() << "[HardwareDecoder::transferToCPU]   10-bit format preserved, color precision retained";
        } else {
            qInfo() << "[HardwareDecoder::transferToCPU]   8-bit format (standard precision)";
        }
        
        // 验证关键字段
        if (cpuFrame_->format < 0 || cpuFrame_->format == AV_PIX_FMT_NONE) {
            qWarning() << "[HardwareDecoder::transferToCPU] Warning: invalid CPU frame format!";
        }
        if (cpuFrame_->width <= 0 || cpuFrame_->height <= 0) {
            qWarning() << "[HardwareDecoder::transferToCPU] Warning: invalid CPU frame size!";
        }
        
        formatLogged = true;
    }
    
    // ==================== 高效返回帧的引用以支持所有权转移 ====================
    // 内部重用 cpuFrame_ 缓冲区，使用引用计数而非深拷贝
    // FFmpeg 的帧使用 AVBufferRef 引用计数，av_frame_ref() 只增加引用计数，不拷贝像素数据
    // 
    // 关键：CPU 帧的数据缓冲区是从硬件帧传输过来的，但它们是独立的缓冲区
    // 硬件帧的引用由解码器内部管理（作为参考帧），CPU 帧的引用由队列管理
    // 这样即使硬件帧被重用，CPU 帧的数据也不会丢失
    // 
    // 引用计数机制确保：
    // 1. 当解码器下次调用 av_frame_unref(cpuFrame_) 时，CPU 帧数据不会被释放
    //    因为队列中的帧仍在引用 CPU 帧的数据缓冲区
    // 2. 硬件帧的引用由解码器内部管理，不会被过早释放
    // 3. 只有当队列释放帧时，CPU 帧数据才会真正释放
    AVFrame* refFrame = av_frame_alloc();
    if (!refFrame) {
        qWarning() << "[HardwareDecoder::transferToCPU] Failed to allocate AVFrame";
        return nullptr;
    }
    
    ret = av_frame_ref(refFrame, cpuFrame_);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        qWarning() << "[HardwareDecoder::transferToCPU] Failed to reference CPU frame:" << errbuf;
        av_frame_free(&refFrame);
        return nullptr;
    }
    
    // 调用者获得引用的所有权，负责释放（释放时会自动减少引用计数）
    // 注意：CPU 帧的数据是独立的，不依赖于硬件帧的生命周期
    return refFrame;
#else
    return hwFrame;
#endif
}

AVFrame* HardwareDecoder::processDecodedFrame() {
#ifdef HAS_FFMPEG
    if (!frame_) {
        return nullptr;
    }
    
    // ===== 硬件帧处理策略 =====
    // CPU 下载决策下沉到渲染端（VideoOutput/OpenGLVideoConverter）：
    // - 解码器只负责产出帧，不在此处决定是否执行 CPU 下载
    // - 渲染端优先尝试零拷贝映射，失败后再执行 CPU 下载回退
    // 这样可以让策略集中在渲染路径，避免解码与渲染重复决策
    if (frame_->hw_frames_ctx != nullptr) {
        // 仅在第一次检测到硬件帧时输出日志（避免日志泛滥）
        static bool firstHwFrameDetected = false;
        if (!firstHwFrameDetected) {
            qInfo() << "[HardwareDecoder::processDecodedFrame] Hardware-decoded frame detected, format:"
                    << av_get_pix_fmt_name(static_cast<AVPixelFormat>(frame_->format))
                    << ", handing off to renderer for zero-copy/CPU-download decision";
            firstHwFrameDetected = true;
        }
    }
    
    // ==================== 高效返回帧的引用以支持所有权转移 ====================
    // 统一对硬件帧/软件帧使用引用计数而非深拷贝（解码器内部重用 frame_ 缓冲区）
    // FFmpeg 的帧使用 AVBufferRef 引用计数，av_frame_ref() 只增加引用计数，不拷贝像素数据
    // 引用计数机制确保：当解码器下次调用 av_frame_unref(frame_) 时，数据不会被释放
    // 因为队列中的帧仍在引用数据缓冲区，只有当队列释放帧时，数据才会真正释放
    AVFrame* refFrame = av_frame_alloc();
    if (!refFrame) {
        qWarning() << "[HardwareDecoder::processDecodedFrame] Failed to allocate AVFrame";
        return nullptr;
    }
    
    int ret = av_frame_ref(refFrame, frame_);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        qWarning() << "[HardwareDecoder::processDecodedFrame] Failed to reference software-decoded frame:" << errbuf;
        av_frame_free(&refFrame);
        return nullptr;
    }
    
    // 调用者获得引用的所有权，负责释放（释放时会自动减少引用计数）
    return refFrame;
#else
    return nullptr;
#endif
}

std::vector<HardwareAccelType> HardwareDecoder::detectAvailableAccelerators() {
    std::vector<HardwareAccelType> accelerators;
    
#ifdef HAS_FFMPEG
    // 优先检测 CUDA（NVIDIA 显卡），如果可用则直接返回，避免不必要的尝试
    AVBufferRef* cudaTestCtx = nullptr;
    int cudaRet = av_hwdevice_ctx_create(&cudaTestCtx, AV_HWDEVICE_TYPE_CUDA, nullptr, nullptr, 0);
    if (cudaRet >= 0) {
        accelerators.push_back(HardwareAccelType::NVDEC);
        av_buffer_unref(&cudaTestCtx);
        qInfo() << "[HardwareDecoder::detectAvailableAccelerators] CUDA/NVDEC available, prioritized and skipping other accelerators";
        return accelerators;  // 找到 CUDA 后直接返回，不再检测其他类型
    } else {
        qDebug() << "[HardwareDecoder::detectAvailableAccelerators] CUDA unavailable, continue checking other accelerators";
    }
    
    // FFmpeg 7.1 遍历所有可用的硬件加速类型
    enum AVHWDeviceType type = AV_HWDEVICE_TYPE_NONE;
    
    while ((type = av_hwdevice_iterate_types(type)) != AV_HWDEVICE_TYPE_NONE) {
        // 跳过已检测的 CUDA
        if (type == AV_HWDEVICE_TYPE_CUDA) {
            continue;
        }
        
        // 跳过明显不匹配的平台特定类型，避免不必要的尝试和错误日志
#ifdef PLATFORM_WINDOWS
        // Windows 上：跳过 VAAPI（Linux 专用）、VideoToolbox（macOS 专用）
        if (type == AV_HWDEVICE_TYPE_VAAPI || type == AV_HWDEVICE_TYPE_VIDEOTOOLBOX) {
            continue;
        }
#elif defined(PLATFORM_LINUX)
        // Linux 上：跳过 D3D11VA、D3D12VA、DXVA2（Windows 专用）、VideoToolbox（macOS 专用）
        if (type == AV_HWDEVICE_TYPE_D3D11VA || type == AV_HWDEVICE_TYPE_D3D12VA || 
            type == AV_HWDEVICE_TYPE_DXVA2 || type == AV_HWDEVICE_TYPE_VIDEOTOOLBOX) {
            continue;
        }
#elif defined(PLATFORM_MACOS)
        // macOS 上：跳过 VAAPI（Linux 专用）、D3D11VA、D3D12VA、DXVA2（Windows 专用）
        if (type == AV_HWDEVICE_TYPE_VAAPI || type == AV_HWDEVICE_TYPE_D3D11VA || 
            type == AV_HWDEVICE_TYPE_D3D12VA || type == AV_HWDEVICE_TYPE_DXVA2) {
            continue;
        }
#endif
        const char* typeName = av_hwdevice_get_type_name(type);
        qDebug() << "[HardwareDecoder::detectAvailableAccelerators] Detected hardware acceleration type:" << typeName;
        
        // 将 FFmpeg 类型映射到我们的枚举
        HardwareAccelType accelType = HardwareAccelType::None;
        
        if (type == AV_HWDEVICE_TYPE_D3D11VA) {
            accelType = HardwareAccelType::D3D11VA;
        } else if (type == AV_HWDEVICE_TYPE_D3D12VA) {
            accelType = HardwareAccelType::D3D12VA;
        } else if (type == AV_HWDEVICE_TYPE_DXVA2) {
            accelType = HardwareAccelType::DXVA2;
        } else if (type == AV_HWDEVICE_TYPE_VAAPI) {
            accelType = HardwareAccelType::VAAPI;
        } else if (type == AV_HWDEVICE_TYPE_VIDEOTOOLBOX) {
            accelType = HardwareAccelType::VideoToolbox;
        } else if (type == AV_HWDEVICE_TYPE_CUDA) {
            // CUDA 可以同时映射到 NVDEC 和 CUDA
            // 优先使用 NVDEC（更通用的名称），但也可以单独支持 CUDA
            accelType = HardwareAccelType::NVDEC;
        } else if (type == AV_HWDEVICE_TYPE_QSV) {
            accelType = HardwareAccelType::QSV;
        } else if (type == AV_HWDEVICE_TYPE_VDPAU) {
            // VDPAU 是 Linux 上的旧 NVIDIA 加速，已过时，记录但不支持
            qDebug() << "[HardwareDecoder::detectAvailableAccelerators] VDPAU is deprecated, CUDA/NVDEC is recommended";
        } else if (type == AV_HWDEVICE_TYPE_DRM) {
            // DRM 主要用于显示，不是视频解码
            qDebug() << "[HardwareDecoder::detectAvailableAccelerators] DRM is mainly for display, not video decoding";
        } else if (type == AV_HWDEVICE_TYPE_OPENCL) {
            // OpenCL 主要用于通用 GPU 计算，不是视频解码
            qDebug() << "[HardwareDecoder::detectAvailableAccelerators] OpenCL is mainly for compute, not video decoding";
        } else if (type == AV_HWDEVICE_TYPE_MEDIACODEC) {
            // MediaCodec 是 Android 专用
            qDebug() << "[HardwareDecoder::detectAvailableAccelerators] MediaCodec is Android-only, unsupported on current platform";
        } else if (type == AV_HWDEVICE_TYPE_VULKAN) {
            // Vulkan 主要用于渲染，不是视频解码
            qDebug() << "[HardwareDecoder::detectAvailableAccelerators] Vulkan is mainly for rendering, not video decoding";
        }
        
        // 对于已映射的硬件加速类型，尝试创建设备以验证是否真的可用
        // 让 FFmpeg 的运行时检查来决定硬件是否可用，而不是依赖编译时检查
        if (accelType != HardwareAccelType::None) {
            AVBufferRef* testCtx = nullptr;
            int ret = av_hwdevice_ctx_create(&testCtx, type, nullptr, nullptr, 0);
            if (ret >= 0) {
                accelerators.push_back(accelType);
                av_buffer_unref(&testCtx);
                qInfo() << "[HardwareDecoder::detectAvailableAccelerators] Hardware acceleration available:" << typeName;
            } else {
                char errbuf[AV_ERROR_MAX_STRING_SIZE];
                av_strerror(ret, errbuf, sizeof(errbuf));
                qDebug() << "[HardwareDecoder::detectAvailableAccelerators] Hardware acceleration" << typeName << "is unavailable on this system:" << errbuf;
            }
        }
    }
#else
    // FFmpeg 不可用时返回空列表
    qWarning() << "[HardwareDecoder::detectAvailableAccelerators] FFmpeg is not enabled, cannot detect hardware acceleration";
#endif
    
    return accelerators;
}

HardwareAccelType HardwareDecoder::recommendBestAccelerator(const std::vector<HardwareAccelType>& availableAccelerators) {
    if (availableAccelerators.empty()) {
        qInfo() << "[HardwareDecoder::recommendBestAccelerator] No hardware acceleration available";
        return HardwareAccelType::None;
    }
    
    // 根据平台推荐最佳选项（使用平台特定的条件编译）
#ifdef PLATFORM_WINDOWS
    // Windows 平台：对于 NVIDIA GPU，优先使用 NVDEC 以获得最佳性能
    qDebug() << "[HardwareDecoder::recommendBestAccelerator] Windows platform, checking NVIDIA NVDEC first";
    for (auto type : availableAccelerators) {
        if (type == HardwareAccelType::NVDEC) {
            qInfo() << "[HardwareDecoder::recommendBestAccelerator] Recommended: NVDEC (best for NVIDIA GPU)";
            return type;
        }
    }
    
    // 然后尝试 D3D12VA（Windows 10+，最新技术）
    qDebug() << "[HardwareDecoder::recommendBestAccelerator] NVDEC unavailable, trying D3D12VA";
    for (auto type : availableAccelerators) {
        if (type == HardwareAccelType::D3D12VA) {
            qInfo() << "[HardwareDecoder::recommendBestAccelerator] Recommended: D3D12VA";
            return type;
        }
    }
    
    // 然后尝试 D3D11VA（Windows 8+）
    qDebug() << "[HardwareDecoder::recommendBestAccelerator] D3D12VA unavailable, trying D3D11VA";
    for (auto type : availableAccelerators) {
        if (type == HardwareAccelType::D3D11VA) {
            qInfo() << "[HardwareDecoder::recommendBestAccelerator] Recommended: D3D11VA";
            return type;
        }
    }
    
    // 最后尝试 DXVA2
    for (auto type : availableAccelerators) {
        if (type == HardwareAccelType::DXVA2) {
            qInfo() << "[HardwareDecoder::recommendBestAccelerator] Recommended: DXVA2";
            return type;
        }
    }
#endif

#ifdef PLATFORM_MACOS
    // macOS 平台：VideoToolbox 是最佳选择
    qDebug() << "[HardwareDecoder::recommendBestAccelerator] macOS platform, recommending VideoToolbox";
    for (auto type : availableAccelerators) {
        if (type == HardwareAccelType::VideoToolbox) {
            qInfo() << "[HardwareDecoder::recommendBestAccelerator] Recommended: VideoToolbox";
            return type;
        }
    }
#endif

#ifdef PLATFORM_LINUX
    // Linux 平台：优先使用 VAAPI
    qDebug() << "[HardwareDecoder::recommendBestAccelerator] Linux platform, recommending VAAPI";
    for (auto type : availableAccelerators) {
        if (type == HardwareAccelType::VAAPI) {
            qInfo() << "[HardwareDecoder::recommendBestAccelerator] Recommended: VAAPI";
            return type;
        }
    }
#endif
    
    // 跨平台：如果 NVIDIA GPU 可用，NVDEC 是不错的选择
    for (auto type : availableAccelerators) {
        if (type == HardwareAccelType::NVDEC) {
            qInfo() << "[HardwareDecoder::recommendBestAccelerator] Recommended: NVDEC";
            return type;
        }
    }
    
    // 跨平台：Intel QSV 也得到广泛支持
    for (auto type : availableAccelerators) {
        if (type == HardwareAccelType::QSV) {
            qInfo() << "[HardwareDecoder::recommendBestAccelerator] Recommended: QSV";
            return type;
        }
    }
    
    // 返回第一个可用的
    qInfo() << "[HardwareDecoder::recommendBestAccelerator] Using first available hardware accelerator:" << toString(availableAccelerators[0]).c_str();
    return availableAccelerators[0];
}

HardwareAccelType HardwareDecoder::getRecommendedAccelerator(int codecId) {
#ifdef HAS_FFMPEG
    Q_UNUSED(codecId);  // 当前未使用，保留用于编解码器特定优化
    // 获取所有可用的硬件加速
    auto available = detectAvailableAccelerators();
    return recommendBestAccelerator(available);
#else
    Q_UNUSED(codecId);
    qWarning() << "[HardwareDecoder::getRecommendedAccelerator] FFmpeg is not enabled, cannot recommend hardware acceleration";
    return HardwareAccelType::None;
#endif
}

enum AVPixelFormat HardwareDecoder::getHwFormat(AVCodecContext* ctx, const enum AVPixelFormat* pix_fmts) {
#ifdef HAS_FFMPEG
    // 从 opaque 获取 HardwareDecoder 实例
    auto* decoder = static_cast<HardwareDecoder*>(ctx->opaque);
    if (!decoder) {
        qWarning() << "[HardwareDecoder::getHwFormat] Failed to get HardwareDecoder instance";
        return AV_PIX_FMT_NONE;
    }
    
    // 根据硬件加速类型确定目标格式
    enum AVPixelFormat targetFormat = AV_PIX_FMT_NONE;
    
    switch (decoder->accelType_) {
        case HardwareAccelType::D3D11VA:
            targetFormat = AV_PIX_FMT_D3D11;
            break;
        case HardwareAccelType::D3D12VA:
            targetFormat = AV_PIX_FMT_D3D12;
            break;
        case HardwareAccelType::DXVA2:
            targetFormat = AV_PIX_FMT_DXVA2_VLD;
            break;
        case HardwareAccelType::VAAPI:
            targetFormat = AV_PIX_FMT_VAAPI;
            break;
        case HardwareAccelType::VideoToolbox:
            targetFormat = AV_PIX_FMT_VIDEOTOOLBOX;
            break;
        case HardwareAccelType::NVDEC:
        case HardwareAccelType::CUDA:
            // NVDEC 和 CUDA 使用相同的像素格式
            targetFormat = AV_PIX_FMT_CUDA;
            break;
        case HardwareAccelType::QSV:
            targetFormat = AV_PIX_FMT_QSV;
            break;
        case HardwareAccelType::None:
            // None 不应该到达这里
            qWarning() << "[HardwareDecoder::getHwFormat] Invalid hardware acceleration type (None)";
            return AV_PIX_FMT_NONE;
        default:
            qWarning() << "[HardwareDecoder::getHwFormat] Unknown hardware acceleration type";
            return AV_PIX_FMT_NONE;
    }
    
    // ==================== 优先选择10bit格式（P010）====================
    // 对于支持10bit的硬件解码器，优先选择P010格式以保持色彩精度
    // P010是10bit版本的NV12格式，广泛支持于现代GPU（NVDEC/D3D11VA/QSV等）
    
    enum AVPixelFormat preferred10bitFormat = AV_PIX_FMT_NONE;
    enum AVPixelFormat fallback8bitFormat = AV_PIX_FMT_NONE;
    
    // 第一遍遍历：查找10bit和8bit格式
    for (const enum AVPixelFormat* p = pix_fmts; *p != AV_PIX_FMT_NONE; p++) {
        // 优先选择P010（10bit NV12）
        if (*p == AV_PIX_FMT_P010) {
            preferred10bitFormat = AV_PIX_FMT_P010;
        }
        // 记录目标硬件格式（8bit，如NV12）
        if (*p == targetFormat) {
            fallback8bitFormat = *p;
        }
    }
    
    // 优先返回10bit格式
    if (preferred10bitFormat != AV_PIX_FMT_NONE) {
        qInfo() << "[HardwareDecoder::getHwFormat] Selected 10-bit hardware pixel format:"
                << av_get_pix_fmt_name(preferred10bitFormat);
        return preferred10bitFormat;
    }
    
    // 回退到8bit格式
    if (fallback8bitFormat != AV_PIX_FMT_NONE) {
        qDebug() << "[HardwareDecoder::getHwFormat] Selected 8-bit hardware pixel format:"
                 << av_get_pix_fmt_name(fallback8bitFormat);
        return fallback8bitFormat;
    }
    
    qWarning() << "[HardwareDecoder::getHwFormat] No matching hardware pixel format found";
#endif
    return AV_PIX_FMT_NONE;
}

} // namespace AdvancedPlayer

