#pragma once

#include <QPushButton>
#include <QString>

class QLabel;
class QWidget;

namespace frontend::components {

struct VideoCardData {
    QString id;
    QString title;
    QString creator;
    QString duration;
    QString accentStart;
    QString accentEnd;
};

class VideoCard final : public QPushButton
{
    Q_OBJECT

public:
    explicit VideoCard(const VideoCardData &data, QWidget *parent = nullptr);

    QString videoId() const;
    void setCardWidth(int width);

private:
    void updatePreviewHeight();

    QString videoId_;
    QWidget *posterWidget_ = nullptr;
    QLabel *titleLabel_ = nullptr;
    QLabel *metaLabel_ = nullptr;
};

}  // namespace frontend::components
