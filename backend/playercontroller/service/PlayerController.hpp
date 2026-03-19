#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include "playerEngine/IjkMediaPlayer.hpp"

class QWidget;
namespace media {
class IjkMediaPlayer;
}
namespace backend::playercontroller::service
{

    class PlayerController final : public QObject
    {
        Q_OBJECT

    public:
        enum class PlaybackState
        {
            Idle,
            Opening,
            Prepared,
            Playing,
            Paused,
            Stopped,
            Error
        };
        Q_ENUM(PlaybackState)

        explicit PlayerController(QObject *parent = nullptr);
        ~PlayerController() override;

        PlaybackState playbackState() const;
        bool hasMediaLoaded() const;

    public slots:
        void attachVideoSurface(QWidget *surface);
        void openMedia(
            const QString &videoId,
            const QString &title,
            const QString &creator,
            const QString &duration);
        void requestPlay();
        void requestPause();
        void requestTogglePlayback();
        void requestStop();

    signals:
        void mediaChanged(
            const QString &videoId,
            const QString &title,
            const QString &creator,
            const QString &duration);
        void playbackStateChanged(PlaybackState state, const QString &message);
        void ijkPlayerCreated();
        void ijkPlayerOpenRequested(const QString &videoId, const QString &title);

    private:
        void updatePlaybackState(PlaybackState state, const QString &message);
        void ensureIjkPlayerCreated();
        void openMediaWithIjkPlayer();
        void handlePlayerEvent(media::PlayerEvent event, int arg1, void *arg2);
        int handleVideoFrame(const Frame *frame);
        void resetVideoConverter();
        media::IjkMediaPlayer *ijkPlayerInstance();
        media::IjkMediaPlayer *ijkPlayer_ = nullptr;
        QPointer<QWidget> videoSurface_;
        QString currentVideoId_;
        QString currentTitle_;
        QString currentCreator_;
        QString currentDuration_;
        PlaybackState playbackState_ = PlaybackState::Idle;
        bool ijkPlayerCreated_ = false;
        SwsContext *videoScaleContext_ = nullptr;
        int videoScaleWidth_ = 0;
        int videoScaleHeight_ = 0;
        AVPixelFormat videoScaleFormat_ = AV_PIX_FMT_NONE;
    };

} // namespace backend::playercontroller::service
