#pragma once

#include <QWidget>

namespace frontend::pages {

class ProfilePage final : public QWidget
{
    Q_OBJECT

public:
    explicit ProfilePage(QWidget *parent = nullptr);
};

}  // namespace frontend::pages
