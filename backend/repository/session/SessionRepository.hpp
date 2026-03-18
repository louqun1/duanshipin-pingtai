#pragma once

#include "domain/session/Session.hpp"

#include <optional>

namespace backend::repository::session {

class SessionRepository
{
public:
    virtual ~SessionRepository() = default;

    virtual bool create(const backend::domain::session::Session &session) = 0;
    virtual std::optional<backend::domain::session::Session> findActiveSession() const = 0;
    virtual bool revokeAll() = 0;
};

}  // namespace backend::repository::session
