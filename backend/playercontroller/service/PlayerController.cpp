#include "playercontroller/service/PlayerController.hpp"
#include "PlayerController.hpp"

#include <QByteArray>

namespace backend::playercontroller::service
{

    PlayerController::PlayerController(QObject *parent)
        : QObject(parent)
    {
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
        int ret = ijkPlayer_->create();
        ijkPlayer_->setEventCallback([this](media::PlayerEvent event, int arg1, void *arg2) {
            // Handle player events here and update state accordingly.
            // For now, we'll just log the events.
            qDebug("Received player event: %d", static_cast<int>(event));
        });
        ijkPlayer_->setVideoFrameCallback([](const Frame *frame) -> int {
            // Handle video frame callback here if needed.
            //如果需要处理视频帧回调，请在此处处理。
            return 0; // Return 0 for success.
        });
        ijkPlayerCreated_ = true;
        emit ijkPlayerCreated();
    }

    void PlayerController::openMediaWithIjkPlayer()
    {
        // This is the hand-off seam for the future ijkPlayer integration.
        //openMediaWithIjkPlayer
        if (ijkPlayer_) {
            const QByteArray encodedVideoId = currentVideoId_.toUtf8();
            ijkPlayer_->setDataSource(encodedVideoId.constData());
            ijkPlayer_->prepareAsync();
        }
        emit ijkPlayerOpenRequested(currentVideoId_, currentTitle_);
    }

    media::IjkMediaPlayer *PlayerController::ijkPlayerInstance()
    {
        // 创建实例
        if (!ijkPlayer_)
        {
            ijkPlayer_ = new media::IjkMediaPlayer();
        }
        return ijkPlayer_;
    }

} // namespace backend::playercontroller::service
