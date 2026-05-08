#include "linkmic/RoomMemberDirectory.hpp"

#include <QJsonArray>
#include <QJsonValue>

namespace frontend::linkmic {
namespace {

QString firstNonEmptyString(const QJsonObject &object, std::initializer_list<const char *> keys)
{
    for (const char *key : keys) {
        const QString value = object.value(QLatin1String(key)).toString().trimmed();
        if (!value.isEmpty()) {
            return value;
        }
    }
    return {};
}

bool firstBool(const QJsonObject &object, std::initializer_list<const char *> keys, bool fallback)
{
    for (const char *key : keys) {
        if (object.contains(QLatin1String(key))) {
            return object.value(QLatin1String(key)).toBool(fallback);
        }
    }
    return fallback;
}

QString extractMemberId(const QJsonObject &memberObject)
{
    return firstNonEmptyString(memberObject, {"userId", "peer_id", "user_id", "member_id", "id"});
}

std::optional<RoomMember> parseMember(const QJsonValue &value)
{
    if (value.isString()) {
        const QString userId = value.toString().trimmed();
        if (userId.isEmpty()) {
            return std::nullopt;
        }

        RoomMember member;
        member.userId = userId;
        member.displayName = userId;
        return member;
    }

    if (!value.isObject()) {
        return std::nullopt;
    }

    const QJsonObject object = value.toObject();
    const QString userId = extractMemberId(object);
    if (userId.isEmpty()) {
        return std::nullopt;
    }

    RoomMember member;
    member.userId = userId;
    member.displayName =
        firstNonEmptyString(object, {"display_name", "username", "nickname", "name", "peer_name", "user_name"});
    member.avatarUrl = firstNonEmptyString(object, {"avatar_url", "avatar", "avatarUrl"});
    member.role = firstNonEmptyString(object, {"role", "participant_role"});
    member.online = firstBool(object, {"online", "is_online", "isOnline"}, true);
    member.inLinkMic = firstBool(object, {"is_linkmic", "in_linkmic", "linkmic_active", "isLinkMicActive"}, false);
    member.raw = object;
    return member;
}

QJsonArray extractMemberArray(const QJsonObject &message)
{
    if (message.contains(QStringLiteral("members")) && message.value(QStringLiteral("members")).isArray()) {
        return message.value(QStringLiteral("members")).toArray();
    }
    if (message.contains(QStringLiteral("peers")) && message.value(QStringLiteral("peers")).isArray()) {
        return message.value(QStringLiteral("peers")).toArray();
    }
    if (message.contains(QStringLiteral("payload")) && message.value(QStringLiteral("payload")).isObject()) {
        const QJsonObject payload = message.value(QStringLiteral("payload")).toObject();
        if (payload.contains(QStringLiteral("members")) && payload.value(QStringLiteral("members")).isArray()) {
            return payload.value(QStringLiteral("members")).toArray();
        }
        if (payload.contains(QStringLiteral("peers")) && payload.value(QStringLiteral("peers")).isArray()) {
            return payload.value(QStringLiteral("peers")).toArray();
        }
    }
    return {};
}

std::optional<RoomMember> extractMemberFromMessage(const QJsonObject &message)
{
    if (message.contains(QStringLiteral("member"))) {
        return parseMember(message.value(QStringLiteral("member")));
    }

    if (message.contains(QStringLiteral("payload")) && message.value(QStringLiteral("payload")).isObject()) {
        const QJsonObject payload = message.value(QStringLiteral("payload")).toObject();
        if (payload.contains(QStringLiteral("member"))) {
            return parseMember(payload.value(QStringLiteral("member")));
        }
        if (payload.contains(QStringLiteral("user"))) {
            return parseMember(payload.value(QStringLiteral("user")));
        }
        if (payload.contains(QStringLiteral("peer"))) {
            return parseMember(payload.value(QStringLiteral("peer")));
        }
        const auto parsedPayload = parseMember(QJsonValue(payload));
        if (parsedPayload.has_value()) {
            return parsedPayload;
        }
    }

    const auto parsedTopLevel = parseMember(QJsonValue(message));
    if (parsedTopLevel.has_value()) {
        return parsedTopLevel;
    }

    return std::nullopt;
}

QString extractMemberIdFromMessage(const QJsonObject &message)
{
    if (const auto member = extractMemberFromMessage(message); member.has_value()) {
        return member->userId;
    }
    return firstNonEmptyString(message, {"userId", "peer_id", "user_id", "member_id", "from_user_id"});
}

}  // namespace

void RoomMemberDirectory::clear()
{
    order_.clear();
    membersById_.clear();
    cachedMembers_.clear();
}

void RoomMemberDirectory::setLocalUserId(const QString &userId)
{
    localUserId_ = userId.trimmed();
}

void RoomMemberDirectory::replaceFromMessage(const QJsonObject &message)
{
    const QJsonArray members = extractMemberArray(message);

    clear();
    for (const QJsonValue &value : members) {
        if (const auto member = parseMember(value); member.has_value()) {
            upsertMember(*member);
        }
    }
    rebuildCache();
}

void RoomMemberDirectory::applyJoinFromMessage(const QJsonObject &message)
{
    if (const auto member = extractMemberFromMessage(message); member.has_value()) {
        upsertMember(*member);
        rebuildCache();
    }
}

void RoomMemberDirectory::applyLeaveFromMessage(const QJsonObject &message)
{
    const QString userId = extractMemberIdFromMessage(message);
    if (userId.isEmpty()) {
        return;
    }

    membersById_.remove(userId);
    order_.removeAll(userId);
    rebuildCache();
}

void RoomMemberDirectory::clearLinkMicFlags()
{
    for (auto it = membersById_.begin(); it != membersById_.end(); ++it) {
        it->inLinkMic = false;
    }
    rebuildCache();
}

void RoomMemberDirectory::setMemberLinkMicState(const QString &userId, bool inLinkMic)
{
    const QString normalizedUserId = userId.trimmed();
    if (normalizedUserId.isEmpty()) {
        return;
    }

    if (!membersById_.contains(normalizedUserId)) {
        RoomMember member;
        member.userId = normalizedUserId;
        member.displayName = normalizedUserId;
        upsertMember(std::move(member));
    }

    auto it = membersById_.find(normalizedUserId);
    it->inLinkMic = inLinkMic;
    rebuildCache();
}

int RoomMemberDirectory::onlineCount() const
{
    return cachedMembers_.size();
}

const QVector<RoomMember> &RoomMemberDirectory::members() const
{
    return cachedMembers_;
}

std::optional<RoomMember> RoomMemberDirectory::memberById(const QString &userId) const
{
    const auto it = membersById_.find(userId);
    if (it == membersById_.end()) {
        return std::nullopt;
    }
    return *it;
}

void RoomMemberDirectory::upsertMember(RoomMember member)
{
    member.userId = member.userId.trimmed();
    if (member.userId.isEmpty()) {
        return;
    }

    if (member.displayName.trimmed().isEmpty()) {
        member.displayName = member.userId;
    }

    if (!order_.contains(member.userId)) {
        order_.push_back(member.userId);
    }

    membersById_.insert(member.userId, std::move(member));
}

void RoomMemberDirectory::rebuildCache()
{
    cachedMembers_.clear();
    cachedMembers_.reserve(order_.size());

    for (const QString &userId : order_) {
        const auto it = membersById_.find(userId);
        if (it == membersById_.end()) {
            continue;
        }
        cachedMembers_.push_back(*it);
    }
}

}  // namespace frontend::linkmic
