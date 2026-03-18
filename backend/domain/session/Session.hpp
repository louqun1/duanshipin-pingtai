#pragma once

#include <QDateTime>
#include <QString>

#include <optional>

namespace backend::domain::session {

struct Session
{
    QString id;
    QString userId;
    QString tokenHash;
    QDateTime createdAt;
    QDateTime expiresAt;
    QDateTime lastSeenAt;
    std::optional<QDateTime> revokedAt;

    [[nodiscard]] bool isRevoked() const
    {
        return revokedAt.has_value();
    }
};

}  // namespace backend::domain::session
