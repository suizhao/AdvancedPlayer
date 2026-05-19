#include "NetworkPlaylistModel.h"
#include <QDebug>
#include <QUrl>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

namespace AdvancedPlayer {

NetworkPlaylistModel::NetworkPlaylistModel(QObject* parent)
    : QAbstractListModel(parent) {
    qInfo() << "[NetworkPlaylistModel::NetworkPlaylistModel] NetworkPlaylistModel created";
    
    // 尝试从缓存加载
    loadFromCache();
}

int NetworkPlaylistModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(items_.size());
}

QVariant NetworkPlaylistModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= static_cast<int>(items_.size())) {
        return QVariant();
    }
    
    const auto& item = items_[index.row()];
    
    switch (role) {
        case UrlRole:
            return item.url;
        case TitleRole:
            return item.title;
        case SourceRole:
            return item.source;
        case ThumbnailUrlRole:
            return item.thumbnailUrl;
        case DescriptionRole:
            return item.description;
        case AddedTimeRole:
            return item.addedTime;
        case IsLiveRole:
            return QVariant::fromValue(item.isLive);
        case IndexRole:
            return index.row();
        default:
            return QVariant();
    }
}

QHash<int, QByteArray> NetworkPlaylistModel::roleNames() const {
    QHash<int, QByteArray> roles;
    roles[UrlRole] = "url";
    roles[TitleRole] = "title";
    roles[SourceRole] = "source";
    roles[ThumbnailUrlRole] = "thumbnailUrl";
    roles[DescriptionRole] = "description";
    roles[AddedTimeRole] = "addedTime";
    roles[IsLiveRole] = "isLive";
    roles[IndexRole] = "index";
    return roles;
}

bool NetworkPlaylistModel::isValidStreamUrl(const QString& url) const {
    // 支持的网络协议
    static const QStringList validProtocols = {
        "http://", "https://", "rtmp://", "rtsp://",
        "rtp://", "udp://", "tcp://", "mms://", "mmsh://",
        "hls://", "dash://"
    };
    
    QString lowerUrl = url.toLower().trimmed();
    
    for (const QString& protocol : validProtocols) {
        if (lowerUrl.startsWith(protocol)) {
            return true;
        }
    }
    
    return false;
}

QString NetworkPlaylistModel::extractTitleFromUrl(const QString& url) const {
    QUrl qurl(url);
    
    // 尝试从路径中提取文件名
    QString fileName = qurl.fileName();
    if (!fileName.isEmpty()) {
        // 移除文件扩展名
        int dotIndex = fileName.lastIndexOf('.');
        if (dotIndex > 0) {
            fileName = fileName.left(dotIndex);
        }
        return fileName;
    }
    
    // 如果没有文件名，使用主机名
    QString host = qurl.host();
    if (!host.isEmpty()) {
        return host;
    }
    
    // 最后返回URL本身
    return url;
}

bool NetworkPlaylistModel::addStream(const QString& url, const QString& title, 
                                     const QString& source) {
    QString trimmedUrl = url.trimmed();
    
    if (!isValidStreamUrl(trimmedUrl)) {
        qWarning() << "[NetworkPlaylistModel::addStream] Invalid stream URL:" << url;
        emit errorOccurred(tr("无效的网络流URL: %1").arg(url));
        return false;
    }
    
    // 规范化URL用于去重
    QString normalizedUrl = trimmedUrl.toLower();
    
    // 去重检查
    if (existingUrls_.contains(normalizedUrl)) {
        qDebug() << "[NetworkPlaylistModel::addStream] URL already exists, skipping:" << url;
        return false;
    }
    
    NetworkMediaItem item{};
    item.url = trimmedUrl;
    item.title = title.isEmpty() ? extractTitleFromUrl(trimmedUrl) : title;
    item.source = source;
    item.addedTime = QDateTime::currentDateTime();
    
    // 检测是否为直播流（简单启发式判断）
    QString lowerUrl = trimmedUrl.toLower();
    item.isLive = lowerUrl.contains("live") || 
                  lowerUrl.startsWith("rtmp://") ||
                  lowerUrl.startsWith("rtsp://") ||
                  lowerUrl.contains("/live/") ||
                  lowerUrl.contains("m3u8");
    
    beginInsertRows(QModelIndex(), static_cast<int>(items_.size()), 
                    static_cast<int>(items_.size()));
    items_.push_back(item);
    existingUrls_.insert(normalizedUrl);
    endInsertRows();
    
    emit countChanged();
    
    qInfo() << "[NetworkPlaylistModel::addStream] Added network stream:" << item.title
            << "source:" << source;
    
    // 自动保存到缓存
    saveToCache();
    
    return true;
}

void NetworkPlaylistModel::addStreams(const QList<NetworkMediaItem>& items, 
                                      const QString& source) {
    if (items.isEmpty()) {
        return;
    }
    
    int addedCount = 0;
    
    for (const auto& item : items) {
        QString normalizedUrl = item.url.toLower();
        
        if (!isValidStreamUrl(item.url) || existingUrls_.contains(normalizedUrl)) {
            continue;
        }
        
        NetworkMediaItem newItem = item;
        newItem.source = source;
        if (newItem.addedTime.isNull()) {
            newItem.addedTime = QDateTime::currentDateTime();
        }
        
        beginInsertRows(QModelIndex(), static_cast<int>(items_.size()), 
                        static_cast<int>(items_.size()));
        items_.push_back(newItem);
        existingUrls_.insert(normalizedUrl);
        endInsertRows();
        
        ++addedCount;
    }
    
    if (addedCount > 0) {
        emit countChanged();
        saveToCache();
        qInfo() << "[NetworkPlaylistModel::addStreams] Batch added" << addedCount << "network streams";
    }
}

void NetworkPlaylistModel::removeStream(int index) {
    if (index < 0 || index >= static_cast<int>(items_.size())) {
        return;
    }
    
    // 从URL集合中移除
    QString normalizedUrl = items_[index].url.toLower();
    existingUrls_.remove(normalizedUrl);
    
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
    saveToCache();
    
    qDebug() << "[NetworkPlaylistModel::removeStream] Removed index:" << index;
}

void NetworkPlaylistModel::clearPlaylist() {
    beginResetModel();
    items_.clear();
    existingUrls_.clear();
    endResetModel();
    
    currentIndex_ = -1;
    emit currentIndexChanged();
    emit countChanged();
    
    saveToCache();
    
    qDebug() << "[NetworkPlaylistModel::clearPlaylist] Network playlist cleared";
}

void NetworkPlaylistModel::clearBySource(const QString& source) {
    // 从后向前遍历，避免索引失效
    for (int i = static_cast<int>(items_.size()) - 1; i >= 0; --i) {
        if (items_[i].source == source) {
            removeStream(i);
        }
    }
    
    qDebug() << "[NetworkPlaylistModel::clearBySource] Cleared source:" << source;
}

void NetworkPlaylistModel::playIndex(int index) {
    if (index < 0 || index >= static_cast<int>(items_.size())) {
        return;
    }
    
    currentIndex_ = index;
    emit currentIndexChanged();
    emit playRequested(items_[index].url);
    
    qDebug() << "[NetworkPlaylistModel::playIndex] Playing index:" << index
             << "URL:" << items_[index].url;
}

void NetworkPlaylistModel::setCurrentIndex(int index) {
    if (index < 0 || index >= static_cast<int>(items_.size())) {
        return;
    }
    
    if (currentIndex_ != index) {
        currentIndex_ = index;
        emit currentIndexChanged();
    }
}

QString NetworkPlaylistModel::getUrl(int index) const {
    if (index < 0 || index >= static_cast<int>(items_.size())) {
        return QString();
    }
    return items_[index].url;
}

QString NetworkPlaylistModel::getTitle(int index) const {
    if (index < 0 || index >= static_cast<int>(items_.size())) {
        return QString();
    }
    return items_[index].title;
}

void NetworkPlaylistModel::playNext() {
    if (items_.empty()) {
        return;
    }
    
    int nextIndex = (currentIndex_ + 1) % static_cast<int>(items_.size());
    playIndex(nextIndex);
}

void NetworkPlaylistModel::playPrevious() {
    if (items_.empty()) {
        return;
    }
    
    int prevIndex = currentIndex_ - 1;
    if (prevIndex < 0) {
        prevIndex = static_cast<int>(items_.size()) - 1;
    }
    
    playIndex(prevIndex);
}

void NetworkPlaylistModel::refreshFromServer(const QString& serverUrl) {
    // 预留接口：从服务器拉取网络流列表
    // 实际实现需要：
    // 1. 使用QNetworkAccessManager发送HTTP请求
    // 2. 解析JSON响应
    // 3. 调用addStreams批量添加
    
    qInfo() << "[NetworkPlaylistModel::refreshFromServer] Reserved interface, server URL:" << serverUrl;
    
    isLoading_ = true;
    emit loadingStateChanged();
    
    // TODO: 实现网络请求逻辑
    // 目前仅发出完成信号
    isLoading_ = false;
    emit loadingStateChanged();
    emit refreshCompleted(false, 0);
    emit errorOccurred(tr("服务器接口尚未实现"));
}

QString NetworkPlaylistModel::getCacheFilePath() const {
    QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(cacheDir);
    qInfo()<<QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    return cacheDir + "/network_playlist.json";
}

bool NetworkPlaylistModel::saveToCache() {
    QString filePath = getCacheFilePath();
    
    QJsonArray jsonArray;
    for (const auto& item : items_) {
        QJsonObject obj;
        obj["url"] = item.url;
        obj["title"] = item.title;
        obj["source"] = item.source;
        obj["thumbnailUrl"] = item.thumbnailUrl;
        obj["description"] = item.description;
        obj["addedTime"] = item.addedTime.toString(Qt::ISODate);
        obj["isLive"] = item.isLive;
        jsonArray.append(obj);
    }
    
    QJsonDocument doc(jsonArray);
    
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "[NetworkPlaylistModel::saveToCache] Failed to write cache file:" << filePath;
        return false;
    }
    
    file.write(doc.toJson());
    file.close();
    
    qDebug() << "[NetworkPlaylistModel::saveToCache] Saved" << items_.size() << "items to cache";
    return true;
}

bool NetworkPlaylistModel::loadFromCache() {
    QString filePath = getCacheFilePath();
    
    QFile file(filePath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        qDebug() << "[NetworkPlaylistModel::loadFromCache] Cache file does not exist or cannot be read";
        return false;
    }
    
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    
    if (!doc.isArray()) {
        qWarning() << "[NetworkPlaylistModel::loadFromCache] Invalid cache file format";
        return false;
    }
    
    beginResetModel();
    items_.clear();
    existingUrls_.clear();
    
    QJsonArray jsonArray = doc.array();
    for (const auto& value : std::as_const(jsonArray)) {
        QJsonObject obj = value.toObject();
        
        NetworkMediaItem item;
        item.url = obj["url"].toString();
        item.title = obj["title"].toString();
        item.source = obj["source"].toString();
        item.thumbnailUrl = obj["thumbnailUrl"].toString();
        item.description = obj["description"].toString();
        item.addedTime = QDateTime::fromString(obj["addedTime"].toString(), Qt::ISODate);
        item.isLive = obj["isLive"].toBool();
        
        if (isValidStreamUrl(item.url)) {
            items_.push_back(item);
            existingUrls_.insert(item.url.toLower());
        }
    }
    
    endResetModel();
    emit countChanged();
    
    qInfo() << "[NetworkPlaylistModel::loadFromCache] Loaded" << items_.size() << "items from cache";
    return true;
}

} // namespace AdvancedPlayer

