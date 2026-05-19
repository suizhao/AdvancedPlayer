#ifndef HARDWAREDECODER_H
#define HARDWAREDECODER_H

#include "DecoderInterface.h"
#include <vector>

#ifdef HAS_FFMPEG
extern "C" {
#include <libavutil/pixfmt.h>
}
#endif

struct AVCodecContext;
struct AVBufferRef;
struct AVFrame;
struct AVPacket;

namespace AdvancedPlayer {

/**
 * @brief 硬件解码器实现
 * 
 * 支持多种 GPU 硬件加速解码：
 * - Windows: D3D11VA, D3D12VA, DXVA2, NVDEC, QSV
 * - macOS: VideoToolbox
 * - Linux: VAAPI, NVDEC
 */
class HardwareDecoder : public IDecoder {
public:
    /**
     * @brief 构造函数
     * @param codecCtx FFmpeg 解码器上下文
     * @param accelType 硬件加速类型
     */
    HardwareDecoder(AVCodecContext* codecCtx, HardwareAccelType accelType);
    
    /**
     * @brief 析构函数
     */
    ~HardwareDecoder() override;
    
    // 禁用拷贝
    HardwareDecoder(const HardwareDecoder&) = delete;
    HardwareDecoder& operator=(const HardwareDecoder&) = delete;
    
    bool initialize(AVDictionary** options = nullptr) override;
    AVFrame* decode(const AVPacket* packet) override;
    AVFrame* flush() override;
    bool isHardwareAccelerated() const override { return true; }
    std::string getName() const override;
    
    /**
     * @brief 将硬件帧传输到 CPU 内存
     * @param hwFrame 硬件帧
     * @return CPU 帧，失败时返回 nullptr
     */
    AVFrame* transferToCPU(AVFrame* hwFrame);
    
    /**
     * @brief 检测系统支持的硬件加速类型
     * @return 支持的硬件加速类型列表
     */
    static std::vector<HardwareAccelType> detectAvailableAccelerators();
    
    /**
     * @brief 获取推荐的硬件加速类型
     * @param codecId 编解码器 ID
     * @return 推荐的硬件加速类型
     */
    static HardwareAccelType getRecommendedAccelerator(int codecId);
    
    /**
     * @brief 从可用硬件加速器列表中推荐最佳选项
     * @param availableAccelerators 可用硬件加速器列表
     * @return 推荐的硬件加速类型
     */
    static HardwareAccelType recommendBestAccelerator(
        const std::vector<HardwareAccelType>& availableAccelerators);
    
private:
    AVCodecContext* codecCtx_{nullptr};
    AVBufferRef* hwDeviceCtx_{nullptr};
    AVBufferRef* hwFramesCtx_{nullptr};
    HardwareAccelType accelType_{};
    AVFrame* frame_{nullptr};
    AVFrame* cpuFrame_{nullptr};
    
    /**
     * @brief 初始化硬件设备上下文
     * @return 成功时返回 true
     */
    bool initHardwareDevice();
    
    /**
     * @brief 硬件解码回调函数
     */
    static enum AVPixelFormat getHwFormat(AVCodecContext* ctx, const enum AVPixelFormat* pix_fmts);
    
    /**
     * @brief 处理已解码的帧（硬件帧传输、引用计数等）
     * @return 处理后的帧引用，失败时返回 nullptr
     */
    AVFrame* processDecodedFrame();
};

// 使用 C++20 static_assert 验证 HardwareDecoder 满足 Decoder 概念
static_assert(Decoder<HardwareDecoder>, 
              "HardwareDecoder 必须满足 Decoder 概念");

} // namespace AdvancedPlayer

#endif // HARDWAREDECODER_H

