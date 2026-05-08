#pragma once

#include <optional>

#include <QMap>
#include <QJsonObject>
#include <QVector>

#include "linkmic/LinkMicTypes.hpp"

namespace frontend::linkmic {

class RoomMemberDirectory {
public:
    void clear();
    void setLocalUserId(const QString &userId);

    void replaceFromMessage(const QJsonObject &message);
    void applyJoinFromMessage(const QJsonObject &message);
    void applyLeaveFromMessage(const QJsonObject &message);

    void clearLinkMicFlags();
    void setMemberLinkMicState(const QString &userId, bool inLinkMic);

    [[nodiscard]] int onlineCount() const;
    [[nodiscard]] const QVector<RoomMember> &members() const;
    [[nodiscard]] std::optional<RoomMember> memberById(const QString &userId) const;

private:
    void upsertMember(RoomMember member);
    void rebuildCache();

    QString localUserId_;
    QVector<QString> order_;
    QMap<QString, RoomMember> membersById_;
    QVector<RoomMember> cachedMembers_;
};

}  // namespace frontend::linkmic
