#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QtGlobal>
#include "playerEngine/IjkMediaPlayer.hpp"

class QWidget;
class QTimer;
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
        void requestSeek(int positionMs);
        void requestSetVolume(int volume);
        void requestToggleMute();
        void requestStop();
        void releasePlaybackResources();

    signals:
        void mediaChanged(
            const QString &videoId,
            const QString &title,
            const QString &creator,
            const QString &duration);
        void playbackStateChanged(PlaybackState state, const QString &message);
        void playbackProgressChanged(qint64 positionMs, qint64 durationMs);
        void playbackVolumeChanged(int volume, bool muted);
        void ijkPlayerCreated();
        void ijkPlayerOpenRequested(const QString &videoId, const QString &title);

    private:
        void updatePlaybackState(PlaybackState state, const QString &message);
        void updatePlaybackProgress(qint64 positionMs, qint64 durationMs);
        void updatePlaybackVolume(int volume, bool forceEmit = false);
        void applyPlaybackVolume();
        void ensureIjkPlayerCreated();
        void openMediaWithIjkPlayer();
        void syncPlaybackProgress();
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
        QTimer *playbackProgressTimer_ = nullptr;
        qint64 currentPositionMs_ = 0;
        qint64 totalDurationMs_ = 0;
        bool seekInFlight_ = false;
        qint64 pendingSeekPositionMs_ = -1;
        int playbackVolume_ = 50;
        int lastNonZeroVolume_ = 50;
        SwsContext *videoScaleContext_ = nullptr;
        int videoScaleWidth_ = 0;
        int videoScaleHeight_ = 0;
        AVPixelFormat videoScaleFormat_ = AV_PIX_FMT_NONE;
    };

} // namespace backend::playercontroller::service
