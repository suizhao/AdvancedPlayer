#include "FileScanner.h"
#include <QDebug>
#include <QDir>

namespace AdvancedPlayer {

FileScanner::FileScanner(QObject* parent)
    : QObject(parent) {
    // 默认支持所有媒体格式
    supportedExtensions_ = defaultMediaExtensions();
    qInfo() << "[FileScanner::FileScanner] FileScanner created";
}

void FileScanner::scanFolder(const QString& folderPath, bool recursive) {
    std::filesystem::path folder(folderPath.toStdString());
    
    if (!std::filesystem::exists(folder) || !std::filesystem::is_directory(folder)) {
        qWarning() << "[FileScanner::scanFolder] Folder does not exist or is not a directory:" << folderPath;
        emit scanCompleted(0);
        return;
    }
    
    qInfo() << "[FileScanner::scanFolder] Scanning folder:" << folderPath << ", recursive:" << recursive;
    int count = 0;
    
    try {
        if (recursive) {
            count = scanRecursive(folder);
        } else {
            // 仅扫描当前目录
            for (const auto& entry : std::filesystem::directory_iterator(folder)) {
                if (entry.is_regular_file() && isMediaFile(entry.path())) {
                    QString filePath = QString::fromStdString(entry.path().string());
                    emit fileFound(filePath);
                    ++count;
                }
            }
        }
    } catch (const std::exception& e) {
        qWarning() << "[FileScanner::scanFolder] Error while scanning folder:" << e.what();
    }
    
    emit scanCompleted(count);
    qInfo() << "[FileScanner::scanFolder] Scan finished, found" << count << "media files";
}

void FileScanner::setSupportedExtensions(const QStringList& extensions) {
    supportedExtensions_ = extensions;
}

QStringList FileScanner::defaultVideoExtensions() {
    return QStringList{
        ".mp4", ".mkv", ".avi", ".mov", ".flv", 
        ".wmv", ".webm", ".m4v", ".mpg", ".mpeg",
        ".3gp", ".ogv", ".ts", ".m2ts"
    };
}

QStringList FileScanner::defaultAudioExtensions() {
    return QStringList{
        ".mp3", ".flac", ".aac", ".wav", ".ogg",
        ".wma", ".m4a", ".opus", ".ape", ".ac3"
    };
}

QStringList FileScanner::defaultMediaExtensions() {
    QStringList all{};
    all.append(defaultVideoExtensions());
    all.append(defaultAudioExtensions());
    return all;
}

bool FileScanner::isMediaFile(const std::filesystem::path& filePath) const {
    QString ext = QString::fromStdString(filePath.extension().string()).toLower();
    return supportedExtensions_.contains(ext);
}

int FileScanner::scanRecursive(const std::filesystem::path& folder) {
    int count = 0;
    try {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(
                 folder, std::filesystem::directory_options::skip_permission_denied)) {
            
            if (entry.is_regular_file() && isMediaFile(entry.path())) {
                QString filePath = QString::fromStdString(entry.path().string());
                emit fileFound(filePath);
                ++count;
            }
        }
    } catch (const std::exception& e) {
        qWarning() << "[FileScanner::scanRecursive] Error during recursive scan:" << e.what();
    }
    return count;
}

} // namespace AdvancedPlayer

