#ifndef DECODERINTERFACE_H
#define DECODERINTERFACE_H

#include <memory>
#include <string>
#include <concepts>

struct AVFrame;
struct AVPacket;
struct AVCodecContext;
struct AVDictionary;

namespace AdvancedPlayer {

/**
 * @brief C++20 概念约束解码器接口
 * 
 * 定义解码器必须实现的操作以进行编译时类型检查
 */
template<typename T>
concept Decoder = requires(T decoder, const AVPacket* packet, AVDictionary** options) {
    // 必须能够初始化
    { decoder.initialize(options) } -> std::same_as<bool>;
    
    // 必须能够解码数据包
    { decoder.decode(packet) } -> std::same_as<AVFrame*>;
    
    // 必须能够刷新缓冲区
    { decoder.flush() } -> std::same_as<AVFrame*>;
    
    // 必须能够查询硬件加速
    { decoder.isHardwareAccelerated() } -> std::same_as<bool>;
    
    // 必须能够获取名称
    { decoder.getName() } -> std::convertible_to<std::string>;
};

/**
 * @brief 解码器接口
 * 
 * 定义解码器的通用接口，支持硬件和软件解码
 * 使用 C++20 概念进行编译时类型约束
 */
class IDecoder {
public:
    virtual ~IDecoder() = default;
    
    /**
     * @brief 初始化解码器
     * @param options 解码器选项字典（可选，可以为 nullptr）
     * @return 成功时返回 true，失败时返回 false
     */
    virtual bool initialize(AVDictionary** options = nullptr) = 0;
    
    /**
     * @brief 解码数据包
     * @param packet 输入数据包
     * @return 解码后的帧，失败时返回 nullptr
     * @note 所有权管理：返回的帧由调用者拥有，调用者负责使用 av_frame_free() 释放
     */
    virtual AVFrame* decode(const AVPacket* packet) = 0;
    
    /**
     * @brief 刷新解码器缓冲区
     * @return 刷新后的帧，如果没有数据则返回 nullptr
     * @note 所有权管理：返回的帧由调用者拥有，调用者负责使用 av_frame_free() 释放
     */
    virtual AVFrame* flush() = 0;
    
    /**
     * @brief 检查是否为硬件加速解码
     * @return 硬件解码返回 true，软件解码返回 false
     */
    virtual bool isHardwareAccelerated() const = 0;
    
    /**
     * @brief 获取解码器名称
     * @return 解码器名称
     */
    virtual std::string getName() const = 0;
};

/**
 * @brief 硬件加速类型
 */
enum class HardwareAccelType {
    None,           // 无硬件加速（软件解码）
    DXVA2,          // Windows DirectX Video Acceleration 2
    D3D11VA,        // Windows Direct3D 11 Video Acceleration
    D3D12VA,        // Windows Direct3D 12 Video Acceleration
    NVDEC,          // NVIDIA NVDEC
    QSV,            // Intel Quick Sync Video
    VAAPI,          // Linux VA-API
    VideoToolbox,   // macOS VideoToolbox
    CUDA            // NVIDIA CUDA（与 NVDEC 使用相同的硬件设备）
};

/**
 * @brief 将硬件加速类型转换为字符串
 */
inline std::string toString(HardwareAccelType type) {
    switch (type) {
        case HardwareAccelType::None: return "None";
        case HardwareAccelType::DXVA2: return "DXVA2";
        case HardwareAccelType::D3D11VA: return "D3D11VA";
        case HardwareAccelType::D3D12VA: return "D3D12VA";
        case HardwareAccelType::NVDEC: return "NVDEC";
        case HardwareAccelType::QSV: return "QSV";
        case HardwareAccelType::VAAPI: return "VAAPI";
        case HardwareAccelType::VideoToolbox: return "VideoToolbox";
        case HardwareAccelType::CUDA: return "CUDA";
        default: return "Unknown";
    }
}

/**
 * @brief 使用 C++20 概念约束的解码器工厂函数
 * 
 * 创建解码器实例并验证其满足 Decoder 概念，返回基类指针以实现多态
 * @tparam T 解码器类型，必须满足 Decoder 概念并继承自 IDecoder
 * @param args 构造函数参数
 * @return 指向解码器基类的唯一指针
 */
template<Decoder T, typename... Args>
    requires std::constructible_from<T, Args...> && std::derived_from<T, IDecoder>
std::unique_ptr<IDecoder> createDecoder(Args&&... args) {
    return std::make_unique<T>(std::forward<Args>(args)...);
}

/**
 * @brief 使用概念约束的解码器初始化辅助函数
 * 
 * 初始化解码器实例并返回成功状态，提供统一的错误处理
 * @param decoder 解码器指针
 * @param decoderName 解码器名称，用于日志输出
 * @return 初始化是否成功
 */
inline bool initializeDecoder(std::unique_ptr<IDecoder>& decoder, const std::string& decoderName = "", AVDictionary** options = nullptr) {
    if (!decoder) {
        return false;
    }
    
    bool success = decoder->initialize(options);
    
    // 可以在此处添加统一的日志记录或错误处理
    if (!success && !decoderName.empty()) {
        // 处理初始化失败（具体逻辑由调用者处理）
    }
    
    return success;
}

} // namespace AdvancedPlayer

#endif // DECODERINTERFACE_H

