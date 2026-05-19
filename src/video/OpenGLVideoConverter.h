#ifndef OPENGLVIDEOCONVERTER_H
#define OPENGLVIDEOCONVERTER_H

#include <QOpenGLTexture>
struct AVFrame;
struct SwsContext;

namespace AdvancedPlayer {

/**
 * @brief OpenGL视频帧转换器
 * 
 * 负责将FFmpeg解码的AVFrame转换为OpenGL纹理
 * 支持硬件解码帧的零拷贝映射和软件解码帧的高效上传
 */
class OpenGLVideoConverter {
public:
    OpenGLVideoConverter();
    ~OpenGLVideoConverter();
    
    // 禁用拷贝
    OpenGLVideoConverter(const OpenGLVideoConverter&) = delete;
    OpenGLVideoConverter& operator=(const OpenGLVideoConverter&) = delete;
    
    /**
     * @brief 初始化转换器
     * @return 成功时返回 true
     */
    bool initialize();
    
    /**
     * @brief 将硬件帧映射为OpenGL纹理（零拷贝）
     * @param hwFrame 硬件解码帧
     * @param textures 输出纹理数组（Y/U/V三个纹理）
     * @return 成功时返回 true
     * 
     * @note 零拷贝实现策略：
     * - Windows (D3D11VA): 使用WGL_NV_DX_interop扩展，直接映射D3D11纹理
     * - Linux (VA-API): 使用EGL/GLX扩展，直接映射VA-API表面
     * - macOS (VideoToolbox): 使用CVPixelBuffer和OpenGL互操作
     * 
     * 如果零拷贝失败，会自动回退到CPU下载方式（downloadHardwareFrameToCPU）
     */
    bool mapHardwareFrameToTextures(AVFrame* hwFrame, QOpenGLTexture* textures[3]);
    
    /**
     * @brief 将软件帧上传到OpenGL纹理
     * @param swFrame 软件解码帧
     * @param textures 输出纹理数组（Y/U/V三个纹理）
     * @return 成功时返回 true
     * 
     * @note 使用 PBO 双缓冲异步上传（如果支持），否则回退到同步上传
     */
    bool uploadSoftwareFrameToTextures(AVFrame* swFrame, QOpenGLTexture* textures[3]);
    
    /**
     * @brief 检查是否为硬件帧
     * @param frame 视频帧
     * @return 硬件帧返回 true
     */
    static bool isHardwareFrame(AVFrame* frame);
    
    /**
     * @brief 将硬件帧下载到CPU内存（用于回退）
     * @param hwFrame 硬件帧
     * @return CPU帧，失败时返回 nullptr（调用者负责释放）
     */
    static AVFrame* downloadHardwareFrameToCPU(AVFrame* hwFrame);
    
private:
    // 软件帧像素格式转换器
    SwsContext* swsContext_{nullptr};
    AVFrame* convertedFrame_{nullptr};
    
    // ==================== PBO 双缓冲异步上传 ====================
    // PBO 双缓冲：每个平面（Y/U/V）使用两个 PBO 实现异步传输
    // 帧 N:   CPU 写入 PBO[0]，GPU 从 PBO[1] 上传纹理（并行）
    // 帧 N+1: CPU 写入 PBO[1]，GPU 从 PBO[0] 上传纹理（并行）
    
    static constexpr int PBO_COUNT = 2;  // 双缓冲
    static constexpr int PLANE_COUNT = 3; // Y/U/V 三个平面
    
    // PBO 句柄：pbo_[plane][buffer_index]
    // plane: 0=Y, 1=U, 2=V
    // buffer_index: 0=PBO_A, 1=PBO_B
    unsigned int pbo_[PLANE_COUNT][PBO_COUNT]{};
    
    // PBO 大小（字节）：pboSize_[plane]
    size_t pboSize_[PLANE_COUNT]{};
    
    // 当前使用的 PBO 索引（0 或 1，用于双缓冲切换）
    int currentPboIndex_{0};
    
    // PBO 是否已初始化
    bool pboInitialized_{false};
    
    // PBO 是否可用（检测到扩展支持）
    bool pboAvailable_{false};
    
    // PBO 数据有效性标志：pboHasData_[plane][buffer_index]
    // 用于跟踪每个 PBO 是否包含有效数据（避免从空的 PBO 读取导致绿屏）
    bool pboHasData_[PLANE_COUNT][PBO_COUNT]{};
    
    /**
     * @brief 创建或更新YUV纹理
     * @param textures 纹理数组
     * @param width 宽度
     * @param height 高度
     */
    void ensureTextures(QOpenGLTexture* textures[3], int width, int height);
    
    /**
     * @brief 转换像素格式（软件帧）
     * @param srcFrame 源帧
     * @param dstFormat 目标格式
     * @return 转换后的帧（内部缓存，下次调用可能失效）
     */
    AVFrame* convertPixelFormat(AVFrame* srcFrame, int dstFormat);
    
    /**
     * @brief 初始化 PBO 双缓冲
     * @return 成功时返回 true
     */
    bool initializePBO();
    
    /**
     * @brief 释放 PBO 资源
     */
    void cleanupPBO();
    
    /**
     * @brief 确保 PBO 大小足够（根据帧尺寸动态调整）
     * @param plane 平面索引（0=Y, 1=U, 2=V）
     * @param requiredSize 所需大小（字节）
     */
    void ensurePboSize(int plane, size_t requiredSize);
    
    /**
     * @brief 使用 PBO 异步上传平面数据
     * @param plane 平面索引（0=Y, 1=U, 2=V）
     * @param data 数据指针
     * @param width 宽度
     * @param height 高度
     * @param linesize 行跨度
     * @param texture 目标纹理
     * @param writeIndex 写入的 PBO 索引
     * @param readIndex 读取的 PBO 索引
     * @return 成功时返回 true
     */
    bool uploadPlaneWithPBO(int plane, const uint8_t* data, int width, int height, 
                            int linesize, QOpenGLTexture* texture,
                            int writeIndex, int readIndex);
    
    /**
     * @brief 使用同步方式上传平面数据（PBO 不可用时的回退）
     * @param data 数据指针
     * @param width 宽度
     * @param height 高度
     * @param linesize 行跨度
     * @param texture 目标纹理
     */
    void uploadPlaneSync(const uint8_t* data, int width, int height, 
                        int linesize, QOpenGLTexture* texture);
};

} // namespace AdvancedPlayer

#endif // OPENGLVIDEOCONVERTER_H

