#include "ScreenshotCapture.h"
#include <QDateTime>
#include <QDir>
#include <QDebug>
#include <QImageWriter>
#include <QFuture>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>


#ifdef HAS_FFMPEG
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}
#endif

namespace AdvancedPlayer {

ScreenshotCapture::ScreenshotCapture(QObject* parent)
    : QObject(parent) {
    qInfo() << "[ScreenshotCapture::ScreenshotCapture] ScreenshotCapture created";
}

QFuture<bool> ScreenshotCapture::captureFrameAsync(AVFrame* frame,
                                                   const QString& outputPath,
                                                   ImageFormat format,
                                                   int quality) {
    if (!frame) {
        qWarning() << "[ScreenshotCapture::captureFrameAsync] Input frame is null";
        emit screenshotFailed("Failed to get valid frame");
        return QtFuture::makeReadyValueFuture(false);
    }

    QFuture<bool> future = QtConcurrent::run([frame, outputPath, format, quality]() mutable {
        QImage image = ScreenshotCapture::avFrameToQImage(frame);

        bool success = false;
        if (!image.isNull()) {
            success = ScreenshotCapture::saveImage(image, outputPath, format, quality);
        } else {
            qWarning() << "[ScreenshotCapture::captureFrameAsync] Failed to convert frame to image";
        }

#ifdef HAS_FFMPEG
        av_frame_free(&frame);
#endif
        return success;
    });

    auto* watcher = new QFutureWatcher<bool>(this);
    connect(watcher, &QFutureWatcher<bool>::finished, this, [this, watcher, outputPath]() {
        bool success = watcher->result();
        watcher->deleteLater();

        if (success) {
            emit screenshotSaved(outputPath);
            qInfo() << "[ScreenshotCapture] Screenshot saved:" << outputPath;
        } else {
            emit screenshotFailed("Failed to save screenshot");
            qWarning() << "[ScreenshotCapture] Failed to save screenshot:" << outputPath;
        }
    });
    watcher->setFuture(future);

    return future;
}

bool ScreenshotCapture::saveImage(const QImage& image,
                                  const QString& outputPath,
                                  ImageFormat format,
                                  int quality) {
    // 映射图片格式
    QByteArray formatStr;
    switch (format) {
        case ImageFormat::PNG:
            formatStr = "png";
            break;
        case ImageFormat::JPEG:
            formatStr = "jpg";
            break;
        case ImageFormat::BMP:
            formatStr = "bmp";
            break;
    }

    auto writeWithFormat = [&]() -> bool {
        QImageWriter writer(outputPath, formatStr);
        if (format == ImageFormat::JPEG) {
            writer.setQuality(quality);
        }
        const bool ok = writer.write(image);
        if (!ok) {
            qWarning() << "[ScreenshotCapture::saveImage] Write failed - path:" << outputPath
                       << ", format:" << formatStr
                       << ", error:" << writer.errorString();
        }else{
            qInfo() << "[ScreenshotCapture::saveImage] Write succeeded - path:" << outputPath
                       << ", format:" << formatStr;
        }
        return ok;
    };

    return writeWithFormat();
}

QImage ScreenshotCapture::avFrameToQImage(AVFrame* frame) {
#ifdef HAS_FFMPEG
    if (!frame || frame->width <= 0 || frame->height <= 0) {
        qWarning() << "[ScreenshotCapture::avFrameToQImage] Invalid AVFrame";
        return QImage();
    }

    int width = frame->width;
    int height = frame->height;
    AVPixelFormat srcPixFmt = static_cast<AVPixelFormat>(frame->format);

    qDebug() << "[ScreenshotCapture::avFrameToQImage] Converting AVFrame to QImage:" << width << "x" << height
             << " AVFrame format:" << av_get_pix_fmt_name(srcPixFmt);

    // 转换到 RGB24，兼容 FFmpeg 7.1
    SwsContext* swsCtx = sws_getContext(
        width, height, srcPixFmt,
        width, height, AV_PIX_FMT_RGB24,
        SWS_BILINEAR,  // 平衡质量与性能
        nullptr, nullptr, nullptr
    );

    if (!swsCtx) {
        qWarning() << "[ScreenshotCapture::avFrameToQImage] Failed to create SwsContext";
        return QImage();
    }

    // 明确设置色彩空间，减少 deprecated pixel format 警告
    int srcRange = (frame->color_range == AVCOL_RANGE_JPEG) ? 1 : 0;// 1=jpeg / 0=mpeg
    sws_setColorspaceDetails(swsCtx,
                            sws_getCoefficients(SWS_CS_DEFAULT), srcRange,
                            sws_getCoefficients(SWS_CS_DEFAULT), 1,
                            0, 1 << 16, 1 << 16);

    // 创建 QImage，RGB888 与目标 RGB24 对应
    QImage image(width, height, QImage::Format_RGB888);
    if (image.isNull()) {
        qWarning() << "[ScreenshotCapture::avFrameToQImage] Failed to create QImage";
        sws_freeContext(swsCtx);
        return QImage();
    }

    // 目标缓冲区
    uint8_t* dstData[4] = {image.bits(), nullptr, nullptr, nullptr};
    int dstLinesize[4] = {static_cast<int>(image.bytesPerLine()), 0, 0, 0};

    // 执行像素格式转换
    int ret = sws_scale(
        swsCtx,
        const_cast<const uint8_t**>(frame->data),
        frame->linesize,
        0,  // 起始行
        height,  // 转换行数
        dstData,
        dstLinesize
    );

    sws_freeContext(swsCtx);

    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        qWarning() << "[ScreenshotCapture::avFrameToQImage] sws_scale conversion failed:" << errbuf;
        return QImage();
    }

    return image;

#else
    qWarning() << "[ScreenshotCapture::avFrameToQImage] FFmpeg is not enabled, cannot convert AVFrame";
    // 返回占位图，避免调用方空指针处理复杂化
    QImage placeholder(f1920,
                       1080,
                       QImage::Format_RGB888);
    placeholder.fill(Qt::black);
    return placeholder;
#endif
}

} // namespace AdvancedPlayer

