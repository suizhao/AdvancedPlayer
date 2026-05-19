#include "VideoOutput.h"
#include "OpenGLVideoConverter.h"
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLTexture>
#include <QOpenGLShaderProgram>
#include <QOpenGLFunctions>
#include <QSurfaceFormat>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QQuickWindow>
#include <QMutex>
#include <QMetaObject>
#include <QDebug>
#include <QRect>

#ifdef HAS_FFMPEG
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}
#endif

namespace AdvancedPlayer {

// 顶点着色器
static const char* vertexShaderSource = R"(
    #version 330 core
    layout (location = 0) in vec2 aPos;
    layout (location = 1) in vec2 aTexCoord;
    
    out vec2 TexCoord;
    
    void main() {
        gl_Position = vec4(aPos, 0.0, 1.0);
        TexCoord = aTexCoord;
    }
)";

// 片段着色器（YUV420P到RGB转换）
static const char* fragmentShaderSource = R"(
    #version 330 core
    in vec2 TexCoord;
    out vec4 FragColor;
    
    uniform sampler2D yTexture;
    uniform sampler2D uTexture;
    uniform sampler2D vTexture;
    
    void main() {
        float y = texture(yTexture, TexCoord).r;
        float u = texture(uTexture, TexCoord).r - 0.5;
        float v = texture(vTexture, TexCoord).r - 0.5;
        
        // YUV to RGB conversion (ITU-R BT.601)
        float r = y + 1.402 * v;
        float g = y - 0.344 * u - 0.714 * v;
        float b = y + 1.772 * u;
        
        FragColor = vec4(r, g, b, 1.0);
    }
)";

class VideoOutput::VideoRenderer : public QQuickFramebufferObject::Renderer {
public:
    VideoRenderer() {
        converter_ = std::make_unique<OpenGLVideoConverter>();
        converter_->initialize();
        
        // 着色器程序可以在构造函数中创建，但VAO/VBO需要在有OpenGL上下文时创建
        setupShaderProgram();
    }
    
    ~VideoRenderer() override {
        cleanup();
    }
    
    void render() override {
        // 首次渲染时输出日志
        static bool firstRender = true;
        if (firstRender) {
            qInfo() << "[VideoRenderer::render] render() called for the first time";
            firstRender = false;
        }
        
        QOpenGLContext* context = QOpenGLContext::currentContext();
        if (!context) {
            qWarning() << "[VideoRenderer::render] No OpenGL context";
            return;
        }

        // 首次拿到 OpenGL 上下文时打印真实环境信息，确认 3.3 Core 是否生效
        static bool contextInfoLogged = false;
        if (!contextInfoLogged) {
            QSurfaceFormat actualFormat = context->format();
            const int major = actualFormat.majorVersion();
            const int minor = actualFormat.minorVersion();
            const auto profile = actualFormat.profile();
            const bool versionOk = (major > 3) || (major == 3 && minor >= 3);
            const bool profileOk = (profile == QSurfaceFormat::CoreProfile);

            QOpenGLFunctions* gl = context->functions();
            const char* glVersion = reinterpret_cast<const char*>(gl->glGetString(GL_VERSION));
            const char* glVendor = reinterpret_cast<const char*>(gl->glGetString(GL_VENDOR));
            const char* glRenderer = reinterpret_cast<const char*>(gl->glGetString(GL_RENDERER));

            qInfo() << "[VideoRenderer::render] Actual OpenGL context:"
                    << major << "." << minor
                    << ", profile:" << profile
                    << ", versionOk:" << versionOk
                    << ", profileOk:" << profileOk;
            qInfo() << "[VideoRenderer::render] GL_VERSION:" << (glVersion ? glVersion : "unknown");
            qInfo() << "[VideoRenderer::render] GL_VENDOR:" << (glVendor ? glVendor : "unknown");
            qInfo() << "[VideoRenderer::render] GL_RENDERER:" << (glRenderer ? glRenderer : "unknown");

            if (!versionOk || !profileOk) {
                qWarning() << "[VideoRenderer::render] Warning: OpenGL 3.3 Core requirements are not satisfied";
            }

            contextInfoLogged = true;
        }
        
        // 延迟初始化几何数据（确保在OpenGL上下文中）
        setupGeometry();
        
        QOpenGLFunctions* gl = context->functions();
        
        // 清除背景为黑色
        gl->glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        gl->glClear(GL_COLOR_BUFFER_BIT);
        
        // 只有在窗口准备好时才处理帧和渲染
        // 这样可以避免在 QML 窗口打开前渲染导致绿屏
        if (!isWindowReady_) {
            return;  // 窗口未准备好，只清除为黑色，不渲染
        }
        
        // 检查是否有新帧需要处理
        processPendingFrame();
        
        // 只有在首帧准备好且所有纹理都有效时才渲染
        // 这样可以避免在首帧数据上传前显示未初始化的纹理（绿色）
        // 额外检查：确保视频尺寸有效（说明已经有视频数据）
        if (firstFrameReady_ && 
            videoSize_.width() > 0 && videoSize_.height() > 0 &&
            textures_[0] && textures_[0]->isCreated() &&
            textures_[1] && textures_[1]->isCreated() &&
            textures_[2] && textures_[2]->isCreated()) {
            renderTextures();
        }
    }
    
    QOpenGLFramebufferObject* createFramebufferObject(const QSize& size) override {
        QOpenGLContext* context = QOpenGLContext::currentContext();
        if (!context) {
            qWarning() << "[VideoRenderer::createFramebufferObject] No OpenGL context";
            return nullptr;
        }
        
        // 只在首次创建时输出日志
        static bool firstTime = true;
        if (firstTime) {
            qInfo() << "[VideoRenderer::createFramebufferObject] Creating FBO, size:" << size;
            firstTime = false;
        }
        
        QOpenGLFramebufferObjectFormat format{};
        format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
        format.setSamples(0); // 禁用多重采样以提高性能
        format.setTextureTarget(GL_TEXTURE_2D);
        format.setInternalTextureFormat(GL_RGBA8); // 确保使用标准格式
        
        QOpenGLFramebufferObject* fbo = new QOpenGLFramebufferObject(size, format);
        if (!fbo || !fbo->isValid()) {
            qCritical() << "[VideoRenderer::createFramebufferObject] FBO creation failed or invalid";
            delete fbo;
            return nullptr;
        }
        
        return fbo;
    }
    
    void synchronize(QQuickFramebufferObject* item) override {
        VideoOutput* output = static_cast<VideoOutput*>(item);
        
        // 同步填充模式
        fillMode_ = output->fillMode_;
        
        // 同步渲染区域大小（FBO大小）
        renderSize_ = output->size().toSize();
        
        // 同步窗口状态：检查窗口是否可见且准备好
        // 这样可以避免在窗口打开前渲染导致绿屏
        QQuickWindow* window = output->window();
        isWindowReady_ = (window != nullptr && 
                         window->isVisible() && 
                         output->isVisible() &&
                         renderSize_.width() > 0 && 
                         renderSize_.height() > 0);
        
        // 同步视频大小
        if (videoSize_ != output->videoSize_) {
            videoSize_ = output->videoSize_;
            // 视频尺寸变化（可能是新视频），重置首帧标志
            // 这样可以避免在切换视频时显示旧视频的最后一帧
            firstFrameReady_ = false;
            // 纹理会在下一帧更新时重新创建
        }
        
        // 从主线程获取待处理的帧（线程安全）
        // synchronize在渲染线程中被调用，但在场景图同步阶段，此时可以安全访问QQuickItem
        QMutexLocker locker(&output->frameMutex_);
        if (output->pendingFrame_) {
            // 释放旧帧
            if (pendingFrame_) {
#ifdef HAS_FFMPEG
                av_frame_unref(pendingFrame_);
                av_frame_free(&pendingFrame_);
#endif
            }
            
            // 复制新帧（创建新的引用）
#ifdef HAS_FFMPEG
            pendingFrame_ = av_frame_alloc();
            if (pendingFrame_) {
                int ret = av_frame_ref(pendingFrame_, output->pendingFrame_);
                if (ret < 0) {
                    qWarning() << "[VideoRenderer::synchronize] Failed to reference AVFrame";
                    av_frame_free(&pendingFrame_);
                    pendingFrame_ = nullptr;
                }
            }
            // 注意：不清空output->pendingFrame_，由updateFrame()负责管理
#endif
        }
    }
    
private:
    std::unique_ptr<OpenGLVideoConverter> converter_{nullptr};
    QOpenGLTexture* textures_[3]{nullptr, nullptr, nullptr};
    QOpenGLShaderProgram* shaderProgram_{nullptr};
    
    QSize videoSize_;
    QSize renderSize_;  // FBO渲染区域大小
    VideoOutput::FillMode fillMode_{VideoOutput::PreserveAspectFit};
    
    // 几何数据
    QOpenGLVertexArrayObject* vao_{nullptr};
    QOpenGLBuffer* vbo_{nullptr};
    
    // 待处理的帧（在synchronize中从VideoOutput复制）
    AVFrame* pendingFrame_{nullptr};
    
    // 首帧就绪标志：确保在首帧数据上传后才开始渲染
    bool firstFrameReady_{false};
    
    // 窗口就绪标志：确保窗口可见且准备好后才渲染（避免窗口打开前绿屏）
    bool isWindowReady_{false};
    
    void setupShaderProgram() {
        shaderProgram_ = new QOpenGLShaderProgram();
        if (!shaderProgram_->addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShaderSource)) {
            qWarning() << "[VideoRenderer] Vertex shader compilation failed:" << shaderProgram_->log();
            return;
        }
        if (!shaderProgram_->addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentShaderSource)) {
            qWarning() << "[VideoRenderer] Fragment shader compilation failed:" << shaderProgram_->log();
            return;
        }
        
        if (!shaderProgram_->link()) {
            qWarning() << "[VideoRenderer] Shader program link failed:" << shaderProgram_->log();
            return;
        }
    }
    
    void setupGeometry() {
        // 延迟初始化VAO/VBO，确保在OpenGL上下文中
        if (vao_ && vao_->isCreated()) {
            return; // 已经初始化
        }
        
        QOpenGLContext* context = QOpenGLContext::currentContext();
        if (!context) {
            qWarning() << "[VideoRenderer] Failed to create geometry data: no OpenGL context";
            return;
        }
        
        vao_ = new QOpenGLVertexArrayObject(context);
        vbo_ = new QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
        
        vao_->create();
        vbo_->create();
        
        vao_->bind();
        vbo_->bind();
        
        // 顶点数据：全屏四边形
        // 纹理坐标：从下到上（0.0在底部，1.0在顶部）
        float vertices[] = {
            // 位置        // 纹理坐标
            -1.0f, -1.0f,  0.0f, 1.0f,  // 左下角
             1.0f, -1.0f,  1.0f, 1.0f,  // 右下角
             1.0f,  1.0f,  1.0f, 0.0f,  // 右上角
            -1.0f,  1.0f,  0.0f, 0.0f   // 左上角
        };
        
        vbo_->allocate(vertices, sizeof(vertices));
        
        // 位置属性
        shaderProgram_->enableAttributeArray(0);
        shaderProgram_->setAttributeBuffer(0, GL_FLOAT, 0, 2, 4 * sizeof(float));
        
        // 纹理坐标属性
        shaderProgram_->enableAttributeArray(1);
        shaderProgram_->setAttributeBuffer(1, GL_FLOAT, 2 * sizeof(float), 2, 4 * sizeof(float));
        
        vao_->release();
        vbo_->release();
    }
    
    void processPendingFrame() {
        if (!pendingFrame_) {
            return;
        }
        
#ifdef HAS_FFMPEG
        // 判断帧类型并更新纹理
        bool isHardware = OpenGLVideoConverter::isHardwareFrame(pendingFrame_);
        
        bool success = false;
        if (isHardware) {
            // ===== 硬件帧处理：优先尝试零拷贝映射 =====
            // mapHardwareFrameToTextures()内部会：
            // 1. 首先尝试平台特定的零拷贝映射（D3D11VA/VA-API/VideoToolbox）
            // 2. 如果零拷贝失败，自动回退到CPU下载方式
            // 3. 将CPU帧上传到OpenGL纹理
            // 
            // 性能说明：
            // - 零拷贝成功：数据始终在GPU内存，延迟最低，性能最佳
            // - 零拷贝失败：会有一次GPU->CPU传输，但仍比纯软件解码快
            success = converter_->mapHardwareFrameToTextures(pendingFrame_, textures_);
            
            // 注意：mapHardwareFrameToTextures()内部已经处理了回退逻辑，
            // 这里不需要再次检查，但如果需要额外的错误处理可以在这里添加
            if (!success) {
                qWarning() << "[VideoRenderer::processPendingFrame] Hardware frame processing failed,"
                           << "both zero-copy and CPU-download fallback failed";
            }
        } else {
            // ===== 软件帧处理：直接上传到OpenGL纹理 =====
            // 软件解码帧已经在CPU内存中，直接上传到GPU纹理即可
            success = converter_->uploadSoftwareFrameToTextures(pendingFrame_, textures_);
        }
        
        // 无论成功与否，都要释放帧（避免内存泄漏）
        AVFrame* frameToFree = pendingFrame_;
        pendingFrame_ = nullptr;  // 先清空指针，避免重复释放
        
        // 只在失败时输出警告，成功时不输出以减少日志
        if (!success) {
            qWarning() << "[VideoRenderer::processPendingFrame] Frame processing failed, size:"
                       << frameToFree->width << "x" << frameToFree->height;
        } else {
            // 首帧成功处理，标记为就绪
            if (!firstFrameReady_) {
                firstFrameReady_ = true;
                qInfo() << "[VideoRenderer::processPendingFrame] First frame is ready, start rendering";
            }
        }
        
        // 释放已处理的帧
        av_frame_unref(frameToFree);
        av_frame_free(&frameToFree);
#endif
    }
    
    void renderTextures() {
        if (!shaderProgram_ || !shaderProgram_->isLinked()) {
            return;
        }
        
        if (!vao_ || !vao_->isCreated()) {
            return;
        }
        
        QOpenGLContext* context = QOpenGLContext::currentContext();
        if (!context) {
            return;
        }
        
        // ==================== 关键检查：确保纹理确实有有效数据 ====================
        // 在组件还没渲染好的时候，纹理可能已经存在但内容未初始化（显示为绿色）
        // 因此必须严格检查：只有在首帧已就绪且视频尺寸有效时才渲染
        if (!firstFrameReady_ || videoSize_.width() <= 0 || videoSize_.height() <= 0) {
            // 纹理未就绪，不渲染（保持黑色背景）
            return;
        }
        
        // 确保所有三个纹理都存在且已创建且尺寸匹配
        if (!textures_[0] || !textures_[0]->isCreated() || 
            textures_[0]->width() != videoSize_.width() || 
            textures_[0]->height() != videoSize_.height()) {
            return;
        }
        
        int uvWidth = videoSize_.width() / 2;
        int uvHeight = videoSize_.height() / 2;
        if (!textures_[1] || !textures_[1]->isCreated() || 
            textures_[1]->width() != uvWidth || 
            textures_[1]->height() != uvHeight) {
            return;
        }
        if (!textures_[2] || !textures_[2]->isCreated() || 
            textures_[2]->width() != uvWidth || 
            textures_[2]->height() != uvHeight) {
            return;
        }
        
        QOpenGLFunctions* gl = context->functions();
        
        // 计算渲染区域（根据fillMode）
        QRect viewport(0, 0, renderSize_.width(), renderSize_.height());
        
        if (videoSize_.width() > 0 && videoSize_.height() > 0 && 
            renderSize_.width() > 0 && renderSize_.height() > 0) {
            
            float videoAspect = static_cast<float>(videoSize_.width()) / videoSize_.height();
            float renderAspect = static_cast<float>(renderSize_.width()) / renderSize_.height();
            
            switch (fillMode_) {
                case VideoOutput::PreserveAspectFit: {
                    // 保持宽高比，可能有黑边
                    if (videoAspect > renderAspect) {
                        // 视频更宽，以宽度为准
                        int height = static_cast<int>(renderSize_.width() / videoAspect);
                        int y = (renderSize_.height() - height) / 2;
                        viewport = QRect(0, y, renderSize_.width(), height);
                    } else {
                        // 视频更高，以高度为准
                        int width = static_cast<int>(renderSize_.height() * videoAspect);
                        int x = (renderSize_.width() - width) / 2;
                        viewport = QRect(x, 0, width, renderSize_.height());
                    }
                    break;
                }
                case VideoOutput::PreserveAspectCrop: {
                    // 保持宽高比，裁剪（使用整个视口，通过纹理坐标裁剪）
                    // 注意：完整的裁剪实现需要调整纹理坐标，这里先使用整个视口
                    viewport = QRect(0, 0, renderSize_.width(), renderSize_.height());
                    break;
                }
                case VideoOutput::Stretch: {
                    // 拉伸填充，使用整个区域
                    viewport = QRect(0, 0, renderSize_.width(), renderSize_.height());
                    break;
                }
            }
        }
        
        // 设置视口
        gl->glViewport(viewport.x(), viewport.y(), viewport.width(), viewport.height());
        
        shaderProgram_->bind();
        vao_->bind();
        
        // 绑定纹理（此时已经确保所有纹理都有效）
        textures_[0]->bind(0);
        shaderProgram_->setUniformValue("yTexture", 0);
        textures_[1]->bind(1);
        shaderProgram_->setUniformValue("uTexture", 1);
        textures_[2]->bind(2);
        shaderProgram_->setUniformValue("vTexture", 2);
        
        // 渲染
        gl->glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
        
        // 解绑纹理
        textures_[0]->release(0);
        textures_[1]->release(1);
        textures_[2]->release(2);
        
        vao_->release();
        shaderProgram_->release();
        
        // 恢复视口到整个区域（确保后续渲染正常）
        gl->glViewport(0, 0, renderSize_.width(), renderSize_.height());
    }
    
    void cleanup() {
        // 重置首帧标志和窗口就绪标志
        firstFrameReady_ = false;
        isWindowReady_ = false;
        
        // 清理纹理
        for (int i = 0; i < 3; ++i) {
            if (textures_[i]) {
                delete textures_[i];
                textures_[i] = nullptr;
            }
        }
        
        // 清理几何数据
        if (vao_) {
            delete vao_;
            vao_ = nullptr;
        }
        if (vbo_) {
            delete vbo_;
            vbo_ = nullptr;
        }
        
        // 清理着色器
        if (shaderProgram_) {
            delete shaderProgram_;
            shaderProgram_ = nullptr;
        }
        
        // 清理待处理帧（在渲染线程，不需要mutex）
        if (pendingFrame_) {
#ifdef HAS_FFMPEG
            av_frame_unref(pendingFrame_);
            av_frame_free(&pendingFrame_);
#endif
            pendingFrame_ = nullptr;
        }
    }
};



//=======================VideoOutput=========================

VideoOutput::VideoOutput(QQuickItem* parent)
    : QQuickFramebufferObject(parent) {
    qInfo() << "[VideoOutput] VideoOutput initialized";
    
    // 设置垂直镜像
    setMirrorVertically(true);
    
    // 确保纹理跟随Item大小变化
    setTextureFollowsItemSize(true);
    
    // 确保在有窗口的场景图中渲染
    setFlag(QQuickItem::ItemHasContents, true);
    
    // 连接到窗口，确保在窗口上下文中渲染
    connect(this, &QQuickItem::windowChanged, this, [this](QQuickWindow* window) {
        if (window) {
            qInfo() << "[VideoOutput] Connected to window, window size:" << window->size();
            // 确保窗口使用OpenGL渲染后端
            if (window->rendererInterface()) {
                auto api = window->rendererInterface()->graphicsApi();
                qInfo() << "[VideoOutput] Renderer interface type:" << api;
                if (api != QSGRendererInterface::OpenGL && 
                    api != QSGRendererInterface::OpenGLRhi) {
                    qWarning() << "[VideoOutput] Warning: current render backend is not OpenGL, video may display incorrectly";
                }
            }
            // 确保窗口可见时触发更新
            // 使用QMetaObject::invokeMethod确保update()在GUI线程中调用
            update();//this已经在GUI线程中，触发一次重绘请求
            //QMetaObject::invokeMethod(this, &VideoOutput::update, Qt::QueuedConnection);//用于跨线程让该函数在GUI线程调用
        } else {
            qWarning() << "[VideoOutput] Window disconnected";
        }
    });
}

VideoOutput::~VideoOutput() {
#ifdef HAS_FFMPEG
    // 清理待处理的帧
    QMutexLocker locker(&frameMutex_);
    if (pendingFrame_) {
        av_frame_unref(pendingFrame_);
        av_frame_free(&pendingFrame_);
        pendingFrame_ = nullptr;
    }
#endif
}

QQuickFramebufferObject::Renderer* VideoOutput::createRenderer() const {
    return new VideoRenderer();
}

void VideoOutput::updateFrame(AVFrame* frame) {
    if (!frame) {
        qWarning() << "[VideoOutput::updateFrame] Received null frame";
        return;
    }
    
#ifdef HAS_FFMPEG
    int width = frame->width;
    int height = frame->height;
    
    // 更新视频大小（线程安全，在主线程调用）
    // 只在视频尺寸变化时输出日志
    if (videoSize_.width() != width || videoSize_.height() != height) {
        videoSize_ = QSize(width, height);
        qInfo() << "[VideoOutput::updateFrame] Video size changed:" << width << "x" << height;
        emit videoSizeChanged();
    }
    
    // 将帧存储在队列中（线程安全）
    {
        QMutexLocker locker(&frameMutex_);
        
        // 释放旧帧
        if (pendingFrame_) {
            av_frame_free(&pendingFrame_);
            pendingFrame_ = nullptr;
        }
        
        // 保存新帧（创建引用）
        pendingFrame_ = av_frame_alloc();
        if (!pendingFrame_) {
            qCritical() << "[VideoOutput::updateFrame] Failed to allocate AVFrame";
            return;
        }
        int ret = av_frame_ref(pendingFrame_, frame);
        if (ret < 0) {
            qCritical() << "[VideoOutput::updateFrame] Failed to reference AVFrame";
            av_frame_free(&pendingFrame_);
            pendingFrame_ = nullptr;
            return;
        }
    }
    
    // 触发重绘（会调用synchronize和render）
    // 使用QMetaObject::invokeMethod确保update()在GUI线程中调用
    //QMetaObject::invokeMethod(this, &VideoOutput::update, Qt::QueuedConnection);
     update();
#else
    Q_UNUSED(frame);
#endif
}

void VideoOutput::setFillMode(FillMode mode) {
    if (fillMode_ != mode) {
        fillMode_ = mode;
        emit fillModeChanged();
        // 使用QMetaObject::invokeMethod确保update()在GUI线程中调用
        //QMetaObject::invokeMethod(this, &VideoOutput::update, Qt::QueuedConnection);
        update();
    }
}

void VideoOutput::clearPendingFrame() {
#ifdef HAS_FFMPEG
    QMutexLocker locker(&frameMutex_);
    
    // 释放待渲染的帧
    if (pendingFrame_) {
        av_frame_free(&pendingFrame_);
        pendingFrame_ = nullptr;
        qDebug() << "[VideoOutput::clearPendingFrame] Cleared pending render frame";
    }
    
    // 强制触发重绘，显示黑屏（避免显示旧帧）
    //QMetaObject::invokeMethod(this, &VideoOutput::update, Qt::QueuedConnection);
    update();
#else
    // FFmpeg未编译时无需处理
#endif
}

} // namespace AdvancedPlayer
