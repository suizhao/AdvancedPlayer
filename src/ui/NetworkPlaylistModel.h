#ifndef NETWORKPLAYLISTMODEL_H
#define NETWORKPLAYLISTMODEL_H

#include <QAbstractListModel>
#include <QString>
#include <QUrl>
#include <QSet>
#include <QDateTime>
#include <vector>

namespace AdvancedPlayer {

/**
 * @brief 网络流媒体项数据结构
 */
struct NetworkMediaItem {
    QString url{};           // 网络流URL
    QString title{};         // 显示标题
    QString source{};        // 来源标识（用于区分不同服务器）
    QString thumbnailUrl{};  // 缩略图URL（可选）
    QString description{};   // 描述信息（可选）
    QDateTime addedTime{};   // 添加时间
    bool isLive{false};    // 是否为直播流
};

/**
 * @brief 网络流播放列表数据模型
 * 
 * 为 QML 提供网络流播放列表数据
 * 支持从热门服务器拉取或用户手动添加URL
 */
class NetworkPlaylistModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool isLoading READ isLoading NOTIFY loadingStateChanged)
    
public:
    enum Roles {
        UrlRole = Qt::UserRole + 1,
        TitleRole,
        SourceRole,
        ThumbnailUrlRole,
        DescriptionRole,
        AddedTimeRole,
        IsLiveRole,
        IndexRole
    };
    
    explicit NetworkPlaylistModel(QObject* parent = nullptr);
    ~NetworkPlaylistModel() override = default;
    
    // QAbstractListModel 接口
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    
    // 属性访问器
    int currentIndex() const { return currentIndex_; }
    int count() const { return static_cast<int>(items_.size()); }
    bool isLoading() const { return isLoading_; }
    
public slots:
    /**
     * @brief 添加网络流URL
     * @param url 网络流URL
     * @param title 标题（可选，若空则自动提取）
     * @param source 来源标识（默认为"user"表示用户添加）
     * @return 是否添加成功
     */
    bool addStream(const QString& url, const QString& title = QString(), 
                   const QString& source = "user");
    
    /**
     * @brief 批量添加网络流
     * @param items 网络流列表
     * @param source 来源标识
     */
    void addStreams(const QList<AdvancedPlayer::NetworkMediaItem>& items, const QString& source);
    
    /**
     * @brief 移除网络流
     * @param index 索引
     */
    void removeStream(int index);
    
    /**
     * @brief 清空播放列表
     */
    void clearPlaylist();
    
    /**
     * @brief 清空指定来源的项目
     * @param source 来源标识
     */
    void clearBySource(const QString& source);
    
    /**
     * @brief 播放指定索引的项目
     * @param index 索引
     */
    void playIndex(int index);
    
    /**
     * @brief 只设置当前索引，不触发播放请求
     * @param index 索引
     */
    void setCurrentIndex(int index);
    
    /**
     * @brief 获取指定索引的URL
     * @param index 索引
     * @return URL字符串
     */
    QString getUrl(int index) const;
    
    /**
     * @brief 获取指定索引的标题
     * @param index 索引
     * @return 标题字符串
     */
    QString getTitle(int index) const;
    
    /**
     * @brief 播放下一个
     */
    void playNext();
    
    /**
     * @brief 播放上一个
     */
    void playPrevious();
    
    /**
     * @brief 从服务器刷新网络流列表
     * @param serverUrl 服务器API地址
     * @note 预留接口，实际实现需要网络请求
     */
    void refreshFromServer(const QString& serverUrl);
    
    /**
     * @brief 保存播放列表到本地缓存
     * @return 是否保存成功
     */
    bool saveToCache();
    
    /**
     * @brief 从本地缓存加载播放列表
     * @return 是否加载成功
     */
    bool loadFromCache();
    
signals:
    void currentIndexChanged();
    void countChanged();
    void loadingStateChanged();
    void playRequested(const QString& url);
    void errorOccurred(const QString& message);
    void refreshCompleted(bool success, int count);
    
private:
    std::vector<NetworkMediaItem> items_{};
    int currentIndex_{-1};
    bool isLoading_{false};
    
    // 使用URL的哈希集合进行去重（统一转换为小写）
    QSet<QString> existingUrls_{};
    
    /**
     * @brief 验证URL格式
     * @param url 待验证的URL
     * @return 是否为有效的网络流URL
     */
    bool isValidStreamUrl(const QString& url) const;
    
    /**
     * @brief 从URL提取标题
     * @param url 网络流URL
     * @return 提取的标题
     */
    QString extractTitleFromUrl(const QString& url) const;
    
    /**
     * @brief 获取缓存文件路径
     * @return 缓存文件的完整路径
     */
    QString getCacheFilePath() const;
};

} // namespace AdvancedPlayer

#endif // NETWORKPLAYLISTMODEL_H

