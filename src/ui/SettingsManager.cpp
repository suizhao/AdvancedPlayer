#include "SettingsManager.h"
#include <QStandardPaths>
#include <QDir>
#include <QDebug>

namespace AdvancedPlayer {

SettingsManager& SettingsManager::instance() {
    static SettingsManager instance;
    return instance;
}

SettingsManager::SettingsManager()
    : settings_("AdvancedPlayer", "AdvancedPlayer") {
    qInfo() << "[SettingsManager::SettingsManager] SettingsManager created, config file:" << settings_.fileName();
    
    // 初始化默认播放速度为1.0（如果配置文件中没有这个值）
    if (!settings_.contains("playback/defaultSpeed")) {
        settings_.setValue("playback/defaultSpeed", 1.0);
        qInfo() << "[SettingsManager::SettingsManager] Initialized default playback speed to 1.0";
    }
}

QVariant SettingsManager::get(const QString& key, const QVariant& defaultValue) const {
    return settings_.value(key, defaultValue);
}

void SettingsManager::set(const QString& key, const QVariant& value) {
    settings_.setValue(key, value);
    emit settingsChanged();
}

bool SettingsManager::hardwareAccelerationEnabled() const {
    return settings_.value("playback/hardwareAcceleration", true).toBool();
}

void SettingsManager::setHardwareAccelerationEnabled(bool enabled) {
    settings_.setValue("playback/hardwareAcceleration", enabled);
    settings_.sync();  // 立即同步到磁盘，确保设置立即保存
    emit settingsChanged();
    qDebug() << "[SettingsManager::setHardwareAccelerationEnabled] Hardware acceleration:" << enabled;
}

int SettingsManager::defaultVolume() const {
    return settings_.value("audio/defaultVolume", 100).toInt();
}

void SettingsManager::setDefaultVolume(int volume) {
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    
    settings_.setValue("audio/defaultVolume", volume);
    emit settingsChanged();
    qDebug() << "[SettingsManager::setDefaultVolume] Default volume:" << volume;
}

bool SettingsManager::rememberPlaybackPosition() const {
    return settings_.value("playback/rememberPosition", false).toBool();
}

void SettingsManager::setRememberPlaybackPosition(bool remember) {
    settings_.setValue("playback/rememberPosition", remember);
    emit settingsChanged();
    qDebug() << "[SettingsManager::setRememberPlaybackPosition] Remember playback position:" << remember;
}

QString SettingsManager::screenshotDirectory() const {
    QString defaultDir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    defaultDir += "/AdvancedPlayer";
    
    return settings_.value("screenshot/directory", defaultDir).toString();
}

void SettingsManager::setScreenshotDirectory(const QString& dir) {
    settings_.setValue("screenshot/directory", dir);
    
    // 确保目录存在
    QDir().mkpath(dir);
    
    emit settingsChanged();
    qDebug() << "[SettingsManager::setScreenshotDirectory] Screenshot directory:" << dir;
}

QString SettingsManager::screenshotFormat() const {
    QString format = settings_.value("screenshot/format", "png").toString().toLower();
    if (format != "png" && format != "jpg" && format != "bmp") {
        return "png";
    }
    return format;
}

void SettingsManager::setScreenshotFormat(const QString& format) {
    QString normalizedFormat = format.toLower();
    if (normalizedFormat != "png" && normalizedFormat != "jpg" && normalizedFormat != "bmp") {
        normalizedFormat = "png";
    }

    settings_.setValue("screenshot/format", normalizedFormat);
    emit settingsChanged();
    qDebug() << "[SettingsManager::setScreenshotFormat] Screenshot format:" << normalizedFormat;
}

bool SettingsManager::autoPlayNext() const {
    return settings_.value("playlist/autoPlayNext", true).toBool();
}

void SettingsManager::setAutoPlayNext(bool autoPlay) {
    settings_.setValue("playlist/autoPlayNext", autoPlay);
    emit settingsChanged();
    qDebug() << "[SettingsManager::setAutoPlayNext] Auto play next:" << autoPlay;
}

double SettingsManager::defaultPlaybackSpeed() const {
    return settings_.value("playback/defaultSpeed", 1.0).toDouble();
}

void SettingsManager::setDefaultPlaybackSpeed(double speed) {
    if (speed < 0.25) speed = 0.25;
    if (speed > 2.0) speed = 2.0;
    
    settings_.setValue("playback/defaultSpeed", speed);
    emit settingsChanged();
    qDebug() << "[SettingsManager::setDefaultPlaybackSpeed] Default playback speed:" << speed;
}

int SettingsManager::theme() const {
    // 默认值为1（深色主题）
    return settings_.value("interface/theme", 1).toInt();
}

void SettingsManager::setTheme(int theme) {
    // 限制范围：0=浅色, 1=深色, 2=跟随系统
    if (theme < 0) theme = 0;
    if (theme > 2) theme = 2;
    
    settings_.setValue("interface/theme", theme);
    emit settingsChanged();
    qDebug() << "[SettingsManager::setTheme] Theme set to:" << theme;
}

void SettingsManager::savePlaybackPosition(const QString& filePath, double position) {
    if (filePath.isEmpty() || position < 0) {
        return;
    }
    
    // 使用文件的绝对路径作为key，确保唯一性
    // 对于网络流，直接使用URL
    QString key = QString("playback/positions/%1").arg(filePath);
    settings_.setValue(key, position);
    qDebug() << "[SettingsManager::savePlaybackPosition] Saved playback position - file:" << filePath << ", position:" << position << "s";
}

double SettingsManager::getPlaybackPosition(const QString& filePath) const {
    if (filePath.isEmpty()) {
        return -1.0;
    }
    
    QString key = QString("playback/positions/%1").arg(filePath);
    bool ok = false;
    double position = settings_.value(key, -1.0).toDouble(&ok);
    
    if (ok && position >= 0) {
        qDebug() << "[SettingsManager::getPlaybackPosition] Loaded playback position - file:" << filePath << ", position:" << position << "s";
        return position;
    }
    
    return -1.0;
}

void SettingsManager::clearPlaybackPosition(const QString& filePath) {
    if (filePath.isEmpty()) {
        return;
    }
    
    QString key = QString("playback/positions/%1").arg(filePath);
    settings_.remove(key);
    qDebug() << "[SettingsManager::clearPlaybackPosition] Cleared playback position - file:" << filePath;
}

void SettingsManager::save() {
    settings_.sync();
    qInfo() << "[SettingsManager::save] Settings saved";
}

void SettingsManager::restoreDefaults() {
    settings_.clear();
    emit settingsChanged();
    qInfo() << "[SettingsManager::restoreDefaults] Restored default settings";
}

} // namespace AdvancedPlayer

