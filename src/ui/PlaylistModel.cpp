#include "PlaylistModel.h"
#include <QFileInfo>
#include <QDebug>
#include <fstream>

namespace AdvancedPlayer {

PlaylistModel::PlaylistModel(QObject* parent)
    : QAbstractListModel(parent) {
    qInfo() << "[PlaylistModel::PlaylistModel] PlaylistModel created";
}

int PlaylistModel::rowCount(const QModelIndex& parent) const {
    // 只有树形 / 层级结构（TreeModel），parent 才会有效、存在子节点
    // 只要不是根节点，直接返回 0，代表没有任何子层级
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(items_.size());
}

QVariant PlaylistModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= static_cast<int>(items_.size())) {
        return QVariant();
    }
    
    const auto& item = items_[index.row()];
    
    switch (role) {
        case FilePathRole:
            return QString::fromStdString(item.filePath.string());
        case FileNameRole:
            return item.fileName;
        case DurationRole:
            return QVariant::fromValue(item.durationMs);
        case SizeRole:
            return QVariant::fromValue(item.sizeBytes);
        case IndexRole:
            return index.row();
        default:
            return QVariant();
    }
}

QHash<int, QByteArray> PlaylistModel::roleNames() const {
    QHash<int, QByteArray> roles;
    roles[FilePathRole] = "filePath";
    roles[FileNameRole] = "fileName";
    roles[DurationRole] = "duration";
    roles[SizeRole] = "size";
    roles[IndexRole] = "index";
    return roles;
}

void PlaylistModel::addMedia(const QString& filePath) {
    // 处理 URL 格式路径（file:///...）
    QString localPath = filePath;
    if (localPath.startsWith("file:///")) {
        localPath = localPath.mid(8);  // 移除 "file:///"
    } else if (localPath.startsWith("file://")) {
        localPath = localPath.mid(7);  // 移除 "file://"
    }
    // Windows 路径处理：/C:/ -> C:/
    if (localPath.startsWith("/") && localPath.length() > 2 && localPath.at(2) == ':') {
        localPath = localPath.mid(1);
    }
    
    QFileInfo fileInfo(localPath);
    
    if (!fileInfo.exists()) {
        qWarning() << "[PlaylistModel::addMedia] File does not exist:" << localPath << "(original path:" << filePath << ")";
        return;
    }
    
    // 规范化路径用于比较（转换为绝对路径并统一转换为小写，用于大小写不敏感比较）
    QString normalizedPath = QFileInfo(localPath).absoluteFilePath().toLower();
    
    // 使用哈希集合进行 O(1) 去重检查（优化：从 O(n) 线性搜索优化为 O(1) 哈希查找）
    if (existingPaths_.contains(normalizedPath)) {
        qDebug() << "[PlaylistModel::addMedia] File already exists in playlist, skipping:" << fileInfo.fileName();
        return;
    }
    
    MediaItem item;
    item.filePath = localPath.toStdString();
    item.fileName = fileInfo.fileName();
    item.sizeBytes = fileInfo.size();
    // 获取媒体时长（异步，暂时设置为 0）
    // 注意：完整实现需要使用 FFmpeg 打开文件以获取时长，这会影响性能
    // 可以在后台线程中异步获取
    item.durationMs = 0;
    
    beginInsertRows(QModelIndex(), static_cast<int>(items_.size()), 
                    static_cast<int>(items_.size()));
    items_.push_back(item);
    // 将规范化路径添加到哈希集合中（O(1) 平均时间复杂度）
    existingPaths_.insert(normalizedPath);
    endInsertRows();
    
    emit countChanged();
    
    qDebug() << "[PlaylistModel::addMedia] Added to playlist:" << item.fileName;
}

void PlaylistModel::removeMedia(int index) {
    if (index < 0 || index >= static_cast<int>(items_.size())) {
        return;
    }
    
    // 从哈希集合中移除路径（O(1) 平均时间复杂度）
    QString normalizedPath = QFileInfo(QString::fromStdString(items_[index].filePath.string())).absoluteFilePath().toLower();
    existingPaths_.remove(normalizedPath);
    
    beginRemoveRows(QModelIndex(), index, index);
    items_.erase(items_.begin() + index);
    endRemoveRows();
    
    if (currentIndex_ == index) {
        currentIndex_ = -1;
        emit currentIndexChanged();
    } else if (currentIndex_ > index) {
        currentIndex_--;
        emit currentIndexChanged();
    }
    
    emit countChanged();
    
    qDebug() << "[PlaylistModel::removeMedia] Removed playlist index:" << index;
}

void PlaylistModel::clearPlaylist() {
    beginResetModel();
    items_.clear();
    existingPaths_.clear();  // 清空哈希集合（O(n) 时间复杂度，但通常很快）
    endResetModel();
    
    currentIndex_ = -1;
    emit currentIndexChanged();
    emit countChanged();
    
    qDebug() << "[PlaylistModel::clearPlaylist] Playlist cleared";
}

void PlaylistModel::playIndex(int index) {
    qInfo() << "[PlaylistModel::playIndex] Called, index:" << index
            << ", list size:" << items_.size();
    
    if (index < 0 || index >= static_cast<int>(items_.size())) {
        qWarning() << "[PlaylistModel::playIndex] Index out of range, cancel play";
        return;
    }
    
    currentIndex_ = index;
    QString filePath = QString::fromStdString(items_[index].filePath.string());
    
    qInfo() << "[PlaylistModel::playIndex] Set current index:" << index
            << ", file path:" << filePath;
    
    emit currentIndexChanged();
    emit playRequested(filePath);
    
    qInfo() << "[PlaylistModel::playIndex] playRequested signal emitted";
}

void PlaylistModel::setCurrentIndex(int index) {
    if (index < 0 || index >= static_cast<int>(items_.size())) {
        qWarning() << "[PlaylistModel::setCurrentIndex] Index out of range:" << index;
        return;
    }
    
    if (currentIndex_ != index) {
        currentIndex_ = index;
        emit currentIndexChanged();
        qDebug() << "[PlaylistModel::setCurrentIndex] Index updated to:" << index << "(without triggering play request)";
    }
}

void PlaylistModel::playNext() {
    qInfo() << "[PlaylistModel::playNext] Called, current index:" << currentIndex_
            << ", item count:" << items_.size();
    
    if (items_.empty()) {
        qWarning() << "[PlaylistModel::playNext] Playlist is empty, cannot play next";
        return;
    }
    
    int nextIndex = (currentIndex_ + 1) % static_cast<int>(items_.size());
    qInfo() << "[PlaylistModel::playNext] Next index:" << nextIndex;
    playIndex(nextIndex);
}

void PlaylistModel::playPrevious() {
    if (items_.empty()) {
        return;
    }
    
    int prevIndex = currentIndex_ - 1;
    if (prevIndex < 0) {
        prevIndex = static_cast<int>(items_.size()) - 1;
    }
    
    playIndex(prevIndex);
}

void PlaylistModel::moveItem(int from, int to) {
    if (from < 0 || from >= static_cast<int>(items_.size()) ||
        to < 0 || to >= static_cast<int>(items_.size()) ||
        from == to) {
        return;
    }

    if (beginMoveRows(QModelIndex(), from, from, QModelIndex(),
                      to > from ? to + 1 : to)) {
        auto item = items_[from];
        items_.erase(items_.begin() + from);
        items_.insert(items_.begin() + to, item);
        endMoveRows();

        qDebug() << "[PlaylistModel::moveItem] Moved item:" << from << "->" << to;
    }
}

bool PlaylistModel::loadPlaylist(const QString& filePath) {
    std::filesystem::path path(filePath.toStdString());

    if (!std::filesystem::exists(path)) {
        qWarning() << "[PlaylistModel::loadPlaylist] Playlist file does not exist:" << filePath;
        return false;
    }

    std::vector<std::filesystem::path> paths;

    // 根据扩展名选择解析器
    QString ext = QString::fromStdString(path.extension().string()).toLower();
    if (ext == ".m3u" || ext == ".m3u8") {
        paths = parseM3U(path);
    } else if (ext == ".pls") {
        paths = parsePLS(path);
    } else {
        qWarning() << "[PlaylistModel::loadPlaylist] Unsupported playlist format:" << ext;
        return false;
    }

    // 添加到播放列表
    for (const auto& p : paths) {
        addMedia(QString::fromStdString(p.string()));
    }

    qInfo() << "[PlaylistModel::loadPlaylist] Playlist loaded:" << filePath << ", total" << paths.size() << "items";
    return true;
}

bool PlaylistModel::savePlaylist(const QString& filePath, const QString& format) {
    std::filesystem::path path(filePath.toStdString());

    std::ofstream file(path);
    if (!file.is_open()) {
        qWarning() << "[PlaylistModel::savePlaylist] Failed to create playlist file:" << filePath;
        return false;
    }

    if (format.toLower() == "m3u") {
        file << "#EXTM3U\n";
        for (const auto& item : items_) {
            file << "#EXTINF:" << (item.durationMs / 1000) << ","
                 << item.fileName.toStdString() << "\n";
            file << item.filePath.string() << "\n";
        }
    } else if (format.toLower() == "pls") {
        file << "[playlist]\n";
        for (size_t i = 0; i < items_.size(); ++i) {
            file << "File" << (i + 1) << "=" << items_[i].filePath.string() << "\n";
            file << "Title" << (i + 1) << "=" << items_[i].fileName.toStdString() << "\n";
            file << "Length" << (i + 1) << "=" << (items_[i].durationMs / 1000) << "\n";
        }
        file << "NumberOfEntries=" << items_.size() << "\n";
    } else {
        qWarning() << "[PlaylistModel::savePlaylist] Unsupported playlist format:" << format;
        return false;
    }

    file.close();

    qInfo() << "[PlaylistModel::savePlaylist] Playlist saved:" << filePath;
    return true;
}

QString PlaylistModel::getFilePath(int index) const {
    if (index < 0 || index >= static_cast<int>(items_.size())) {
        return QString();
    }
    return QString::fromStdString(items_[index].filePath.string());
}

std::vector<std::filesystem::path> PlaylistModel::parseM3U(const std::filesystem::path& path) {
    std::vector<std::filesystem::path> paths;
    std::ifstream file(path);
    
    if (!file.is_open()) {
        return paths;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        // 跳过注释和空行
        if (line.empty() || line[0] == '#') {
            continue;
        }
        
        std::filesystem::path itemPath(line);
        if (std::filesystem::exists(itemPath)) {
            paths.push_back(itemPath);
        }
    }
    
    return paths;
}

std::vector<std::filesystem::path> PlaylistModel::parsePLS(const std::filesystem::path& path) {
    std::vector<std::filesystem::path> paths;
    std::ifstream file(path);
    
    if (!file.is_open()) {
        return paths;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        // 查找 File 条目
        if (line.find("File") == 0) {
            size_t pos = line.find('=');
            if (pos != std::string::npos) {
                std::string filePath = line.substr(pos + 1);
                std::filesystem::path itemPath(filePath);
                if (std::filesystem::exists(itemPath)) {
                    paths.push_back(itemPath);
                }
            }
        }
    }
    
    return paths;
}

} // namespace AdvancedPlayer

