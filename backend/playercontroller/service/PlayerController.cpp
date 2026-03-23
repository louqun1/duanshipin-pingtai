#include "playercontroller/service/PlayerController.hpp"
#include "PlayerController.hpp"

#include <QByteArray>
#include <QDebug>
#include <QMetaObject>
#include <QTimer>
#include <QtWidgets/QWidget>

#include <cstring>

namespace backend::playercontroller::service
{

    namespace
    {

        enum class VideoColorMatrix
        {
            Bt601 = 0,
            Bt709 = 1
        };

        QByteArray copyPlane(
            const uint8_t *sourceData,
            int sourceLineSize,
            int copyWidth,
            int copyHeight)
        {
            if (!sourceData || sourceLineSize == 0 || copyWidth <= 0 || copyHeight <= 0)
            {
                return {};
            }

            if ((sourceLineSize > 0 && sourceLineSize < copyWidth) ||
                (sourceLineSize < 0 && -sourceLineSize < copyWidth))
            {
                return {};
            }

            QByteArray plane(copyWidth * copyHeight, Qt::Uninitialized);
            auto *destinationData = reinterpret_cast<uint8_t *>(plane.data());

            if (sourceLineSize > 0)
            {
                for (int row = 0; row < copyHeight; ++row)
                {
                    std::memcpy(destinationData + (row * copyWidth),
                                sourceData + (row * sourceLineSize),
                                copyWidth);
                }
                return plane;
            }

            const int absoluteLineSize = -sourceLineSize;
            const uint8_t *rowData = sourceData + ((copyHeight - 1) * absoluteLineSize);
            for (int row = 0; row < copyHeight; ++row)
            {
                std::memcpy(destinationData + (row * copyWidth),
                            rowData - (row * absoluteLineSize),
                            copyWidth);
            }

            return plane;
        }

        bool isFullRangeSource(AVPixelFormat sourceFormat, AVColorRange colorRange)
        {
            if (colorRange == AVCOL_RANGE_JPEG)
            {
                return true;
            }

            switch (sourceFormat)
            {
            case AV_PIX_FMT_YUVJ420P:
            case AV_PIX_FMT_YUVJ422P:
            case AV_PIX_FMT_YUVJ444P:
            case AV_PIX_FMT_YUVJ440P:
            case AV_PIX_FMT_YUVJ411P:
                return true;
            default:
                return false;
            }
        }

        VideoColorMatrix resolveColorMatrix(const AVFrame *frame)
        {
            if (!frame)
            {
                return VideoColorMatrix::Bt601;
            }

            switch (frame->colorspace)
            {
            case AVCOL_SPC_BT709:
                return VideoColorMatrix::Bt709;
            case AVCOL_SPC_FCC:
            case AVCOL_SPC_BT470BG:
            case AVCOL_SPC_SMPTE170M:
            case AVCOL_SPC_SMPTE240M:
                return VideoColorMatrix::Bt601;
            default:
                break;
            }

            if (frame->width >= 1280 || frame->height > 576)
            {
                return VideoColorMatrix::Bt709;
            }

            return VideoColorMatrix::Bt601;
        }

        int clampVolume(int volume)
        {
            if (volume < 0)
            {
                return 0;
            }

            if (volume > 100)
            {
                return 100;
            }

            return volume;
        }

    } // namespace

    PlayerController::PlayerController(QObject *parent)
        : QObject(parent)
    {
        playbackProgressTimer_ = new QTimer(this);  //创建定时器，250ms触发一次，用于同步播放进度
        playbackProgressTimer_->setInterval(250);
        connect(playbackProgressTimer_, &QTimer::timeout,
                this, &PlayerController::syncPlaybackProgress);
        playbackProgressTimer_->start();    //启动定时器
    }

    

    PlayerController::~PlayerController()
    {
        releasePlaybackResources();
    }

    PlayerController::PlaybackState PlayerController::playbackState() const
    {
        return playbackState_;
    }

    bool PlayerController::hasMediaLoaded() const
    {
        return !currentVideoId_.isEmpty();
    }

    void PlayerController::attachVideoSurface(QWidget *surface)
    {
        videoSurface_ = surface;
    }

    void PlayerController::openMedia(
        const QString &videoId,
        const QString &title,
        const QString &creator,
        const QString &duration)
    {
        currentVideoId_ = videoId;
        currentTitle_ = title;
        currentCreator_ = creator;
        currentDuration_ = duration;
        seekInFlight_ = false;  //重置 seek 状态，取消任何未完成的 seek 操作，防止多次seek请求导致状态混乱
        pendingSeekPositionMs_ = -1;    //重置待处理的 seek 位置(实际上是用来保存最后一次请求的 seek 位置)
        updatePlaybackProgress(0, 0);

        emit mediaChanged(currentVideoId_, currentTitle_, currentCreator_, currentDuration_);
        updatePlaybackState(PlaybackState::Opening, QString("OpenMedia received for %1").arg(currentTitle_));

        if (videoSurface_)
        {
            QMetaObject::invokeMethod(videoSurface_.data(), "clearFrame", Qt::QueuedConnection);
        }

        ensureIjkPlayerCreated();
        if (!ijkPlayerCreated_ || !ijkPlayer_)
        {
            return;
        }

        openMediaWithIjkPlayer();
    }

    void PlayerController::requestPlay()
    {
        if (!hasMediaLoaded())
        {
            updatePlaybackState(PlaybackState::Error, QString("No media has been selected yet."));
            return;
        }

        if (!ijkPlayer_)
        {
            updatePlaybackState(PlaybackState::Error, QString("ijkPlayer is not available."));
            return;
        }

        const int ret = ijkPlayer_->start();
        if (ret != 0)
        {
            updatePlaybackState(playbackState_, QString("Play request was rejected by ijkPlayer."));
        }
    }

    void PlayerController::requestPause()
    {
        if (playbackState_ != PlaybackState::Playing)
        {
            return;
        }

        if (!ijkPlayer_)
        {
            updatePlaybackState(PlaybackState::Error, QString("ijkPlayer is not available."));
            return;
        }

        const int ret = ijkPlayer_->pause();
        if (ret != 0)
        {
            updatePlaybackState(playbackState_, QString("Pause request was rejected by ijkPlayer."));
        }
    }

    void PlayerController::requestTogglePlayback()//请求切换播放状态
    {
        if (!hasMediaLoaded())
        {
            updatePlaybackState(PlaybackState::Error, QString("No media has been selected yet."));
            return;
        }

        if (playbackState_ == PlaybackState::Playing)
        {
            requestPause();
            return;
        }
        if (playbackState_ == PlaybackState::Paused ||
            playbackState_ == PlaybackState::Prepared ||
            playbackState_ == PlaybackState::Stopped)
        {
            requestPlay();
            return;
        }
    }

    void PlayerController::requestSeek(int positionMs)
    {
        if (!hasMediaLoaded())
        {
            return;
        }

        if (!ijkPlayer_)
        {
            updatePlaybackState(PlaybackState::Error, QString("ijkPlayer is not available."));
            return;
        }

        qint64 seekTargetMs = positionMs < 0 ? 0 : static_cast<qint64>(positionMs);
        if (totalDurationMs_ > 0 && seekTargetMs > totalDurationMs_)
        {
            seekTargetMs = totalDurationMs_;
        }

        const int ret = ijkPlayer_->seekTo(static_cast<long>(seekTargetMs));
        if (ret != 0)
        {
            updatePlaybackState(playbackState_, QString("Seek request was rejected by ijkPlayer."));
            return;
        }

        seekInFlight_ = true;
        pendingSeekPositionMs_ = seekTargetMs;
        updatePlaybackProgress(seekTargetMs, totalDurationMs_);
    }

    void PlayerController::requestSetVolume(int volume)
    {
        updatePlaybackVolume(volume);
        applyPlaybackVolume();
    }

    void PlayerController::requestToggleMute()
    {
        if (playbackVolume_ > 0)
        {
            lastNonZeroVolume_ = playbackVolume_;
            updatePlaybackVolume(0);
        }
        else
        {
            updatePlaybackVolume(lastNonZeroVolume_ > 0 ? lastNonZeroVolume_ : 50);
        }

        applyPlaybackVolume();
    }

    void PlayerController::requestStop()
    {
        if (!hasMediaLoaded())
        {
            return;
        }

        if (!ijkPlayer_)
        {
            updatePlaybackState(PlaybackState::Error, QString("ijkPlayer is not available."));
            return;
        }

        const int ret = ijkPlayer_->stop();
        if (ret != 0)
        {
            updatePlaybackState(playbackState_, QString("Stop request was rejected by ijkPlayer."));
        }
    }

    void PlayerController::releasePlaybackResources()
    {
        if (videoSurface_)
        {
            QMetaObject::invokeMethod(videoSurface_.data(), "clearFrame", Qt::QueuedConnection);
        }

        resetVideoConverter();

        if (ijkPlayer_)
        {
            ijkPlayer_->setEventCallback({});
            ijkPlayer_->setVideoFrameCallback({});
            ijkPlayer_->stop();
            delete ijkPlayer_;
            ijkPlayer_ = nullptr;
        }

        ijkPlayerCreated_ = false;
        currentVideoId_.clear();
        currentTitle_.clear();
        currentCreator_.clear();
        currentDuration_.clear();
        seekInFlight_ = false;
        pendingSeekPositionMs_ = -1;
        updatePlaybackProgress(0, 0);

        if (playbackState_ != PlaybackState::Idle)
        {
            updatePlaybackState(PlaybackState::Idle, QString("Playback resources released."));
        }
    }

    void PlayerController::updatePlaybackState(PlaybackState state, const QString &message)
    {
        playbackState_ = state;
        emit playbackStateChanged(playbackState_, message);
    }

    void PlayerController::updatePlaybackProgress(qint64 positionMs, qint64 durationMs)
    {
        if (positionMs < 0)
            positionMs = 0;

        if (durationMs < 0)
            durationMs = 0;

        if (durationMs > 0 && positionMs > durationMs)
            positionMs = durationMs;

        if (currentPositionMs_ == positionMs && totalDurationMs_ == durationMs)//如果没有变化，就不发出信号了
            return;

        currentPositionMs_ = positionMs;
        totalDurationMs_ = durationMs;
        emit playbackProgressChanged(currentPositionMs_, totalDurationMs_);
    }

    void PlayerController::updatePlaybackVolume(int volume, bool forceEmit)
    {
        const int clampedVolume = clampVolume(volume);
        if (clampedVolume > 0)
        {
            lastNonZeroVolume_ = clampedVolume;
        }

        if (!forceEmit && playbackVolume_ == clampedVolume)
        {
            return;
        }

        playbackVolume_ = clampedVolume;
        emit playbackVolumeChanged(playbackVolume_, playbackVolume_ == 0);
    }

    void PlayerController::applyPlaybackVolume()
    {
        if (ijkPlayer_)
        {
            ijkPlayer_->setPlaybackVolume(playbackVolume_);
        }
    }

    void PlayerController::ensureIjkPlayerCreated()
    {
        if (ijkPlayerCreated_)
        {
            return;
        }

        ijkPlayer_ = ijkPlayerInstance();
        ijkPlayer_->setEventCallback([this](media::PlayerEvent event, int arg1, void *arg2) {
            handlePlayerEvent(event, arg1, arg2);
        });
        ijkPlayer_->setVideoFrameCallback([this](const Frame *frame) -> int {
            return handleVideoFrame(frame);
        });

        const int ret = ijkPlayer_->create();
        if (ret != 0)
        {
            updatePlaybackState(PlaybackState::Error, QString("Failed to create ijkPlayer instance."));
            return;
        }

        applyPlaybackVolume();
        ijkPlayerCreated_ = true;
        emit ijkPlayerCreated();
    }

    void PlayerController::openMediaWithIjkPlayer()
    {
        if (ijkPlayerCreated_ && ijkPlayer_)
        {
            applyPlaybackVolume();
            const QByteArray encodedVideoId = currentVideoId_.toUtf8();
            if (ijkPlayer_->setDataSource(encodedVideoId.constData()) != 0)
            {
                updatePlaybackState(PlaybackState::Error,
                                    QString("Failed to set media source on ijkPlayer."));
                return;
            }

            if (ijkPlayer_->prepareAsync() != 0)
            {
                updatePlaybackState(PlaybackState::Error,
                                    QString("Failed to prepare media with ijkPlayer."));
                return;
            }
        }

        emit ijkPlayerOpenRequested(currentVideoId_, currentTitle_);
    }

    media::IjkMediaPlayer *PlayerController::ijkPlayerInstance()
    {
        if (!ijkPlayer_)
        {
            ijkPlayer_ = new media::IjkMediaPlayer();
        }
        return ijkPlayer_;
    }

    void PlayerController::handlePlayerEvent(media::PlayerEvent event, int arg1, void *arg2)
    {
        Q_UNUSED(arg2);
        qDebug("Received player event: %d", static_cast<int>(event));

        auto queuePlaybackStateUpdate = [this](PlaybackState state, const QString &message) {
            QMetaObject::invokeMethod(
                this,
                [this, state, message]() {
                    updatePlaybackState(state, message);
                },
                Qt::QueuedConnection);
        };
        auto queueProgressSync = [this]() {
            QMetaObject::invokeMethod(
                this,
                [this]() {
                    syncPlaybackProgress();
                },
                Qt::QueuedConnection);
        };
        auto queueSeekStateReset = [this]() {
            QMetaObject::invokeMethod(
                this,
                [this]() {
                    seekInFlight_ = false;
                    pendingSeekPositionMs_ = -1;
                },
                Qt::QueuedConnection);
        };

        switch (event)
        {
        case media::PlayerEvent::OpenInputStarted:
            queuePlaybackStateUpdate(PlaybackState::Opening,
                                     QString("ijkPlayer started opening %1.").arg(currentTitle_));
            break;
        case media::PlayerEvent::Prepared:
            queuePlaybackStateUpdate(PlaybackState::Prepared,
                                     QString("Media prepared. Ready to play."));
            queueProgressSync();
            break;
        case media::PlayerEvent::Playing:
            queuePlaybackStateUpdate(PlaybackState::Playing,
                                     QString("Playback is running."));
            queueProgressSync();
            break;
        case media::PlayerEvent::Paused:
            queuePlaybackStateUpdate(PlaybackState::Paused,
                                     QString("Playback paused."));
            queueProgressSync();
            break;
        case media::PlayerEvent::Stopped:
            queuePlaybackStateUpdate(PlaybackState::Stopped,
                                     QString("Playback stopped."));
            queueProgressSync();
            break;
        case media::PlayerEvent::SeekCompleted:
            queueSeekStateReset();
            queueProgressSync();
            break;
        case media::PlayerEvent::PlaybackFinished:
            queuePlaybackStateUpdate(PlaybackState::Stopped,
                                     QString("Playback finished."));
            queueSeekStateReset();
            queueProgressSync();
            break;
        case media::PlayerEvent::ErrorOccurred:
            queuePlaybackStateUpdate(PlaybackState::Error,
                                     QString("ijkPlayer reported an error (%1).").arg(arg1));
            queueSeekStateReset();
            break;
        default:
            break;
        }
    }

    void PlayerController::syncPlaybackProgress()
    {
        if (!ijkPlayer_ || !ijkPlayerCreated_ || !hasMediaLoaded())
        {
            updatePlaybackProgress(0, 0);
            return;
        }

        if (seekInFlight_)
        {
            updatePlaybackProgress(pendingSeekPositionMs_ >= 0 ? pendingSeekPositionMs_ : currentPositionMs_,
                                   totalDurationMs_);
            return;
        }

        qint64 durationMs = static_cast<qint64>(ijkPlayer_->getDuration());
        qint64 positionMs = static_cast<qint64>(ijkPlayer_->getCurrentPosition());
        if (durationMs < 0)
        {
            durationMs = 0;
        }
        if (positionMs < 0)
        {
            positionMs = 0;
        }

        updatePlaybackProgress(positionMs, durationMs);
    }

    int PlayerController::handleVideoFrame(const Frame *frame)
    {
        if (!frame || !frame->frame || !videoSurface_)
        {
            return 0;
        }

        AVFrame *rawFrame = frame->frame;
        if (rawFrame->width <= 0 || rawFrame->height <= 0 || rawFrame->format == AV_PIX_FMT_NONE)
        {
            return -1;
        }

        const AVPixelFormat sourceFormat = static_cast<AVPixelFormat>(rawFrame->format);
        const int frameWidth = rawFrame->width;
        const int frameHeight = rawFrame->height;
        const int chromaWidth = (frameWidth + 1) / 2;
        const int chromaHeight = (frameHeight + 1) / 2;
        const VideoColorMatrix colorMatrix = resolveColorMatrix(rawFrame);
        const bool fullRange = isFullRangeSource(sourceFormat, rawFrame->color_range);

        QByteArray planeY;
        QByteArray planeU;
        QByteArray planeV;

        if (sourceFormat == AV_PIX_FMT_YUV420P &&
            rawFrame->data[0] &&
            rawFrame->data[1] &&
            rawFrame->data[2])
        {
            planeY = copyPlane(rawFrame->data[0], rawFrame->linesize[0], frameWidth, frameHeight);
            planeU = copyPlane(rawFrame->data[1], rawFrame->linesize[1], chromaWidth, chromaHeight);
            planeV = copyPlane(rawFrame->data[2], rawFrame->linesize[2], chromaWidth, chromaHeight);
        }
        else
        {
            videoScaleContext_ = sws_getCachedContext(videoScaleContext_,
                                                      frameWidth,
                                                      frameHeight,
                                                      sourceFormat,
                                                      frameWidth,
                                                      frameHeight,
                                                      AV_PIX_FMT_YUV420P,
                                                      SWS_BILINEAR,
                                                      nullptr,
                                                      nullptr,
                                                      nullptr);
            if (!videoScaleContext_)
            {
                return -1;
            }

            videoScaleWidth_ = frameWidth;
            videoScaleHeight_ = frameHeight;
            videoScaleFormat_ = sourceFormat;

            planeY = QByteArray(frameWidth * frameHeight, Qt::Uninitialized);
            planeU = QByteArray(chromaWidth * chromaHeight, Qt::Uninitialized);
            planeV = QByteArray(chromaWidth * chromaHeight, Qt::Uninitialized);
            if (planeY.isEmpty() || planeU.isEmpty() || planeV.isEmpty())
            {
                return -1;
            }

            uint8_t *destinationData[4] = {
                reinterpret_cast<uint8_t *>(planeY.data()),
                reinterpret_cast<uint8_t *>(planeU.data()),
                reinterpret_cast<uint8_t *>(planeV.data()),
                nullptr,
            };
            int destinationLinesize[4] = {
                frameWidth,
                chromaWidth,
                chromaWidth,
                0,
            };

            const int scaledHeight = sws_scale(videoScaleContext_,
                                               rawFrame->data,
                                               rawFrame->linesize,
                                               0,
                                               frameHeight,
                                               destinationData,
                                               destinationLinesize);
            if (scaledHeight <= 0)
            {
                return -1;
            }
        }

        if (planeY.isEmpty() || planeU.isEmpty() || planeV.isEmpty())
        {
            return -1;
        }

        QMetaObject::invokeMethod(videoSurface_.data(),
                                  "presentFrame",
                                  Qt::QueuedConnection,
                                  Q_ARG(int, frameWidth),
                                  Q_ARG(int, frameHeight),
                                  Q_ARG(QByteArray, planeY),
                                  Q_ARG(QByteArray, planeU),
                                  Q_ARG(QByteArray, planeV),
                                  Q_ARG(int, static_cast<int>(colorMatrix)),
                                  Q_ARG(bool, fullRange));
        return 0;
    }

    void PlayerController::resetVideoConverter()
    {
        if (videoScaleContext_)
        {
            sws_freeContext(videoScaleContext_);
            videoScaleContext_ = nullptr;
        }

        videoScaleWidth_ = 0;
        videoScaleHeight_ = 0;
        videoScaleFormat_ = AV_PIX_FMT_NONE;
    }

} // namespace backend::playercontroller::service
