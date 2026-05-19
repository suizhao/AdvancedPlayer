#ifndef SCREENSHOTCAPTURE_H
#define SCREENSHOTCAPTURE_H

#include <QObject>
#include <QString>
#include <QImage>
#include <QFuture>

struct AVFrame;

namespace AdvancedPlayer {

/**
 * @brief 截图工具类
 *
 * 提供当前视频帧的异步截图保存能力
 */
class ScreenshotCapture : public QObject {
    Q_OBJECT

public:
    enum class ImageFormat {
        PNG,
        JPEG,
        BMP
    };
    Q_ENUM(ImageFormat)

    explicit ScreenshotCapture(QObject* parent = nullptr);
    ~ScreenshotCapture() = default;

    /**
     * @brief 异步保存视频帧
     * @param frame 输入视频帧
     * @param outputPath 输出路径
     * @param format 图片格式
     * @param quality 质量(1-100，仅 JPEG 生效)
     * @return 异步结果
     */
    QFuture<bool> captureFrameAsync(AVFrame* frame,
                                    const QString& outputPath,
                                    ImageFormat format = ImageFormat::PNG,
                                    int quality = 95);

signals:
    void screenshotSaved(const QString& path);
    void screenshotFailed(const QString& error);

private:

    /**
     * @brief 保存 QImage 到文件
     * @param image 输入图像
     * @param outputPath 输出路径
     * @param format 图片格式
     * @param quality 质量
     * @return 保存成功返回 true
     */
    static bool saveImage(const QImage& image,
                          const QString& outputPath,
                          ImageFormat format,
                          int quality);

    /**
     * @brief 将 AVFrame 转换为 QImage
     * @param frame 输入帧
     * @return QImage 图像
     */
    static QImage avFrameToQImage(AVFrame* frame);
};

} // namespace AdvancedPlayer

#endif // SCREENSHOTCAPTURE_H

