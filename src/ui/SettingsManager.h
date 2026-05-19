#ifndef SETTINGSMANAGER_H
#define SETTINGSMANAGER_H

#include <QObject>
#include <QSettings>
#include <QString>
#include <QVariant>

namespace AdvancedPlayer {

/**
 * @brief 设置管理器
 * 
 * 管理应用程序设置的存储和读取
 */
class SettingsManager : public QObject {
    Q_OBJECT
    
    // 通用设置属性
    Q_PROPERTY(bool hardwareAccelerationEnabled READ hardwareAccelerationEnabled
               WRITE setHardwareAccelerationEnabled NOTIFY settingsChanged)
    Q_PROPERTY(bool rememberPlaybackPosition READ rememberPlaybackPosition 
               WRITE setRememberPlaybackPosition NOTIFY settingsChanged)
    Q_PROPERTY(QString screenshotDirectory READ screenshotDirectory 
               WRITE setScreenshotDirectory NOTIFY settingsChanged)
    Q_PROPERTY(QString screenshotFormat READ screenshotFormat
               WRITE setScreenshotFormat NOTIFY settingsChanged)
    Q_PROPERTY(bool autoPlayNext READ autoPlayNext WRITE setAutoPlayNext NOTIFY settingsChanged)

    Q_PROPERTY(double defaultPlaybackSpeed READ defaultPlaybackSpeed 
               WRITE setDefaultPlaybackSpeed NOTIFY settingsChanged)
    Q_PROPERTY(int defaultVolume READ defaultVolume WRITE setDefaultVolume NOTIFY settingsChanged)
    Q_PROPERTY(int theme READ theme WRITE setTheme NOTIFY settingsChanged)
    
public:
    static SettingsManager& instance();
    
    // 硬件加速
    bool hardwareAccelerationEnabled() const;
    void setHardwareAccelerationEnabled(bool enabled);
    
    // 记住播放位置
    bool rememberPlaybackPosition() const;
    void setRememberPlaybackPosition(bool remember);
    
    // 截图目录
    QString screenshotDirectory() const;
    void setScreenshotDirectory(const QString& dir);

    // 截图格式
    QString screenshotFormat() const;
    void setScreenshotFormat(const QString& format);
    
    // 自动播放下一个
    bool autoPlayNext() const;
    void setAutoPlayNext(bool autoPlay);
    
    // 播放位置管理（用于记住播放位置功能）
    // 保存指定文件的播放位置（秒）
    void savePlaybackPosition(const QString& filePath, double position);
    
    // 读取指定文件的播放位置（秒），如果不存在则返回-1
    double getPlaybackPosition(const QString& filePath) const;
    
    // 清除指定文件的播放位置
    void clearPlaybackPosition(const QString& filePath);

    // 默认播放速度
    double defaultPlaybackSpeed() const;
    void setDefaultPlaybackSpeed(double speed);

    // 默认音量
    int defaultVolume() const;
    void setDefaultVolume(int volume);

    // 主题设置（0=浅色, 1=深色, 2=跟随系统）
    int theme() const;
    void setTheme(int theme);
    
    // 通用设置
    Q_INVOKABLE QVariant get(const QString& key, const QVariant& defaultValue = QVariant()) const;
    Q_INVOKABLE void set(const QString& key, const QVariant& value);

    // 保存所有设置
    Q_INVOKABLE void save();
    
    // 恢复默认设置
    Q_INVOKABLE void restoreDefaults();
    
signals:
    void settingsChanged();
    
private:
    SettingsManager();
    ~SettingsManager() = default;
    
    // 禁用拷贝
    SettingsManager(const SettingsManager&) = delete;
    SettingsManager& operator=(const SettingsManager&) = delete;
    
    QSettings settings_;
};

} // namespace AdvancedPlayer

#endif // SETTINGSMANAGER_H

