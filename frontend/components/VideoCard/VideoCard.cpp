#include "components/VideoCard/VideoCard.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QStackedLayout>
#include <QVBoxLayout>
#include <QWidget>

namespace frontend::components {

namespace {

void makeTransparentForMouse(QWidget *widget)
{
    widget->setAttribute(Qt::WA_TransparentForMouseEvents);
}

}  // namespace

VideoCard::VideoCard(const VideoCardData &data, QWidget *parent)
    : QPushButton(parent)
    , videoId_(data.id)
    , accentStart_(data.accentStart)
    , accentEnd_(data.accentEnd)
    , likeCount_(qMax<qint64>(0, data.likeCount))
    , likedByMe_(data.likedByMe)
{
    setCursor(Qt::PointingHandCursor);
    setFlat(true);
    setCheckable(false);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setObjectName("videoCard");
    setStyleSheet(
        "QPushButton#videoCard {"
        "  border: 1px solid #dbe4f0;"
        "  border-radius: 22px;"
        "  background: #ffffff;"
        "  padding: 0;"
        "  text-align: left;"
        "}"
        "QPushButton#videoCard:hover {"
        "  border-color: #93c5fd;"
        "  background: #f8fbff;"
        "}"
        "QPushButton#videoCard:pressed {"
        "  background: #eff6ff;"
        "}"
        "QLabel#videoCardTitle {"
        "  font-size: 15px;"
        "  font-weight: 700;"
        "  color: #0f172a;"
        "}"
        "QLabel#videoCardMeta {"
        "  font-size: 12px;"
        "  color: #64748b;"
        "}");

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(12);

    posterWidget_ = new QWidget(this);
    makeTransparentForMouse(posterWidget_);

    auto *posterStack = new QStackedLayout(posterWidget_);
    posterStack->setStackingMode(QStackedLayout::StackAll);
    posterStack->setContentsMargins(0, 0, 0, 0);

    posterImageLabel_ = new QLabel(posterWidget_);
    posterImageLabel_->setAlignment(Qt::AlignCenter);
    posterImageLabel_->setStyleSheet("background: transparent;");
    makeTransparentForMouse(posterImageLabel_);
    posterStack->addWidget(posterImageLabel_);

    auto *posterOverlay = new QWidget(posterWidget_);
    makeTransparentForMouse(posterOverlay);
    auto *posterLayout = new QVBoxLayout(posterOverlay);
    posterLayout->setContentsMargins(14, 14, 14, 14);
    posterLayout->setSpacing(10);

    auto *badgeRow = new QHBoxLayout();
    badgeRow->setContentsMargins(0, 0, 0, 0);
    badgeRow->setSpacing(8);

    auto *recommendedBadge = new QLabel("推荐", posterWidget_);
    recommendedBadge->setStyleSheet(
        "padding: 4px 8px;"
        "border-radius: 10px;"
        "background: rgba(255, 255, 255, 0.18);"
        "color: #f8fafc;"
        "font-size: 10px;"
        "font-weight: 700;"
        "letter-spacing: 0.5px;");
    makeTransparentForMouse(recommendedBadge);
    badgeRow->addWidget(recommendedBadge, 0, Qt::AlignLeft);
    badgeRow->addStretch();

    auto *durationLabel = new QLabel(data.duration, posterWidget_);
    durationLabel->setStyleSheet(
        "padding: 4px 8px;"
        "border-radius: 10px;"
        "background: rgba(15, 23, 42, 0.32);"
        "color: #f8fafc;"
        "font-size: 11px;"
        "font-weight: 600;");
    makeTransparentForMouse(durationLabel);
    badgeRow->addWidget(durationLabel, 0, Qt::AlignRight);
    posterLayout->addLayout(badgeRow);

    posterLayout->addStretch();

    auto *playLabel = new QLabel("点击卡片任意位置播放", posterWidget_);
    playLabel->setWordWrap(true);
    playLabel->setStyleSheet(
        "color: #f8fafc;"
        "padding: 10px 12px;"
        "border-radius: 14px;"
        "background: rgba(15, 23, 42, 0.42);"
        "font-size: 18px;"
        "font-weight: 700;");
    makeTransparentForMouse(playLabel);
    posterLayout->addWidget(playLabel);
    posterStack->addWidget(posterOverlay);

    layout->addWidget(posterWidget_);

    titleLabel_ = new QLabel(data.title, this);
    titleLabel_->setObjectName("videoCardTitle");
    titleLabel_->setWordWrap(true);
    makeTransparentForMouse(titleLabel_);
    layout->addWidget(titleLabel_);

    footerWidget_ = new QWidget(this);
    auto *footerLayout = new QHBoxLayout(footerWidget_);
    footerLayout->setContentsMargins(0, 0, 0, 0);
    footerLayout->setSpacing(12);

    metaLabel_ = new QLabel(QString("%1  |  Short video").arg(data.creator), footerWidget_);
    metaLabel_->setObjectName("videoCardMeta");
    metaLabel_->setWordWrap(true);
    makeTransparentForMouse(metaLabel_);
    footerLayout->addWidget(metaLabel_, 1);

    likeButton_ = new QPushButton(footerWidget_);
    likeButton_->setCursor(Qt::PointingHandCursor);
    likeButton_->setFocusPolicy(Qt::NoFocus);
    likeButton_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    footerLayout->addWidget(likeButton_, 0, Qt::AlignRight | Qt::AlignVCenter);
    connect(likeButton_, &QPushButton::clicked, this, [this]() {
        if (likeBusy_) {
            return;
        }

        emit likeToggled(videoId_, !likedByMe_);
    });

    layout->addWidget(footerWidget_);
    updateLikeButtonAppearance();

    setCardWidth(248);
}

QString VideoCard::videoId() const
{
    return videoId_;
}

void VideoCard::setCardWidth(int width)
{
    setFixedWidth(width);
    updatePreviewHeight();
}

void VideoCard::setPosterPixmap(const QPixmap &pixmap)
{
    posterPixmap_ = pixmap;
    updatePosterAppearance();
}

void VideoCard::setLikeState(qint64 likeCount, bool likedByMe)
{
    likeCount_ = qMax<qint64>(0, likeCount);
    likedByMe_ = likedByMe;
    updateLikeButtonAppearance();
    updatePreviewHeight();
}

void VideoCard::setLikeBusy(bool busy)
{
    likeBusy_ = busy;
    updateLikeButtonAppearance();
    updatePreviewHeight();
}

void VideoCard::resizeEvent(QResizeEvent *event)
{
    QPushButton::resizeEvent(event);
    updatePosterAppearance();
}

void VideoCard::updatePreviewHeight()
{
    const int previewHeight = qMax(260, (width() * 5) / 4);
    const int textWidth = qMax(120, width() - 28);
    const int likeButtonWidth = likeButton_ ? qMax(96, likeButton_->sizeHint().width()) : 0;
    const int footerSpacing = 12;
    const int metaWidth = qMax(60, textWidth - likeButtonWidth - footerSpacing);

    posterWidget_->setFixedHeight(previewHeight);
    titleLabel_->setFixedWidth(textWidth);
    if (metaLabel_) {
        metaLabel_->setFixedWidth(metaWidth);
    }
    if (likeButton_) {
        likeButton_->setFixedWidth(likeButtonWidth);
    }
    if (footerWidget_) {
        const int footerHeight = qMax(
            metaLabel_ ? metaLabel_->sizeHint().height() : 0,
            likeButton_ ? likeButton_->sizeHint().height() : 0);
        footerWidget_->setFixedWidth(textWidth);
        footerWidget_->setFixedHeight(footerHeight);
    }
    setFixedHeight(
        previewHeight +
        titleLabel_->sizeHint().height() +
        (footerWidget_ ? footerWidget_->height() : 0) +
        58);
    updatePosterAppearance();
}

void VideoCard::updatePosterAppearance()
{
    if (!posterWidget_ || !posterImageLabel_) {
        return;
    }

    const QSize targetSize = posterWidget_->size();
    if (targetSize.width() <= 0 || targetSize.height() <= 0) {
        return;
    }

    if (posterPixmap_.isNull()) {
        posterWidget_->setStyleSheet(
            QString(
                "border-radius: 18px;"
                "background: qlineargradient(x1:0, y1:0, x2:1, y2:1,"
                " stop:0 %1, stop:1 %2);")
                .arg(accentStart_, accentEnd_));
        posterImageLabel_->clear();
        return;
    }

    posterWidget_->setStyleSheet("border-radius: 18px; background: #0f172a;");

    const QPixmap scaled = posterPixmap_.scaled(
        targetSize,
        Qt::KeepAspectRatioByExpanding,
        Qt::SmoothTransformation);

    QPixmap rounded(targetSize);
    rounded.fill(Qt::transparent);

    QPainter painter(&rounded);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    QPainterPath clipPath;
    clipPath.addRoundedRect(QRectF(rounded.rect()), 18.0, 18.0);
    painter.setClipPath(clipPath);

    const QPoint offset(
        (targetSize.width() - scaled.width()) / 2,
        (targetSize.height() - scaled.height()) / 2);
    painter.drawPixmap(offset, scaled);

    posterImageLabel_->setPixmap(rounded);
}

void VideoCard::updateLikeButtonAppearance()
{
    if (!likeButton_) {
        return;
    }

    QString label;
    if (likeBusy_) {
        label = "保存中...";
    } else if (likedByMe_) {
        label = QString("已点赞 %1").arg(likeCount_);
    } else {
        label = QString("点赞 %1").arg(likeCount_);
    }

    const QString background = likeBusy_
        ? "#e2e8f0"
        : (likedByMe_ ? "#dbeafe" : "#f8fafc");
    const QString border = likeBusy_
        ? "#cbd5e1"
        : (likedByMe_ ? "#60a5fa" : "#cbd5e1");
    const QString textColor = likeBusy_
        ? "#475569"
        : (likedByMe_ ? "#1d4ed8" : "#334155");
    const QString hoverBackground = likedByMe_ ? "#bfdbfe" : "#eff6ff";

    likeButton_->setText(label);
    likeButton_->setToolTip(likedByMe_ ? "取消点赞" : "点赞此视频");
    likeButton_->setEnabled(!likeBusy_);
    likeButton_->setStyleSheet(
        QString(
            "QPushButton {"
            "  min-width: 96px;"
            "  padding: 7px 12px;"
            "  border-radius: 12px;"
            "  border: 1px solid %1;"
            "  background: %2;"
            "  color: %3;"
            "  font-size: 12px;"
            "  font-weight: 700;"
            "}"
            "QPushButton:hover { background: %4; }"
            "QPushButton:disabled { color: #64748b; }")
            .arg(border, background, textColor, hoverBackground));
}

}  // namespace frontend::components
