#pragma once

#include <QWidget>

namespace frontend::pages {

class StreamPage final : public QWidget
{
    Q_OBJECT

public:
    explicit StreamPage(QWidget *parent = nullptr);
};

}  // namespace frontend::pages
