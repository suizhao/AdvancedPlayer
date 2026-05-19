#ifndef PLAYLISTMODEL_H
#define PLAYLISTMODEL_H

#include <QAbstractListModel>
#include <QString>
#include <QSet>
#include <vector>
#include <filesystem>

namespace AdvancedPlayer {

/**
 * @brief 播放列表数据模型
 * 
 * 为 QML 提供播放列表数据
 */
class PlaylistModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    
public:
    enum Roles {
        FilePathRole = Qt::UserRole + 1,
        FileNameRole,
        DurationRole,
        SizeRole,
        IndexRole
    };
    
    explicit PlaylistModel(QObject* parent = nullptr);
    
    // QAbstractListModel 接口
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    
    // 属性访问器
    int currentIndex() const { return currentIndex_; }
    int count() const { return static_cast<int>(items_.size()); }
    
public slots:
    /**
     * @brief 添加媒体文件
     * @param filePath 文件路径
     */
    void addMedia(const QString& filePath);
    
    /**
     * @brief 移除媒体文件
     * @param index 索引
     */
    void removeMedia(int index);
    
    /**
     * @brief 清空播放列表
     */
    void clearPlaylist();
    
    /**
     * @brief 播放指定索引的项目
     * @param index 索引
     */
    void playIndex(int index);
    
    /**
     * @brief 只设置当前索引，不触发播放请求
     * @param index 索引
     * @note 用于在外部已经触发播放时只更新UI状态，避免重复调用openFile
     */
    void setCurrentIndex(int index);
    
    /**
     * @brief 播放下一个
     */
    void playNext();
    
    /**
     * @brief 播放上一个
     */
    void playPrevious();

    /**
     * @brief 移动项目
     * @param from 源索引
     * @param to 目标索引
     */
    void moveItem(int from, int to);

    /**
     * @brief 加载播放列表文件
     * @param filePath 播放列表文件路径（M3U/PLS）
     */
    bool loadPlaylist(const QString& filePath);

    /**
     * @brief 保存播放列表
     * @param filePath 保存路径
     * @param format 格式（M3U/PLS）
     */
    bool savePlaylist(const QString& filePath, const QString& format);

    /**
     * @brief 获取指定索引的文件路径
     * @param index 索引
     * @return 文件路径，如果索引无效则返回空字符串
     */
    QString getFilePath(int index) const;
    
signals:
    void currentIndexChanged();
    void countChanged();
    void playRequested(const QString& filePath);
    
private:
    struct MediaItem {
        std::filesystem::path filePath{};
        QString fileName{};
        int64_t durationMs{0};
        uint64_t sizeBytes{0};
    };
    
    std::vector<MediaItem> items_{};
    int currentIndex_{-1};
    
    // 使用哈希集合存储已存在的文件路径（规范化后的绝对路径，统一转换为小写），用于 O(1) 去重检查
    // 时间复杂度：O(1) 平均，O(n) 最坏（哈希冲突）
    // 空间复杂度：O(n)，其中 n 是播放列表中的文件数量
    QSet<QString> existingPaths_{};
    
    /**
     * @brief 解析 M3U 播放列表
     */
    std::vector<std::filesystem::path> parseM3U(const std::filesystem::path& path);
    
    /**
     * @brief 解析 PLS 播放列表
     */
    std::vector<std::filesystem::path> parsePLS(const std::filesystem::path& path);
};

} // namespace AdvancedPlayer

#endif // PLAYLISTMODEL_H

