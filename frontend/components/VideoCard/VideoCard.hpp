#pragma once

#include <QPixmap>
#include <QPushButton>
#include <QString>

class QLabel;
class QResizeEvent;
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
    void setPosterPixmap(const QPixmap &pixmap);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void updatePreviewHeight();
    void updatePosterAppearance();

    QString videoId_;
    QString accentStart_;
    QString accentEnd_;
    QWidget *posterWidget_ = nullptr;
    QLabel *posterImageLabel_ = nullptr;
    QLabel *titleLabel_ = nullptr;
    QLabel *metaLabel_ = nullptr;
    QPixmap posterPixmap_;
};

}  // namespace frontend::components
