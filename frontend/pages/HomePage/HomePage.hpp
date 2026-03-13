#pragma once

#include <QWidget>

namespace frontend::pages {

class HomePage final : public QWidget
{
    Q_OBJECT

public:
    explicit HomePage(QWidget *parent = nullptr);
};

}  // namespace frontend::pages
