#ifndef SOFTWAREDECODER_H
#define SOFTWAREDECODER_H

#include "DecoderInterface.h"

struct AVCodecContext;
struct AVFrame;
struct AVPacket;

namespace AdvancedPlayer {

/**
 * @brief 软件解码器实现
 * 
 * 使用 CPU 进行解码，当硬件解码失败时作为后备方案
 */
class SoftwareDecoder : public IDecoder {
public:
    /**
     * @brief 构造函数
     * @param codecCtx FFmpeg 解码器上下文
     */
    explicit SoftwareDecoder(AVCodecContext* codecCtx);
    
    /**
     * @brief 析构函数
     */
    ~SoftwareDecoder() override;
    
    // 禁用拷贝
    SoftwareDecoder(const SoftwareDecoder&) = delete;
    SoftwareDecoder& operator=(const SoftwareDecoder&) = delete;
    
    bool initialize(AVDictionary** options = nullptr) override;
    AVFrame* decode(const AVPacket* packet) override;
    AVFrame* flush() override;
    bool isHardwareAccelerated() const override { return false; }
    std::string getName() const override { return "SoftwareDecoder"; }
    
private:
    AVCodecContext* codecCtx_{nullptr};
    AVFrame* frame_{nullptr};
    
    /**
     * @brief 创建帧的引用（用于返回给调用者）
     * @return 帧引用，失败时返回 nullptr
     */
    AVFrame* createFrameReference();
};

// 使用 C++20 static_assert 验证 SoftwareDecoder 满足 Decoder 概念
static_assert(Decoder<SoftwareDecoder>, 
              "SoftwareDecoder 必须满足 Decoder 概念");

} // namespace AdvancedPlayer

#endif // SOFTWAREDECODER_H

