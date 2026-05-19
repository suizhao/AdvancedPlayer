#include "MediaPlayer.h"
#include "PlaybackController.h"
#include "../utils/ScreenshotCapture.h"
#include "../video/VideoOutput.h"
#include "../ui/SettingsManager.h"
#include <QDebug>
#include <QFileInfo>
#include <QTimer>
#include <QDateTime>
#include <QThread>
#include <QWindow>
#include <algorithm>

#ifdef HAS_FFMPEG
extern "C" {
#include <libavutil/frame.h>
}
#endif

#ifdef HAS_SDL3
#include <SDL3/SDL.h>
#endif

namespace AdvancedPlayer {

MediaPlayer::MediaPlayer(QObject* parent)
    : QObject(parent)
    , controller_(std::make_unique<PlaybackController>())
    , screenshotCapture_(std::make_unique<ScreenshotCapture>())
    , retiredReclaimTimer_(new QTimer(this))
    , positionSaveTimer_(new QTimer(this)) {
    
    qInfo() << "[MediaPlayer::MediaPlayer] MediaPlayer created";
    
    // 初始化定时保存器
    positionSaveTimer_->setInterval(POSITION_SAVE_INTERVAL_MS);
    positionSaveTimer_->setSingleShot(false);
    connect(positionSaveTimer_, &QTimer::timeout, this, &MediaPlayer::saveCurrentPosition);

    // 退役控制器回收器：串行、限速回收，避免长期累积
    retiredReclaimTimer_->setInterval(RETIRED_RECLAIM_INTERVAL_MS);
    retiredReclaimTimer_->setSingleShot(false);
    connect(retiredReclaimTimer_, &QTimer::timeout, this, &MediaPlayer::reclaimOneRetiredController);
    
    bindControllerSignals();

    // 转发截图信号到 QML，统一由 MediaPlayer 对外暴露
    connect(screenshotCapture_.get(), &ScreenshotCapture::screenshotSaved,
            this, &MediaPlayer::screenshotSaved);
    connect(screenshotCapture_.get(), &ScreenshotCapture::screenshotFailed,
            this, &MediaPlayer::screenshotFailed);
    
    // VideoOutput 由 QML 侧初始化后再主动回调绑定
}

void MediaPlayer::bindControllerSignals() {
    if (!controller_) {
        return;
    }

    // 连接 PlaybackController 状态信号
    connect(controller_.get(), &PlaybackController::stateChanged, this, [this](PlaybackState state) {
        switch (state) {
            case PlaybackState::Playing:
                isPlaying_ = true;
                isPaused_ = false;
                wasPlayingBeforeStopped_ = true;  // 标记曾经进入播放状态
                // 开始播放后，启用播放进度定时保存
                if (SettingsManager::instance().rememberPlaybackPosition() && !currentFilePath_.isEmpty()) {
                    positionSaveTimer_->start();
                }
                break;
            case PlaybackState::Paused:
                isPlaying_ = false;
                isPaused_ = true;
                wasPlayingBeforeStopped_ = false;  // 手动暂停不视为播放完成
                // 暂停时立即保存一次并停掉定时器
                if (SettingsManager::instance().rememberPlaybackPosition() && !currentFilePath_.isEmpty()) {
                    saveCurrentPosition();
                    positionSaveTimer_->stop();
                }
                break;
            case PlaybackState::Stopped: {
                isPlaying_ = false;
                isPaused_ = false;
                
                // 仅在“确实播放过且有当前文件”时认为是自然播完
                // 避免在初始化/切换文件过程中误触发“自动下一首”
                bool isPlaybackFinished = wasPlayingBeforeStopped_ && !currentFile_.isEmpty();
                
                // 处理播放位置持久化
                if (SettingsManager::instance().rememberPlaybackPosition() && !currentFilePath_.isEmpty()) {
                    positionSaveTimer_->stop();
                    
                    if (isPlaybackFinished) {
                        // 正常播完时清除记忆位置，下次从头播放
                        SettingsManager::instance().clearPlaybackPosition(currentFilePath_);
                    } else {
                        // 非播完停止时保留当前位置
                        saveCurrentPosition();
                    }
                }
                
                position_ = 0;
                
                if (isPlaybackFinished) {
                    qInfo() << "[MediaPlayer] Current media playback finished";
                    
                    // 根据配置决定是否自动播放下一项
                    bool autoPlayNextEnabled = SettingsManager::instance().autoPlayNext();
                    
                    if (autoPlayNextEnabled) {
                        // 延迟触发，避免与停止阶段状态更新竞争
                        QTimer::singleShot(100, this, [this]() {
                            if (autoNextInFlight_) {
                                qInfo() << "[MediaPlayer] Auto-next already in flight, skip duplicate request";
                                return;
                            }
                            autoNextInFlight_ = true;
                            qInfo() << "[MediaPlayer] Emitting requestNextFile signal";
                            emit requestNextFile();
                        });
                    } else {
                        qInfo() << "[MediaPlayer] Auto-play next is disabled";
                    }
                }
                wasPlayingBeforeStopped_ = false;  // 重置播放完成判定标志
                break;
            }
            case PlaybackState::Loading:
                isLoading_ = true;
                break;
            case PlaybackState::Error:
                isPlaying_ = false;
                isPaused_ = false;
                wasPlayingBeforeStopped_ = false;  // 发生错误时不视为播放完成
                break;
        }
        emit playbackStateChanged();
        if (state != PlaybackState::Loading) {
            isLoading_ = false;
            emit loadingStateChanged();
        }
    });
    
    connect(controller_.get(), &PlaybackController::positionChanged, this, [this](int64_t posMs) {
        position_ = posMs / 1000.0; // 毫秒转秒
        emit positionChanged();
        
        // 播放中确保定时器处于开启状态，持续保存进度
        if (SettingsManager::instance().rememberPlaybackPosition() && 
            isPlaying_ && !currentFilePath_.isEmpty()) {
            if (!positionSaveTimer_->isActive()) {
                positionSaveTimer_->start();
            }
        }
    });
    
    connect(controller_.get(), &PlaybackController::durationChanged, this, [this](int64_t durMs) {
        duration_ = durMs / 1000.0; // 毫秒转秒
        emit durationChanged();
    });
    
    connect(controller_.get(), &PlaybackController::errorOccurred, this, [this](const QString& msg) {
        emit error(msg);
        qWarning() << "[MediaPlayer::MediaPlayer] Playback error received:" << msg;
    });
}

void MediaPlayer::rotatePlaybackControllerForAutoNext() {
    if (!controller_) {
        controller_ = std::make_unique<PlaybackController>();
        bindControllerSignals();
        return;
    }

    // 在退役旧控制器前先切断它到 VideoOutput 的帧投递，
    // 避免旧/新控制器同时向同一渲染面推帧导致画面交替闪烁。
    controller_->setVideoOutput(nullptr);
    // 让旧控制器进入暂停，尽量减少继续产生活跃音视频数据。
    controller_->pause();

    QObject::disconnect(controller_.get(), nullptr, this, nullptr);

    // MinGW + winpthread 在线程退出阶段存在偶发 TLS 崩溃。
    // 自动切歌场景下不立即销毁旧控制器，避免触发 worker 线程析构路径。
    retiredControllers_.push_back(controller_.release());
    controller_ = std::make_unique<PlaybackController>();
    bindControllerSignals();
    if (boundVideoOutput_) {
        controller_->setVideoOutput(boundVideoOutput_);
    }
    triggerRetiredControllerReclaim();
}

void MediaPlayer::triggerRetiredControllerReclaim() {
    if (!retiredReclaimTimer_) {
        return;
    }

    if (retiredControllers_.empty()) {
        retiredReclaimTimer_->stop();
        return;
    }

    // 常态下由定时器慢速回收，避免和频繁切歌竞争资源。
    if (!retiredReclaimTimer_->isActive()) {
        retiredReclaimTimer_->start();
    }

    // 超过上限时立即加速回收，避免长期堆积。
    if (!retiredReclaimInProgress_ &&
        static_cast<int>(retiredControllers_.size()) > RETIRED_CONTROLLER_LIMIT) {
        QTimer::singleShot(0, this, &MediaPlayer::reclaimOneRetiredController);
    }
}

void MediaPlayer::reclaimOneRetiredController() {
    if (retiredReclaimInProgress_) {
        return;
    }

    if (retiredControllers_.empty()) {
        if (retiredReclaimTimer_) {
            retiredReclaimTimer_->stop();
        }
        return;
    }

    PlaybackController* candidate = retiredControllers_.front();
    if (!candidate) {
        retiredControllers_.erase(retiredControllers_.begin());
        triggerRetiredControllerReclaim();
        return;
    }

    if (reclaimCloseConnection_) {
        QObject::disconnect(reclaimCloseConnection_);
        reclaimCloseConnection_ = {};
    }

    retiredReclaimInProgress_ = true;
    reclaimingController_ = candidate;

    // 回收闭环：closeMediaAsync 完成后由 closeMediaCompleted 驱动释放。
    reclaimCloseConnection_ = connect(candidate, &PlaybackController::closeMediaCompleted,
                                      this, [this, candidate]() {
        if (reclaimCloseConnection_) {
            QObject::disconnect(reclaimCloseConnection_);
            reclaimCloseConnection_ = {};
        }

        auto it = std::find(retiredControllers_.begin(), retiredControllers_.end(), candidate);
        if (it != retiredControllers_.end()) {
            delete *it;
            retiredControllers_.erase(it);
        }

        reclaimingController_ = nullptr;
        retiredReclaimInProgress_ = false;

        if (retiredControllers_.empty()) {
            if (retiredReclaimTimer_) {
                retiredReclaimTimer_->stop();
            }
            return;
        }

        // 超限时继续快速回收，否则等待下一次定时器滴答。
        if (static_cast<int>(retiredControllers_.size()) > RETIRED_CONTROLLER_LIMIT) {
            QTimer::singleShot(0, this, &MediaPlayer::reclaimOneRetiredController);
        }
    });

    candidate->closeMediaAsync();
}

MediaPlayer::~MediaPlayer() {
    if (positionSaveTimer_) {
        positionSaveTimer_->stop();
    }
    if (retiredReclaimTimer_) {
        retiredReclaimTimer_->stop();
    }
    if (reclaimCloseConnection_) {
        QObject::disconnect(reclaimCloseConnection_);
        reclaimCloseConnection_ = {};
    }

    // 断开所有关联信号
    if (controller_) {
        QObject::disconnect(controller_.get(), nullptr, this, nullptr);
    }

    // 兜底提示：若此时仍有退役对象，说明它们还未等到异步回收完成。
    if (!retiredControllers_.empty()) {
        qWarning() << "[MediaPlayer::~MediaPlayer] Retired controllers still pending:"
                   << retiredControllers_.size()
                   << ", they will be kept leaked to avoid unsafe join on shutdown";
    }

}

void MediaPlayer::setPosition(double pos) {
    if (!controller_) {
        return;
    }
    if (duration_ > 0.0) {
        if (pos < 0.0) pos = 0.0;
        if (pos > duration_) pos = duration_;
    } else if (pos < 0.0) {
        pos = 0.0;
    }

    const int64_t targetMs = static_cast<int64_t>(pos * 1000.0);
    if (!controller_->seekTo(targetMs)) {
        qWarning() << "[MediaPlayer::setPosition] Seek failed, target:" << targetMs << "ms";
        return;
    }

    position_ = static_cast<double>(targetMs) / 1000.0;
    emit positionChanged();
}

void MediaPlayer::setVolume(double vol) {
    if (vol < 0.0) vol = 0.0;
    if (vol > 1.0) vol = 1.0;
    
    if (qAbs(volume_ - vol) > 0.001) {
        // 记录静音前音量，供取消静音时恢复
        if (volume_ > 0.0) {
            previousVolume_ = volume_;
            emit previousVolumeChanged();
        }
        volume_ = vol;
        // 同步到底层播放控制器
        controller_->setVolume(static_cast<float>(volume_));
        emit volumeChanged();
        qDebug() << "[MediaPlayer::setVolume] Volume updated:" << volume_;
    }
}

void MediaPlayer::setMuted(bool muted) {
    if (muted_ != muted) {
        muted_ = muted;
        // 同步到底层播放控制器
        controller_->setMuted(muted_);
        emit mutedChanged();
        qDebug() << "[MediaPlayer::setMuted] Mute state:" << muted_;
    }
}

void MediaPlayer::setPlaybackSpeed(double speed) {
    // 限制范围：0.25x - 2.0x
    if (speed < 0.25) speed = 0.25;
    if (speed > 2.0) speed = 2.0;
    
    if (qAbs(playbackSpeed_ - speed) > 0.001) {
        playbackSpeed_ = speed;
        // 同步到底层播放控制器
        controller_->setPlaybackSpeed(playbackSpeed_);
        emit playbackSpeedChanged();
        qInfo() << "[MediaPlayer::setPlaybackSpeed] Playback speed updated:" << playbackSpeed_ << "x";
    }
}

void MediaPlayer::openFile(const QUrl& fileUrl) {
    qInfo() << "[MediaPlayer::openFile] Request to open fileUrl:" << fileUrl << ", toString:" << fileUrl.toString();
    // 本次 openFile 已接管切换流程，清理自动下一首闸门
    autoNextInFlight_ = false;
    
    QString filePath,absolutePath;
    bool isNetworkStream = false;
    QFileInfo fileInfo;  // 仅本地文件场景使用，网络流不会访问该对象
    // 先判断是否为网络流 URL
    QString urlString = fileUrl.toString();
    if (urlString.startsWith("http://") || urlString.startsWith("https://") ||
        urlString.startsWith("rtmp://") || urlString.startsWith("rtsp://") ||
        urlString.startsWith("rtp://") || urlString.startsWith("udp://") ||
        urlString.startsWith("mms://") || urlString.startsWith("hls://") ||
        urlString.startsWith("tcp://") || urlString.startsWith("mmsh://") ||
        urlString.startsWith("dash://") ){
        isNetworkStream = true;
        absolutePath = urlString;
        filePath = urlString;
        qInfo() << "[MediaPlayer::openFile] Recognized as network stream URL:" << urlString;
    } else {
        // 处理本地文件
        filePath = fileUrl.toLocalFile();
        
        if (filePath.isEmpty()) {
            emit error("File path is empty");
            return;
        }
        // 本地路径必须存在
        fileInfo = QFileInfo(filePath);
        if (!fileInfo.exists()) {
            emit error("File does not exist: " + filePath);
            return;
        }
        
        absolutePath = fileInfo.absoluteFilePath();
    }
    
    // ==================== 防抖逻辑 ====================
    // 避免同一路径在极短时间内被重复 openFile
    // 常见重复来源：
    // 1. playlistModel.playIndex 修改后触发 playRequested
    // 2. QML 层又直接调用 player.openFile
    // 两条路径可能在同一帧内同时到达
    auto currentTime = std::chrono::steady_clock::now();
    auto timeSinceLastOpen = std::chrono::duration_cast<std::chrono::milliseconds>(
        currentTime - lastOpenTime_).count();
    
    if (timeSinceLastOpen < OPEN_FILE_DEBOUNCE_MS && pendingFilePath_ == absolutePath) {
        QString displayName = isNetworkStream ? absolutePath : fileInfo.fileName();
        qInfo() << "[MediaPlayer::openFile] Debounce active, ignore duplicate openFile call"
                << "interval:" << timeSinceLastOpen << "ms, file:" << displayName;
        return;
    }
    
    // 更新防抖状态
    lastOpenTime_ = currentTime;
    pendingFilePath_ = absolutePath;
    
    qInfo() << "[MediaPlayer::openFile] Start opening media:" << filePath;
    
    // ===== 进入新媒体前，重置结束判定标志 =====
    wasPlayingBeforeStopped_ = false;
    
    // ===== 先通知 UI 进入加载状态 =====
    isLoading_ = true;
    emit loadingStateChanged();
    
    
    // ===== 关闭旧媒体并清理状态 =====
    // 自动下一首场景采用一次性控制器轮转，避免 close/join 导致的崩溃路径
    if (!currentFile_.isEmpty() || !currentFilePath_.isEmpty()) {
        qInfo() << "[MediaPlayer::openFile] Existing media detected, rotate controller once and continue open";
        rotatePlaybackControllerForAutoNext();

        // 轮转后重置上一媒体状态，避免重复走“已有媒体”分支
        currentFile_.clear();
        currentFilePath_.clear();
        duration_ = 0;
        position_ = 0;
        isPlaying_ = false;
        isPaused_ = false;
        wasPlayingBeforeStopped_ = false;
        autoNextInFlight_ = false;
        deferredOpenInProgress_ = false;
        deferredOpenUrl_.clear();
    }
    
    // 如果没有正在关闭流程，确保遗留连接被清理
    if (closeCompletedConnection_) {
        QObject::disconnect(closeCompletedConnection_);
        closeCompletedConnection_ = {};
    }
    
    // ===== 打开新媒体 =====
    // openMedia() 返回是否成功
    bool success = controller_->openMedia(absolutePath,isNetworkStream);
    
    if (success) {
        // 打开成功后更新当前媒体信息
        currentFilePath_ = absolutePath;
        if (isNetworkStream) {
            // 网络流优先展示文件名，不存在则回退 host/完整 URL
            QString displayName = fileUrl.fileName();
            if (displayName.isEmpty()) {
                displayName = fileUrl.host();
                if (displayName.isEmpty()) {
                    displayName = absolutePath;
                }
            }
            currentFile_ = displayName;
        } else {
            currentFile_ = fileInfo.fileName();
        }
        qInfo() << "[MediaPlayer::openFile] currentFile_ updated:" << currentFile_;
        // duration will be updated via durationChanged signal
        position_ = 0.0;
        emit positionChanged();
        emit currentFileChanged();
        qInfo() << "[MediaPlayer::openFile] currentFileChanged() emitted";
        emit videoInfoChanged();
        
        isLoading_ = false;
        emit loadingStateChanged();
        
        // 若启用断点续播且当前刚打开文件，尝试恢复历史播放位置
        // 延迟执行，等待 duration 等基础信息先就绪
        // if(!SettingsManager::instance().rememberPlaybackPosition()){
        //     qInfo() << "当前未启用断点续播";
        // }
        if (SettingsManager::instance().rememberPlaybackPosition() &&
            !currentFilePath_.isEmpty() && duration_ > 0 && position_ < 0.1) {
            // 轻微延迟，避免与首帧初始化竞争
            QTimer::singleShot(10, this, &MediaPlayer::restorePlaybackPosition);
        }
        // 播放状态变化由底层信号回调更新
        // 这里不直接操作 isPlaying_/isPaused_，避免状态重复写入
        
        // 自动开始播放
        play();
    } else {
        qWarning() << "[MediaPlayer::openFile] Failed to open media:" << absolutePath;
        QString displayError = isNetworkStream ? absolutePath : fileInfo.fileName();
        emit error("Unable to open: " + displayError);
        
        isLoading_ = false;
        emit loadingStateChanged();
        
        // 打开失败时清理防抖候选路径
        pendingFilePath_.clear();
    }
}

void MediaPlayer::play() {
    if (currentFile_.isEmpty()) {
        qWarning() << "[MediaPlayer::play] No media available to play";
        return;
    }
    
    qInfo() << "[MediaPlayer::play] Start playback";
    
    // 委托 PlaybackController 执行播放
    controller_->play();
    
    // 状态更新由 stateChanged 信号回调处理
}

void MediaPlayer::pause() {
    if (!isPlaying_) {
        return;
    }
    
    qInfo() << "[MediaPlayer::pause] Pause playback";
    
    // 委托 PlaybackController 执行暂停
    controller_->pause();
    
    // 状态更新由 stateChanged 信号回调处理
}

void MediaPlayer::stop() {
    qInfo() << "[MediaPlayer::stop] Stop playback (user triggered)";
    
    // 用户主动停止，不应判定为自然播放完成
    wasPlayingBeforeStopped_ = false;
    
    // 委托 PlaybackController 执行停止
    controller_->stop();
    
    // 状态更新由 stateChanged 信号回调处理
}

void MediaPlayer::closeMedia() {
    // 媒体已进入显式关闭流程，重置自动下一首闸门
    autoNextInFlight_ = false;
    // ===== 关闭前持久化当前播放位置（如果启用）=====
    if (SettingsManager::instance().rememberPlaybackPosition() && !currentFilePath_.isEmpty()) {
        saveCurrentPosition();
    }
    
    // 停止定时保存器
    positionSaveTimer_->stop();
    
    // ===== 重置“播放完成判定”标志 =====
    wasPlayingBeforeStopped_ = false;
    
    // ===== 重置 UI 相关播放状态 =====
    isPlaying_ = false;
    isPaused_ = false;
    position_ = 0.0;
    
    // 清空当前媒体信息，避免 UI 误显示旧文件
    currentFile_.clear();
    currentFilePath_.clear();  // 清理绝对路径
    
    // 立即通知 UI 做状态回收
    emit playbackStateChanged();
    emit positionChanged();
    emit currentFileChanged();
    
    // 关闭窗口路径使用“安全轮转”，避免触发 close/join 崩溃点
    if (controller_) {
        try {
            controller_->setVideoOutput(nullptr);
            controller_->pause();
            QObject::disconnect(controller_.get(), nullptr, this, nullptr);

            retiredControllers_.push_back(controller_.release());
            controller_ = std::make_unique<PlaybackController>();
            bindControllerSignals();
            if (boundVideoOutput_) {
                controller_->setVideoOutput(boundVideoOutput_);
            }
            triggerRetiredControllerReclaim();
        } catch (const std::exception& e) {
            qCritical() << "[MediaPlayer::closeMedia] Exception while rotating controller on close:" << e.what();
        }
    }
    
    // 重置媒体时长
    duration_ = 0.0;
    
    // 再次同步 UI，确保时长清零生效
    emit currentFileChanged();
    emit durationChanged();
}

void MediaPlayer::togglePlayPause() {
    if (isPlaying_) {
        pause();
    } else {
        play();
    }
}

void MediaPlayer::toggleMute() {
    setMuted(!muted_);
}

void MediaPlayer::playPrevious() {
    qInfo() << "[MediaPlayer::playPrevious] Request play previous";
    // 由外部播放列表决定具体上一项
    emit requestPreviousFile();
}

void MediaPlayer::playNext() {
    qInfo() << "[MediaPlayer::playNext] Request play next";
    // 由外部播放列表决定具体下一项
    emit requestNextFile();
}

void MediaPlayer::captureScreenshot(const QString& outputPath) {
    qInfo() << "[MediaPlayer::captureScreenshot] Screenshot requested, output path:" << outputPath;
    
    // 获取当前视频帧
    AVFrame* frame = controller_->getCurrentFrame();
    if (!frame) {
        qWarning() << "[MediaPlayer::captureScreenshot] Failed to get current frame";
        emit screenshotFailed("Failed to get current frame");
        return;
    }
    
    // 处理输出路径
    QString finalPath = outputPath;
    if (finalPath.isEmpty()) {
        // 未指定路径时，按当前设置格式生成默认文件名
        QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
        const QString screenshotFormat = SettingsManager::instance().screenshotFormat().toLower();
        const QString fileExtension = (screenshotFormat == "jpeg") ? "jpg" : screenshotFormat;
        finalPath = QString("screenshot_%1.%2").arg(timestamp, fileExtension);
    }
    
    // 配置字符串格式转为截图模块枚举
    const QString screenshotFormat = SettingsManager::instance().screenshotFormat().toLower();
    const ScreenshotCapture::ImageFormat imageFormat =
        (screenshotFormat == "jpg" || screenshotFormat == "jpeg")
            ? ScreenshotCapture::ImageFormat::JPEG
            : (screenshotFormat == "bmp")
                ? ScreenshotCapture::ImageFormat::BMP
                : ScreenshotCapture::ImageFormat::PNG;

    screenshotCapture_->captureFrameAsync(
        frame,
        finalPath,
        imageFormat,
        95
    );
}

int MediaPlayer::videoWidth() const {
    return controller_ ? controller_->getVideoWidth() : 0;
}

int MediaPlayer::videoHeight() const {
    return controller_ ? controller_->getVideoHeight() : 0;
}

QString MediaPlayer::videoCodec() const {
    return controller_ ? controller_->getVideoCodec() : QString();
}

double MediaPlayer::frameRate() const {
    return controller_ ? controller_->getFrameRate() : 0.0;
}

qint64 MediaPlayer::videoBitrate() const {
    return controller_ ? controller_->getVideoBitrate() : 0;
}

QString MediaPlayer::audioCodec() const {
    return controller_ ? controller_->getAudioCodec() : QString();
}

qint64 MediaPlayer::audioBitrate() const {
    return controller_ ? controller_->getAudioBitrate() : 0;
}

int MediaPlayer::audioSampleRate() const {
    return controller_ ? controller_->getAudioSampleRate() : 0;
}

int MediaPlayer::audioChannels() const {
    return controller_ ? controller_->getAudioChannels() : 0;
}

void MediaPlayer::setVideoOutput(VideoOutput* videoOutput) {
    boundVideoOutput_ = videoOutput;
    if (videoOutput && controller_) {
        // VideoOutput 最好已挂载到可见窗口，避免渲染上下文异常
        if (!videoOutput->parent() && !videoOutput->window()) {
            qWarning() << "[MediaPlayer::setVideoOutput] VideoOutput may not be attached to a window, render issues may occur";
        }
        controller_->setVideoOutput(videoOutput);
    } else {
        qWarning() << "[MediaPlayer::setVideoOutput] Invalid arguments - videoOutput:" << reinterpret_cast<void*>(videoOutput)
                   << ", controller_:" << (controller_ ? "valid" : "invalid");
    }
}

void MediaPlayer::saveCurrentPosition() {
    if (!SettingsManager::instance().rememberPlaybackPosition()) {
        return;
    }
    
    if (currentFilePath_.isEmpty() || position_ < 0 || duration_ <= 0) {
        return;
    }
    
    // 仅在有效区间内保存：大于 0 且小于总时长
    // 避免把刚打开时的 0 秒和播放完成后的末尾位置写入
    // 减少“下次打开直接跳结尾”的异常体验
    if (position_ > 0 && position_ < duration_) {
        SettingsManager::instance().savePlaybackPosition(currentFilePath_, position_);
        qDebug() << "[MediaPlayer::saveCurrentPosition] Saved playback position - file:" << currentFile_
                 << ", position:" << position_ << "s / " << duration_ << "s";
    }
}

void MediaPlayer::restorePlaybackPosition() {
    if (!SettingsManager::instance().rememberPlaybackPosition()) {
        return;
    }
    
    if (currentFilePath_.isEmpty() || duration_ <= 0) {
        return;
    }
    
    double savedPosition = SettingsManager::instance().getPlaybackPosition(currentFilePath_);
    if (savedPosition > 0 && savedPosition < duration_) {
        qInfo() << "[MediaPlayer::restorePlaybackPosition] Historical playback position found - file:" << currentFile_
                << ", position:" << savedPosition << "s / " << duration_ << "s";
        // Seek 逻辑待补齐，当前仅记录恢复点
    } else {
        qInfo() << "[MediaPlayer::restorePlaybackPosition] No valid historical playback position found";
    }
}

} // namespace AdvancedPlayer

