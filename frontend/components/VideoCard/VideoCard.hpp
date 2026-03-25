#pragma once

#include <QPixmap>
#include <QPushButton>
#include <QString>
#include <QtGlobal>

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
    qint64 likeCount = 0;
    bool likedByMe = false;
};

class VideoCard final : public QPushButton
{
    Q_OBJECT

public:
    explicit VideoCard(const VideoCardData &data, QWidget *parent = nullptr);

    QString videoId() const;
    void setCardWidth(int width);
    void setPosterPixmap(const QPixmap &pixmap);
    void setLikeState(qint64 likeCount, bool likedByMe);
    void setLikeBusy(bool busy);

signals:
    void likeToggled(const QString &videoId, bool shouldLike);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void updatePreviewHeight();
    void updatePosterAppearance();
    void updateLikeButtonAppearance();

    QString videoId_;
    QString accentStart_;
    QString accentEnd_;
    QWidget *posterWidget_ = nullptr;
    QLabel *posterImageLabel_ = nullptr;
    QLabel *titleLabel_ = nullptr;
    QLabel *metaLabel_ = nullptr;
    QWidget *footerWidget_ = nullptr;
    QPushButton *likeButton_ = nullptr;
    QPixmap posterPixmap_;
    qint64 likeCount_ = 0;
    bool likedByMe_ = false;
    bool likeBusy_ = false;
};

}  // namespace frontend::components
