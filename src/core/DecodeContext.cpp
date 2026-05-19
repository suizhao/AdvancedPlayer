/**
 * @file DecodeContext.cpp
 * @brief 解码上下文实现
 */

#include "DecodeContext.h"
#include <QDebug>

#ifdef HAS_FFMPEG
extern "C" {
#include <libavformat/avformat.h>
}
#endif

namespace AdvancedPlayer {

void VideoDecodeContext::extractStreamInfo(AVFormatContext* formatCtx, int videoStreamIdx) {
#ifdef HAS_FFMPEG
    if (!formatCtx) {
        qWarning() << "[VideoDecodeContext::extractStreamInfo] formatCtx is null";
        return;
    }
    
    streamIndex = videoStreamIdx;
    nbStreams = formatCtx->nb_streams;
    
    if (streamIndex >= 0 && static_cast<unsigned int>(streamIndex) < nbStreams) {
        AVStream* stream = formatCtx->streams[streamIndex];
        if (stream && stream->codecpar) {
            timeBase = stream->time_base;
        }
    }
#else
    Q_UNUSED(formatCtx);
    Q_UNUSED(videoStreamIdx);
#endif
}

void AudioDecodeContext::extractStreamInfo(AVFormatContext* formatCtx, int audioStreamIdx) {
#ifdef HAS_FFMPEG
    if (!formatCtx) {
        qWarning() << "[AudioDecodeContext::extractStreamInfo] formatCtx is null";
        return;
    }
    
    streamIndex = audioStreamIdx;
    nbStreams = formatCtx->nb_streams;
    
    if (streamIndex >= 0 && static_cast<unsigned int>(streamIndex) < nbStreams) {
        AVStream* stream = formatCtx->streams[streamIndex];
        if (stream) {
            timeBase = stream->time_base;
            // qInfo() << "[AudioDecodeContext::extractStreamInfo] 音频流信息已提取:"
            //         << "index=" << streamIndex
            //         << ", time_base=" << timeBase.num << "/" << timeBase.den;
        }
    }
#else
    Q_UNUSED(formatCtx);
    Q_UNUSED(audioStreamIdx);
#endif
}

} // namespace AdvancedPlayer
