#ifndef FILESCANNER_H
#define FILESCANNER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <filesystem>

namespace AdvancedPlayer {

/**
 * @brief 文件扫描器
 * 
 * 扫描文件夹中的媒体文件
 */
class FileScanner : public QObject {
    Q_OBJECT
    
public:
    explicit FileScanner(QObject* parent = nullptr);
    
    /**
     * @brief 扫描文件夹
     * @param folderPath 文件夹路径
     * @param recursive 是否递归扫描子文件夹
     */
    Q_INVOKABLE void scanFolder(const QString& folderPath, bool recursive = true);
    
    /**
     * @brief 设置支持的文件扩展名
     * @param extensions 扩展名列表（例如：[".mp4", ".mkv"]）
     */
    void setSupportedExtensions(const QStringList& extensions);
    
    /**
     * @brief 获取默认支持的视频扩展名
     */
    static QStringList defaultVideoExtensions();
    
    /**
     * @brief 获取默认支持的音频扩展名
     */
    static QStringList defaultAudioExtensions();
    
    /**
     * @brief 获取所有默认支持的媒体扩展名
     */
    static QStringList defaultMediaExtensions();
    
signals:
    void fileFound(const QString& filePath);
    void scanCompleted(int fileCount);
    
private:
    QStringList supportedExtensions_;
    
    /**
     * @brief 检查文件是否为支持的媒体文件
     */
    bool isMediaFile(const std::filesystem::path& filePath) const;
    
    /**
     * @brief 递归扫描文件夹
     */
    int scanRecursive(const std::filesystem::path& folder);
};

} // namespace AdvancedPlayer

#endif // FILESCANNER_H

