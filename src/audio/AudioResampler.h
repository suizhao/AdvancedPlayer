#ifndef AUDIORESAMPLER_H
#define AUDIORESAMPLER_H

#include <QObject>

// FFmpeg 前向声明
struct SwrContext;
struct AVFrame;

namespace AdvancedPlayer {

/**
 * @brief 音频重采样器类
 * 
 * 使用 FFmpeg 的 SwrContext 进行音频重采样和格式转换
 */
class AudioResampler {
public:
    /**
     * @brief 构造函数
     * @param srcRate 源采样率
     * @param srcChannels 源声道数
     * @param srcFormat 源格式
     * @param dstRate 目标采样率
     * @param dstChannels 目标声道数
     * @param dstFormat 目标格式
     */
    AudioResampler(int srcRate, int srcChannels, int srcFormat,
                   int dstRate, int dstChannels, int dstFormat);
    
    ~AudioResampler();
    
    /**
     * @brief 重采样音频帧
     * @param inputFrame 输入帧
     * @param outputData 输出数据指针
     * @return 输出数据大小
     */
    int resample(AVFrame* inputFrame, uint8_t** outputData);
    
    /**
     * @brief 刷新重采样器
     * @param outputData 输出数据指针
     * @return 输出数据大小
     */
    int flush(uint8_t** outputData);
    
    /**
     * @brief 重新配置重采样器（资源复用优化）
     * 
     * 如果格式相同，只重置内部状态（快速路径）；
     * 如果格式不同，销毁旧 context 并创建新的（慢速路径）。
     * 
     * @param srcRate 新的源采样率
     * @param srcChannels 新的源声道数
     * @param srcFormat 新的源格式
     * @param dstRate 新的目标采样率
     * @param dstChannels 新的目标声道数
     * @param dstFormat 新的目标格式
     * @return 成功时返回 true
     */
    bool reconfigure(int srcRate, int srcChannels, int srcFormat,
                     int dstRate, int dstChannels, int dstFormat);
    
    /**
     * @brief 检查是否已初始化
     * @return 如果重采样器已初始化返回 true
     */
    bool isInitialized() const { return swrCtx_ != nullptr; }
    
    /**
     * @brief 获取源采样率
     */
    int getSrcRate() const { return srcRate_; }
    
    /**
     * @brief 获取源声道数
     */
    int getSrcChannels() const { return srcChannels_; }
    
    /**
     * @brief 获取源格式
     */
    int getSrcFormat() const { return srcFormat_; }
    
private:
    SwrContext* swrCtx_{nullptr};
    
    // 源格式参数（用于复用检测）
    int srcRate_{0};
    int srcChannels_{0};
    int srcFormat_{0};
    
    // 目标格式参数
    int dstRate_;
    int dstChannels_;
    int dstFormat_;
};

} // namespace AdvancedPlayer

#endif // AUDIORESAMPLER_H

