#include "playercontroller/service/PlayerController.hpp"
#include "PlayerController.hpp"

#include <QByteArray>
#include <QDebug>
#include <QMetaObject>
#include <QtWidgets/QWidget>

#include <cstring>

namespace backend::playercontroller::service
{

    namespace
    {

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

    } // namespace

    PlayerController::PlayerController(QObject *parent)
        : QObject(parent)
    {
    }

    PlayerController::~PlayerController()
    {
        resetVideoConverter();
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

        emit mediaChanged(currentVideoId_, currentTitle_, currentCreator_, currentDuration_);
        updatePlaybackState(PlaybackState::Opening, QString("OpenMedia received for %1").arg(currentTitle_));

        if (videoSurface_)
        {
            QMetaObject::invokeMethod(videoSurface_.data(), "clearFrame", Qt::QueuedConnection);
        }

        ensureIjkPlayerCreated();
        openMediaWithIjkPlayer();

        updatePlaybackState(
            PlaybackState::Prepared,
            QString("PlayerController is ready to hand this media to ijkPlayer."));
    }

    void PlayerController::requestPlay()
    {
        if (!hasMediaLoaded())
        {
            updatePlaybackState(PlaybackState::Error, QString("No media has been selected yet."));
            return;
        }

        updatePlaybackState(PlaybackState::Playing, QString("Play requested."));
    }

    void PlayerController::requestPause()
    {
        if (playbackState_ != PlaybackState::Playing)
        {
            return;
        }

        updatePlaybackState(PlaybackState::Paused, QString("Pause requested."));
    }

    void PlayerController::requestTogglePlayback()
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

        requestPlay();
    }

    void PlayerController::requestStop()
    {
        if (!hasMediaLoaded())
        {
            return;
        }

        updatePlaybackState(PlaybackState::Stopped, QString("Stop requested."));
    }

    void PlayerController::updatePlaybackState(PlaybackState state, const QString &message)
    {
        playbackState_ = state;
        emit playbackStateChanged(playbackState_, message);
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

        ijkPlayerCreated_ = true;
        emit ijkPlayerCreated();
    }

    void PlayerController::openMediaWithIjkPlayer()
    {
        if (ijkPlayer_)
        {
            const QByteArray encodedVideoId = currentVideoId_.toUtf8();
            ijkPlayer_->setDataSource(encodedVideoId.constData());
            ijkPlayer_->prepareAsync();
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
        Q_UNUSED(arg1);
        Q_UNUSED(arg2);
        qDebug("Received player event: %d", static_cast<int>(event));
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
                                  Q_ARG(QByteArray, planeV));
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
