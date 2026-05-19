#ifndef VIDEOOUTPUT_H
#define VIDEOOUTPUT_H

#include <QQuickFramebufferObject>
#include <QSize>
#include <QMutex>

struct AVFrame;

namespace AdvancedPlayer {

/**
 * @brief Qt Quick 视频输出组件（OpenGL 渲染）
 * 
 * 使用 QQuickFramebufferObject + OpenGL 在 QML 中渲染视频
 * 
 * 渲染架构：
 * - 基于 OpenGL 3.3 Core Profile
 * - 使用 YUV420P → RGB 着色器转换（GPU 加速）
 * - 支持 PBO 双缓冲异步纹理上传（提升性能）
 * - 支持硬件解码帧的零拷贝映射（D3D11VA/VA-API/VideoToolbox）
 * 
 * 渲染流程：
 * 1. updateFrame() 接收 AVFrame（硬件/软件解码）
 * 2. VideoRenderer::synchronize() 在渲染线程同步帧数据
 * 3. VideoRenderer::processPendingFrame() 将帧转换为 OpenGL 纹理
 * 4. VideoRenderer::render() 使用着色器渲染纹理到 FBO
 * 5. Qt 场景图将 FBO 合成到最终画面
 * 
 * 性能特性：
 * - 硬件解码：优先零拷贝，失败时回退到 CPU 下载
 * - 软件解码：PBO 双缓冲异步上传，减少 CPU-GPU 传输延迟
 * - 支持多种填充模式（保持宽高比/裁剪/拉伸）
 */
class VideoOutput : public QQuickFramebufferObject {
    Q_OBJECT
    Q_PROPERTY(QSize videoSize READ videoSize NOTIFY videoSizeChanged)
    Q_PROPERTY(FillMode fillMode READ fillMode WRITE setFillMode NOTIFY fillModeChanged)
    
public:
    /**
     * @brief 填充模式
     */
    enum FillMode {
        PreserveAspectFit,   // 保持宽高比，可能有黑边
        PreserveAspectCrop,  // 保持宽高比，裁剪
        Stretch              // 拉伸填充
    };
    Q_ENUM(FillMode)
    
    explicit VideoOutput(QQuickItem* parent = nullptr);
    ~VideoOutput() override;
    
    Renderer* createRenderer() const override;

    /**
     * @brief 获取视频大小
     */
    QSize videoSize() const { return videoSize_; }

    /**
     * @brief 获取填充模式
     */
    FillMode fillMode() const { return fillMode_; }
    
    /**
     * @brief 更新视频帧
     * @param frame 视频帧（硬件或软件解码）
     */
    void updateFrame(AVFrame* frame);
    
    /**
     * @brief 设置填充模式
     */
    void setFillMode(FillMode mode);
    
    /**
     * @brief 清空待渲染的帧
     * @note 用于seek时清空旧帧，确保画面立即更新
     */
    void clearPendingFrame();

signals:
    void videoSizeChanged();
    void fillModeChanged();
    
private:
    QSize videoSize_{};
    FillMode fillMode_{PreserveAspectFit};
    
    // 线程安全的帧队列
    QMutex frameMutex_;
    AVFrame* pendingFrame_{nullptr};
    
    class VideoRenderer;
    friend class VideoRenderer;
};

} // namespace AdvancedPlayer

// 声明VideoOutput*为Qt元类型，让QML可以识别
Q_DECLARE_METATYPE(AdvancedPlayer::VideoOutput*)

#endif // VIDEOOUTPUT_H

