#pragma once

#include <QWidget>

namespace frontend::pages {

class UploadPage final : public QWidget
{
    Q_OBJECT

public:
    explicit UploadPage(QWidget *parent = nullptr);
};

}  // namespace frontend::pages
