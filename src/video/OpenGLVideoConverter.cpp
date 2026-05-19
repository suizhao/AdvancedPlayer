#include "OpenGLVideoConverter.h"
#include <QDebug>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <cstring>  // memcpy, strstr, memset

#ifdef HAS_FFMPEG
extern "C" {
// 抑制 FFmpeg 弃用警告（key_frame 字段）
// 注意：我们使用 av_frame_copy_props() 来复制属性，避免直接访问已弃用字段
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4996)  // 禁用弃用警告
#endif
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
#include <libavutil/pixdesc.h>  // 用于 av_get_pix_fmt_name()
#include <libavutil/hwcontext.h>
#include <libswscale/swscale.h>
#ifdef __GNUC__
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
}
#endif

namespace AdvancedPlayer {

OpenGLVideoConverter::OpenGLVideoConverter() {
}

OpenGLVideoConverter::~OpenGLVideoConverter() {
    cleanupPBO();
    
    if (swsContext_) {
        sws_freeContext(swsContext_);
        swsContext_ = nullptr;
    }
    if (convertedFrame_) {
#ifdef HAS_FFMPEG
        av_frame_free(&convertedFrame_);
#endif
        convertedFrame_ = nullptr;
    }
}

bool OpenGLVideoConverter::initialize() {
    // 初始化 PBO 双缓冲（如果支持）
    return initializePBO();
}

bool OpenGLVideoConverter::isHardwareFrame(AVFrame* frame) {
#ifdef HAS_FFMPEG
    if (!frame) return false;
    
    // 检查是否为硬件帧：通过检查 hw_frames_ctx 或像素格式
    // 注意：hw_frames_ctx 是最可靠的判断方式
    if (frame->hw_frames_ctx != nullptr) {
        return true;
    }
    
    // 备用检查：通过像素格式判断（某些情况下 hw_frames_ctx 可能为空）
    return (frame->format == AV_PIX_FMT_D3D11 ||
            frame->format == AV_PIX_FMT_DXVA2_VLD ||
            frame->format == AV_PIX_FMT_VAAPI ||
            frame->format == AV_PIX_FMT_VIDEOTOOLBOX ||
            frame->format == AV_PIX_FMT_CUDA ||
            frame->format == AV_PIX_FMT_QSV);
#else
    return false;
#endif
}

bool OpenGLVideoConverter::mapHardwareFrameToTextures(AVFrame* hwFrame, QOpenGLTexture* textures[3]) {
#ifdef HAS_FFMPEG
    if (!hwFrame || !isHardwareFrame(hwFrame)) {
        return false;
    }
    
    // ===== 硬件帧零拷贝映射实现 =====
    // 目标：将硬件解码帧直接映射为OpenGL纹理，避免CPU-GPU数据传输
    // 性能提升：零拷贝可以显著减少内存带宽占用和延迟，特别是对于高分辨率视频
    
    // 根据硬件帧格式选择对应的零拷贝方案
    AVPixelFormat hwFormat = static_cast<AVPixelFormat>(hwFrame->format);
    
#if defined(_WIN32) || defined(_WIN64)
    // ===== Windows平台：D3D11VA零拷贝 =====
    // 方案：使用WGL_NV_DX_interop扩展，将D3D11纹理直接映射为OpenGL纹理
    // 优势：
    // - 完全零拷贝，数据始终在GPU内存中
    // - 延迟极低，适合实时播放
    // - 支持所有D3D11VA硬件解码器（Intel QSV、NVIDIA NVDEC、AMD VCN等）
    if (hwFormat == AV_PIX_FMT_D3D11) {
        // TODO: 实现D3D11VA零拷贝映射
        // 步骤：
        // 1. 从AVFrame获取ID3D11Texture2D指针（通过hw_frames_ctx）
        // 2. 使用WGL_NV_DX_interop扩展注册D3D11设备到OpenGL上下文
        // 3. 使用wglDXSetResourceShareHandleNV()共享D3D11纹理
        // 4. 使用wglDXOpenDeviceNV()和wglDXLockObjectsNV()锁定纹理
        // 5. 使用glBindTexture()绑定共享的OpenGL纹理
        // 6. 渲染完成后使用wglDXUnlockObjectsNV()解锁
        //
        // 注意：需要检查WGL_NV_DX_interop扩展是否可用
        // 如果不可用，回退到CPU下载方式
        
        qDebug() << "[OpenGLVideoConverter] D3D11VA zero-copy mapping (pending), fallback to CPU download";
        // 暂时回退到CPU下载
        AVFrame* cpuFrame = downloadHardwareFrameToCPU(hwFrame);
        if (cpuFrame) {
            bool result = uploadSoftwareFrameToTextures(cpuFrame, textures);
            av_frame_free(&cpuFrame);
            return result;
        }
        return false;
    }
    
    // DXVA2格式（较老的API，通常也需要转换为D3D11或CPU下载）
    if (hwFormat == AV_PIX_FMT_DXVA2_VLD) {
        qDebug() << "[OpenGLVideoConverter] DXVA2 format, requires conversion to D3D11 or CPU download";
        AVFrame* cpuFrame = downloadHardwareFrameToCPU(hwFrame);
        if (cpuFrame) {
            bool result = uploadSoftwareFrameToTextures(cpuFrame, textures);
            av_frame_free(&cpuFrame);
            return result;
        }
        return false;
    }
    
#elif defined(__linux__)
    // ===== Linux平台：VA-API零拷贝 =====
    // 方案：使用EGL/GLX扩展，将VA-API表面直接映射为OpenGL纹理
    // 优势：
    // - 完全零拷贝，数据在GPU显存中
    // - 支持Intel、AMD、NVIDIA的硬件解码
    // - 通过DRM/KMS直接访问GPU内存
    if (hwFormat == AV_PIX_FMT_VAAPI) {
        // TODO: 实现VA-API零拷贝映射
        // 步骤：
        // 1. 从AVFrame获取VASurfaceID（通过hw_frames_ctx）
        // 2. 使用EGL扩展（如EGL_EXT_image_dma_buf_import）导入VA-API表面
        // 3. 或者使用GLX扩展（如GLX_MESA_query_renderer）访问DRM表面
        // 4. 创建EGLImage或GLX纹理，直接绑定到OpenGL
        // 5. 渲染完成后释放资源
        //
        // 注意：需要检查EGL/GLX扩展是否可用
        // 如果不可用，回退到CPU下载方式
        
        qDebug() << "[OpenGLVideoConverter] VA-API zero-copy mapping (pending), fallback to CPU download";
        // 暂时回退到CPU下载
        AVFrame* cpuFrame = downloadHardwareFrameToCPU(hwFrame);
        if (cpuFrame) {
            bool result = uploadSoftwareFrameToTextures(cpuFrame, textures);
            av_frame_free(&cpuFrame);
            return result;
        }
        return false;
    }
    
#elif defined(__APPLE__) && defined(__MACH__)
    // ===== macOS平台：VideoToolbox零拷贝 =====
    // 方案：使用CVPixelBuffer和OpenGL互操作，将VideoToolbox帧直接映射为OpenGL纹理
    // 优势：
    // - 完全零拷贝，数据在GPU内存中
    // - 使用Metal/OpenGL互操作，性能优异
    // - 支持所有Apple Silicon和Intel Mac的硬件解码
    if (hwFormat == AV_PIX_FMT_VIDEOTOOLBOX) {
        // TODO: 实现VideoToolbox零拷贝映射
        // 步骤：
        // 1. 从AVFrame获取CVPixelBufferRef（通过hw_frames_ctx）
        // 2. 使用CVOpenGLTextureCacheCreateTextureFromImage()创建OpenGL纹理
        // 3. 或者使用Metal互操作（如果使用Metal后端）
        // 4. 直接绑定纹理到OpenGL进行渲染
        // 5. 渲染完成后释放CVPixelBuffer引用
        //
        // 注意：需要检查OpenGL扩展是否可用
        // 如果不可用，回退到CPU下载方式
        
        qDebug() << "[OpenGLVideoConverter] VideoToolbox zero-copy mapping (pending), fallback to CPU download";
        // 暂时回退到CPU下载
        AVFrame* cpuFrame = downloadHardwareFrameToCPU(hwFrame);
        if (cpuFrame) {
            bool result = uploadSoftwareFrameToTextures(cpuFrame, textures);
            av_frame_free(&cpuFrame);
            return result;
        }
        return false;
    }
    
#endif
    
    // ===== 其他硬件格式或零拷贝失败时的回退方案 =====
    // 对于CUDA (NVDEC)、QSV等其他格式，或者零拷贝实现不可用时，
    // 使用CPU下载方式作为回退，确保渲染不会失败
    // 性能影响：会有一次GPU->CPU的数据传输，但对于大多数场景仍然可接受
    // qDebug() << "[OpenGLVideoConverter] Hardware format" << av_get_pix_fmt_name(hwFormat)
    //          << "does not support zero-copy yet, using CPU download fallback";
    AVFrame* cpuFrame = downloadHardwareFrameToCPU(hwFrame);
    if (cpuFrame) {
        bool result = uploadSoftwareFrameToTextures(cpuFrame, textures);
        av_frame_free(&cpuFrame);
        return result;
    }
    
    return false;
#else
    return false;
#endif
}

bool OpenGLVideoConverter::uploadSoftwareFrameToTextures(AVFrame* swFrame, QOpenGLTexture* textures[3]) {
#ifdef HAS_FFMPEG
    if (!swFrame) return false;
    
    // 确保纹理已创建
    ensureTextures(textures, swFrame->width, swFrame->height);
    
    // 转换为YUV420P格式（如果需要）
    AVFrame* yuvFrame = swFrame;
    if (swFrame->format != AV_PIX_FMT_YUV420P) {
        yuvFrame = convertPixelFormat(swFrame, AV_PIX_FMT_YUV420P);
        if (!yuvFrame) {
            qWarning() << "[OpenGLVideoConverter] Pixel format conversion failed";
            return false;
        }
    }
    
    QOpenGLContext* context = QOpenGLContext::currentContext();
    if (!context) {
        qWarning() << "[OpenGLVideoConverter] No current OpenGL context";
        return false;
    }
    
    QOpenGLFunctions* gl = context->functions();
    
    // 设置像素对齐方式为1字节（处理linesize不是4的倍数的情况）
    gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    
    // ==================== PBO 双缓冲异步上传 ====================
    if (pboAvailable_ && pboInitialized_) {
        // 使用 PBO 异步上传：CPU 写入当前 PBO，GPU 从上一个 PBO 上传纹理
        int writeIndex = currentPboIndex_;
        int readIndex = 1 - currentPboIndex_;  // 切换索引
        
        bool pboSuccess = true;
        
        // 上传 Y 平面
        if (textures[0] && textures[0]->isCreated()) {
            if (!uploadPlaneWithPBO(0, yuvFrame->data[0], 
                                   yuvFrame->width, yuvFrame->height,
                                   yuvFrame->linesize[0], textures[0], writeIndex, readIndex)) {
                pboSuccess = false;
            }
        }
        
        // 上传 U 平面
        int uvWidth = yuvFrame->width / 2;
        int uvHeight = yuvFrame->height / 2;
        if (textures[1] && textures[1]->isCreated()) {
            if (!uploadPlaneWithPBO(1, yuvFrame->data[1], 
                                   uvWidth, uvHeight,
                                   yuvFrame->linesize[1], textures[1], writeIndex, readIndex)) {
                pboSuccess = false;
            }
        }
        
        // 上传 V 平面
        if (textures[2] && textures[2]->isCreated()) {
            if (!uploadPlaneWithPBO(2, yuvFrame->data[2], 
                                   uvWidth, uvHeight,
                                   yuvFrame->linesize[2], textures[2], writeIndex, readIndex)) {
                pboSuccess = false;
            }
        }
        
        // 如果 PBO 上传失败，回退到同步上传
        if (!pboSuccess) {
            uploadPlaneSync(yuvFrame->data[0], yuvFrame->width, yuvFrame->height,
                           yuvFrame->linesize[0], textures[0]);
            uploadPlaneSync(yuvFrame->data[1], uvWidth, uvHeight,
                           yuvFrame->linesize[1], textures[1]);
            uploadPlaneSync(yuvFrame->data[2], uvWidth, uvHeight,
                           yuvFrame->linesize[2], textures[2]);
        } else {
            // 所有平面上传成功，切换 PBO 索引（下一帧使用另一个缓冲区）
            currentPboIndex_ = readIndex;
        }
    } else {
        // PBO 不可用，使用同步上传（回退方案）
        uploadPlaneSync(yuvFrame->data[0], yuvFrame->width, yuvFrame->height,
                       yuvFrame->linesize[0], textures[0]);
        
        int uvWidth = yuvFrame->width / 2;
        int uvHeight = yuvFrame->height / 2;
        uploadPlaneSync(yuvFrame->data[1], uvWidth, uvHeight,
                       yuvFrame->linesize[1], textures[1]);
        uploadPlaneSync(yuvFrame->data[2], uvWidth, uvHeight,
                       yuvFrame->linesize[2], textures[2]);
    }
    
    // 恢复默认对齐方式
    gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    
    return true;
#else
    return false;
#endif
}

AVFrame* OpenGLVideoConverter::downloadHardwareFrameToCPU(AVFrame* hwFrame) {
#ifdef HAS_FFMPEG
    if (!hwFrame || !isHardwareFrame(hwFrame)) {
        return nullptr;
    }
    
    AVFrame* cpuFrame = av_frame_alloc();
    if (!cpuFrame) {
        return nullptr;
    }
    
    int ret = av_hwframe_transfer_data(cpuFrame, hwFrame, 0);
    if (ret < 0) {
        av_frame_free(&cpuFrame);
        return nullptr;
    }
    
    // 复制帧属性（包括所有元数据）
    // 使用 av_frame_copy_props() 可以安全地复制所有属性，包括已弃用的字段
    // 注意：key_frame 字段已弃用，新版本应使用 flags & AV_FRAME_FLAG_KEY 来判断关键帧
    // 但 av_frame_copy_props() 会处理兼容性，我们不需要直接访问该字段
    int ret_props = av_frame_copy_props(cpuFrame, hwFrame);
    if (ret_props < 0) {
        // 如果复制属性失败，手动复制关键字段
        cpuFrame->pts = hwFrame->pts;
        cpuFrame->pkt_dts = hwFrame->pkt_dts;
        cpuFrame->best_effort_timestamp = hwFrame->best_effort_timestamp;
        cpuFrame->duration = hwFrame->duration;
        cpuFrame->flags = hwFrame->flags;  // 包含关键帧信息 (AV_FRAME_FLAG_KEY)
        cpuFrame->pict_type = hwFrame->pict_type;
        // 注意：不再手动复制已弃用的 key_frame 字段，使用 flags & AV_FRAME_FLAG_KEY 来判断关键帧
    }
    
    return cpuFrame;
#else
    return nullptr;
#endif
}

void OpenGLVideoConverter::ensureTextures(QOpenGLTexture* textures[3], int width, int height) {
    QOpenGLContext* context = QOpenGLContext::currentContext();
    if (!context) {
        qWarning() << "[OpenGLVideoConverter::ensureTextures] No OpenGL context";
        return;
    }
    
    // ==================== 关键检查：确保尺寸有效 ====================
    // 在组件还没渲染好的时候，可能传入无效尺寸（0x0），此时不应该创建纹理
    // 否则纹理内容未定义，渲染时会显示绿色
    if (width <= 0 || height <= 0) {
        qWarning() << "[OpenGLVideoConverter::ensureTextures] Invalid size:" << width << "x" << height;
        return;
    }
    
    QOpenGLFunctions* gl = context->functions();
    bool needInitY = false;
    bool needInitU = false;
    bool needInitV = false;
    
    // 创建Y纹理
    if (!textures[0] || !textures[0]->isCreated() || 
        textures[0]->width() != width || textures[0]->height() != height) {
        if (textures[0]) {
            delete textures[0];
        }
        textures[0] = new QOpenGLTexture(QOpenGLTexture::Target2D);
        textures[0]->setSize(width, height);
        textures[0]->setFormat(QOpenGLTexture::R8_UNorm);
        textures[0]->setMinificationFilter(QOpenGLTexture::Linear);
        textures[0]->setMagnificationFilter(QOpenGLTexture::Linear);
        textures[0]->allocateStorage();
        needInitY = true;  // 标记需要初始化为黑色
    }
    
    // 创建U纹理
    int uvWidth = width / 2;
    int uvHeight = height / 2;
    if (!textures[1] || !textures[1]->isCreated() || 
        textures[1]->width() != uvWidth || textures[1]->height() != uvHeight) {
        if (textures[1]) {
            delete textures[1];
        }
        textures[1] = new QOpenGLTexture(QOpenGLTexture::Target2D);
        textures[1]->setSize(uvWidth, uvHeight);
        textures[1]->setFormat(QOpenGLTexture::R8_UNorm);
        textures[1]->setMinificationFilter(QOpenGLTexture::Linear);
        textures[1]->setMagnificationFilter(QOpenGLTexture::Linear);
        textures[1]->allocateStorage();
        needInitU = true;  // 标记需要初始化为黑色
    }
    
    // 创建V纹理
    if (!textures[2] || !textures[2]->isCreated() || 
        textures[2]->width() != uvWidth || textures[2]->height() != uvHeight) {
        if (textures[2]) {
            delete textures[2];
        }
        textures[2] = new QOpenGLTexture(QOpenGLTexture::Target2D);
        textures[2]->setSize(uvWidth, uvHeight);
        textures[2]->setFormat(QOpenGLTexture::R8_UNorm);
        textures[2]->setMinificationFilter(QOpenGLTexture::Linear);
        textures[2]->setMagnificationFilter(QOpenGLTexture::Linear);
        textures[2]->allocateStorage();
        needInitV = true;  // 标记需要初始化为黑色
    }
    
    // 立即初始化新创建的纹理为黑色（避免显示未初始化的绿色数据）
    // YUV420P 中，黑色对应：Y=0x00, U=0x80, V=0x80
    // 注意：必须在 allocateStorage() 后立即初始化，否则纹理内容未定义
    if (needInitY && textures[0] && textures[0]->isCreated()) {
        textures[0]->bind();
        // 创建全零缓冲区（Y=0 表示黑色）
        std::vector<uint8_t> zeroData(width * height, 0);
        // 设置像素对齐为1字节（确保正确上传）
        gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        gl->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height,
                           GL_RED, GL_UNSIGNED_BYTE, zeroData.data());
        gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 4);  // 恢复默认对齐
        textures[0]->release();
        qDebug() << "[OpenGLVideoConverter::ensureTextures] Y texture initialized to black" << width << "x" << height;
    }
    
    if (needInitU && textures[1] && textures[1]->isCreated()) {
        textures[1]->bind();
        // U=0x80 表示色度中性（黑色），在着色器中 u = texture.r - 0.5，所以 0x80 - 0.5 ≈ 0
        std::vector<uint8_t> neutralData(uvWidth * uvHeight, 0x80);
        gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        gl->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, uvWidth, uvHeight,
                           GL_RED, GL_UNSIGNED_BYTE, neutralData.data());
        gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 4);  // 恢复默认对齐
        textures[1]->release();
        qDebug() << "[OpenGLVideoConverter::ensureTextures] U texture initialized to black" << uvWidth << "x" << uvHeight;
    }
    
    if (needInitV && textures[2] && textures[2]->isCreated()) {
        textures[2]->bind();
        // V=0x80 表示色度中性（黑色），在着色器中 v = texture.r - 0.5，所以 0x80 - 0.5 ≈ 0
        std::vector<uint8_t> neutralData(uvWidth * uvHeight, 0x80);
        gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        gl->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, uvWidth, uvHeight,
                           GL_RED, GL_UNSIGNED_BYTE, neutralData.data());
        gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 4);  // 恢复默认对齐
        textures[2]->release();
        qDebug() << "[OpenGLVideoConverter::ensureTextures] V texture initialized to black" << uvWidth << "x" << uvHeight;
    }
}

AVFrame* OpenGLVideoConverter::convertPixelFormat(AVFrame* srcFrame, int dstFormat) {
#ifdef HAS_FFMPEG
    if (!srcFrame) return nullptr;
    
    // 如果格式已匹配，直接返回
    if (srcFrame->format == dstFormat) {
        return srcFrame;
    }
    
    // 创建转换上下文（如果不存在或参数变化）
    bool needReinit = !swsContext_ || !convertedFrame_ ||
                      srcFrame->width != convertedFrame_->width ||
                      srcFrame->height != convertedFrame_->height ||
                      srcFrame->format != static_cast<AVPixelFormat>(convertedFrame_->format);
    
    if (needReinit) {
        if (swsContext_) {
            sws_freeContext(swsContext_);
        }
        if (convertedFrame_) {
            av_frame_free(&convertedFrame_);
        }
        
        swsContext_ = sws_getContext(
            srcFrame->width, srcFrame->height, (AVPixelFormat)srcFrame->format,
            srcFrame->width, srcFrame->height, (AVPixelFormat)dstFormat,
            SWS_BILINEAR, nullptr, nullptr, nullptr
        );
        
        if (!swsContext_) {
            return nullptr;
        }
        
        convertedFrame_ = av_frame_alloc();
        if (!convertedFrame_) {
            sws_freeContext(swsContext_);
            swsContext_ = nullptr;
            return nullptr;
        }
        
        convertedFrame_->format = dstFormat;
        convertedFrame_->width = srcFrame->width;
        convertedFrame_->height = srcFrame->height;
        int ret = av_frame_get_buffer(convertedFrame_, 0);
        if (ret < 0) {
            av_frame_free(&convertedFrame_);
            convertedFrame_ = nullptr;
            sws_freeContext(swsContext_);
            swsContext_ = nullptr;
            return nullptr;
        }
    }
    
    // 执行转换
    sws_scale(swsContext_,
              (const uint8_t* const*)srcFrame->data, srcFrame->linesize, 0, srcFrame->height,
              convertedFrame_->data, convertedFrame_->linesize);
    
    // 复制元数据
    // 使用 av_frame_copy_props() 可以安全地复制所有属性，包括已弃用的字段
    // 注意：key_frame 字段已弃用，新版本应使用 flags & AV_FRAME_FLAG_KEY 来判断关键帧
    // 但 av_frame_copy_props() 会处理兼容性，我们不需要直接访问该字段
    int ret_props = av_frame_copy_props(convertedFrame_, srcFrame);
    if (ret_props < 0) {
        // 如果复制属性失败，手动复制关键字段
        convertedFrame_->pts = srcFrame->pts;
        convertedFrame_->pkt_dts = srcFrame->pkt_dts;
        convertedFrame_->best_effort_timestamp = srcFrame->best_effort_timestamp;
        convertedFrame_->duration = srcFrame->duration;
        convertedFrame_->flags = srcFrame->flags;  // 包含关键帧信息 (AV_FRAME_FLAG_KEY)
        convertedFrame_->pict_type = srcFrame->pict_type;
        // 注意：不再手动复制已弃用的 key_frame 字段，使用 flags & AV_FRAME_FLAG_KEY 来判断关键帧
    }
    
    return convertedFrame_;
#else
    return srcFrame;
#endif
}

// ==================== PBO 双缓冲实现 ====================

bool OpenGLVideoConverter::initializePBO() {
    // 调试开关：强制禁用 PBO，便于排查驱动相关崩溃
    const QString disablePboEnv = qEnvironmentVariable("ADVANCEDPLAYER_DISABLE_PBO").trimmed();
    const bool forceDisablePbo =
        (disablePboEnv == "1") ||
        (disablePboEnv.compare("true", Qt::CaseInsensitive) == 0) ||
        (disablePboEnv.compare("yes", Qt::CaseInsensitive) == 0) ||
        (disablePboEnv.compare("on", Qt::CaseInsensitive) == 0);
    if (forceDisablePbo) {
        pboAvailable_ = false;
        pboInitialized_ = false;
        qWarning() << "[OpenGLVideoConverter::initializePBO] Debug mode: PBO disabled by ADVANCEDPLAYER_DISABLE_PBO";
        return false;
    }

    QOpenGLContext* context = QOpenGLContext::currentContext();
    if (!context) {
        qWarning() << "[OpenGLVideoConverter::initializePBO] No current OpenGL context";
        return false;
    }
    
    QOpenGLFunctions* gl = context->functions();
    
    // 检测 PBO 支持
    // 方法1：尝试检查扩展（兼容旧版 OpenGL）
    bool hasPboExtension = false;
    const char* extensions = reinterpret_cast<const char*>(gl->glGetString(GL_EXTENSIONS));
    if (extensions) {
        hasPboExtension = (strstr(extensions, "GL_ARB_pixel_buffer_object") != nullptr ||
                           strstr(extensions, "GL_EXT_pixel_buffer_object") != nullptr ||
                           strstr(extensions, "GL_NV_pixel_buffer_object") != nullptr);
    }
    
    // 方法2：OpenGL 3.0+ 核心功能包含 PBO，通过版本判断
    int majorVersion = context->format().majorVersion();
    int minorVersion = context->format().minorVersion();
    bool hasCorePBO = (majorVersion > 3 || (majorVersion == 3 && minorVersion >= 0));
    
    pboAvailable_ = hasPboExtension || hasCorePBO;
    
    if (!pboAvailable_) {
        qInfo() << "[OpenGLVideoConverter::initializePBO] PBO is not supported (OpenGL"
                << majorVersion << "." << minorVersion << "), using synchronous upload";
        return false;
    }
    
    // 初始化 PBO 句柄为 0（未创建状态）
    for (int plane = 0; plane < PLANE_COUNT; ++plane) {
        for (int i = 0; i < PBO_COUNT; ++i) {
            pbo_[plane][i] = 0;
        }
        pboSize_[plane] = 0;
    }
    
    currentPboIndex_ = 0;
    pboInitialized_ = true;
    
    qInfo() << "[OpenGLVideoConverter::initializePBO] PBO double buffering initialized (OpenGL"
            << majorVersion << "." << minorVersion << "）";
    return true;
}

void OpenGLVideoConverter::cleanupPBO() {
    if (!pboInitialized_) {
        return;
    }
    
    QOpenGLContext* context = QOpenGLContext::currentContext();
    if (!context) {
        // 上下文已销毁，PBO 会自动释放
        pboInitialized_ = false;
        return;
    }
    
    QOpenGLFunctions* gl = context->functions();
    
    // 删除所有 PBO
    for (int plane = 0; plane < PLANE_COUNT; ++plane) {
        for (int i = 0; i < PBO_COUNT; ++i) {
            if (pbo_[plane][i] != 0) {
                gl->glDeleteBuffers(1, &pbo_[plane][i]);
                pbo_[plane][i] = 0;
            }
            // 重置数据有效性标志
            pboHasData_[plane][i] = false;
        }
        pboSize_[plane] = 0;
    }
    
    pboInitialized_ = false;
    qDebug() << "[OpenGLVideoConverter::cleanupPBO] PBO resources released";
}

void OpenGLVideoConverter::ensurePboSize(int plane, size_t requiredSize) {
    if (plane < 0 || plane >= PLANE_COUNT) {
        return;
    }
    
    // 如果当前 PBO 大小足够，无需重新分配
    if (pboSize_[plane] >= requiredSize) {
        return;
    }
    
    QOpenGLContext* context = QOpenGLContext::currentContext();
    if (!context) {
        return;
    }
    
    QOpenGLFunctions* gl = context->functions();
    
    // 删除旧的 PBO（如果存在）
    for (int i = 0; i < PBO_COUNT; ++i) {
        if (pbo_[plane][i] != 0) {
            gl->glDeleteBuffers(1, &pbo_[plane][i]);
            pbo_[plane][i] = 0;
        }
        // 重置数据有效性标志
        pboHasData_[plane][i] = false;
    }
    
    // 创建新的 PBO（双缓冲）
    gl->glGenBuffers(PBO_COUNT, pbo_[plane]);
    
    // 为每个 PBO 分配内存（使用 GL_STREAM_DRAW 提示，表示数据会频繁更新）
    for (int i = 0; i < PBO_COUNT; ++i) {
        gl->glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo_[plane][i]);
        gl->glBufferData(GL_PIXEL_UNPACK_BUFFER, requiredSize, nullptr, GL_STREAM_DRAW);
        // 新创建的 PBO 还没有数据
        pboHasData_[plane][i] = false;
    }
    
    gl->glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    pboSize_[plane] = requiredSize;
    
    qDebug() << "[OpenGLVideoConverter::ensurePboSize] Plane" << plane
             << "PBO size resized to" << requiredSize << "bytes";
}

bool OpenGLVideoConverter::uploadPlaneWithPBO(int plane, const uint8_t* data,
                                             int width, int height, int linesize,
                                             QOpenGLTexture* texture,
                                             int writeIndex, int readIndex) {
    if (plane < 0 || plane >= PLANE_COUNT || !texture || !texture->isCreated()) {
        return false;
    }
    
    QOpenGLContext* context = QOpenGLContext::currentContext();
    if (!context) {
        return false;
    }
    
    QOpenGLFunctions* gl = context->functions();
    
    // 计算所需大小（使用 linesize 而不是 width，因为可能有对齐）
    size_t requiredSize = static_cast<size_t>(linesize) * height;
    
    // 确保 PBO 大小足够
    ensurePboSize(plane, requiredSize);
    
    // ==================== 步骤 1：GPU 从上一个 PBO 上传纹理（异步）====================
    // 注意：这是上一帧写入的 PBO，现在 GPU 可以异步读取
    // 第一帧时 readIndex 的 PBO 可能还没有数据，需要检查
    if (pbo_[plane][readIndex] != 0 && pboHasData_[plane][readIndex]) {
        // PBO 存在且包含有效数据，使用异步上传
        gl->glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo_[plane][readIndex]);
        
        texture->bind();
        
        // 设置行跨度（如果 linesize != width）
        if (linesize != width) {
            gl->glPixelStorei(GL_UNPACK_ROW_LENGTH, linesize);
        }
        
        // 从 PBO 上传纹理（异步，不阻塞 CPU）
        // nullptr 表示从当前绑定的 PBO 读取数据
        gl->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height,
                           GL_RED, GL_UNSIGNED_BYTE, nullptr);
        
        if (linesize != width) {
            gl->glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        }
        
        texture->release();
        
        // 标记 readIndex 的 PBO 数据已使用（可选，用于调试）
        // pboHasData_[plane][readIndex] = false;  // 不需要，因为下一帧会覆盖
    } else {
        // 第一帧或 PBO 没有有效数据：使用同步上传当前帧
        // 这样可以避免从空的 PBO 读取导致绿屏
        uploadPlaneSync(data, width, height, linesize, texture);
    }
    
    // ==================== 步骤 2：CPU 写入当前 PBO（为下一帧准备）====================
    gl->glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo_[plane][writeIndex]);
    
    // 使用 glBufferSubData 直接写入数据到 PBO（无需映射/取消映射）
    // 这是 QOpenGLFunctions 支持的标准方法，兼容性更好
    if (linesize == width) {
        // 行对齐，直接写入整个缓冲区
        gl->glBufferSubData(GL_PIXEL_UNPACK_BUFFER, 0, requiredSize, data);
    } else {
        // 行不对齐，需要逐行写入（使用偏移）
        // 注意：PBO 中存储的是对齐后的数据（linesize），与原始数据一致
        // 这样 GPU 读取时可以使用 GL_UNPACK_ROW_LENGTH 正确处理
        size_t offset = 0;
        const uint8_t* src = data;
        for (int y = 0; y < height; ++y) {
            gl->glBufferSubData(GL_PIXEL_UNPACK_BUFFER, offset, width, src);
            src += linesize;
            offset += linesize;
        }
    }
    
    // 标记 writeIndex 的 PBO 现在包含有效数据（下一帧可以使用）
    pboHasData_[plane][writeIndex] = true;
    
    gl->glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    
    return true;
}

void OpenGLVideoConverter::uploadPlaneSync(const uint8_t* data, int width, int height,
                                          int linesize, QOpenGLTexture* texture) {
    if (!texture || !texture->isCreated()) {
        return;
    }
    
    QOpenGLContext* context = QOpenGLContext::currentContext();
    if (!context) {
        return;
    }
    
    QOpenGLFunctions* gl = context->functions();
    
    texture->bind();
    
    // 如果 linesize == width，可以直接上传
    if (linesize == width) {
        gl->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height,
                           GL_RED, GL_UNSIGNED_BYTE, data);
    } else {
        // 否则需要设置行跨度
        gl->glPixelStorei(GL_UNPACK_ROW_LENGTH, linesize);
        gl->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height,
                           GL_RED, GL_UNSIGNED_BYTE, data);
        gl->glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    }
    
    texture->release();
}

} // namespace AdvancedPlayer

