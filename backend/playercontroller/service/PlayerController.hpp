#pragma once

#include <QObject>
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
        ~PlayerController() override = default;

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
        media::IjkMediaPlayer *ijkPlayerInstance();
        media::IjkMediaPlayer *ijkPlayer_ = nullptr;
        QWidget *videoSurface_ = nullptr;
        QString currentVideoId_;
        QString currentTitle_;
        QString currentCreator_;
        QString currentDuration_;
        PlaybackState playbackState_ = PlaybackState::Idle;
        bool ijkPlayerCreated_ = false;
    };

} // namespace backend::playercontroller::service
