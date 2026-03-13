#pragma once

#include <QString>

namespace backend::domain::user {

struct User {
    QString id;
    QString username;
    QString email;
    QString avatarPath;
};

}  // namespace backend::domain::user
