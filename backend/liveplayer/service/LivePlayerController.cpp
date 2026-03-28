#include "liveplayer/service/LivePlayerController.hpp"

namespace backend::liveplayer::service
{

    namespace
    {

        LivePlayerController::PlaybackState mapSessionState(
            backend::liveplayer::session::LivePlayerSession::SessionState state)
        {
            using SessionState = backend::liveplayer::session::LivePlayerSession::SessionState;
            using PlaybackState = LivePlayerController::PlaybackState;

            switch (state)
            {
            case SessionState::Idle:
                return PlaybackState::Idle;
            case SessionState::Connecting:
                return PlaybackState::Connecting;
            case SessionState::Reading:
                return PlaybackState::Reading;
            case SessionState::Playing:
                return PlaybackState::Playing;
            case SessionState::Stopped:
                return PlaybackState::Stopped;
            case SessionState::Error:
                return PlaybackState::Error;
            }

            return PlaybackState::Error;
        }

    } // namespace

    LivePlayerController::LivePlayerController(QObject *parent)
        : QObject(parent), session_(new backend::liveplayer::session::LivePlayerSession(this))
    {
        connect(session_, &backend::liveplayer::session::LivePlayerSession::stateChanged,
                this, &LivePlayerController::handleSessionStateChanged);
        connect(session_, &backend::liveplayer::session::LivePlayerSession::logMessage,
                this, &LivePlayerController::sessionLogAppended);
        connect(session_, &backend::liveplayer::session::LivePlayerSession::statsChanged,
                this, &LivePlayerController::streamStatsChanged);
    }

    LivePlayerController::PlaybackState LivePlayerController::playbackState() const
    {
        return playbackState_;
    }

    QString LivePlayerController::currentUrl() const
    {
        return currentUrl_;
    }

    void LivePlayerController::attachVideoSurface(QWidget *surface)
    {
        session_->attachVideoSurface(surface);
    }

    void LivePlayerController::openStream(const QString &url)
    {
        currentUrl_ = url.trimmed();
        emit streamUrlChanged(currentUrl_);
        session_->open(currentUrl_);
    }

    void LivePlayerController::stopStream()
    {
        session_->stop();
    }

    void LivePlayerController::handleSessionStateChanged(
        backend::liveplayer::session::LivePlayerSession::SessionState state,
        const QString &message)
    {
        setPlaybackState(mapSessionState(state), message);
    }

    void LivePlayerController::setPlaybackState(PlaybackState state, const QString &message)
    {
        playbackState_ = state;
        emit playbackStateChanged(playbackState_, message);
    }

} // namespace backend::liveplayer::service
