#include "components/VideoCard/VideoCard.hpp"

#include <QHBoxLayout>
#include <QLabel>
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
    posterWidget_->setStyleSheet(
        QString(
            "border-radius: 18px;"
            "background: qlineargradient(x1:0, y1:0, x2:1, y2:1,"
            " stop:0 %1, stop:1 %2);")
            .arg(data.accentStart, data.accentEnd));
    makeTransparentForMouse(posterWidget_);

    auto *posterLayout = new QVBoxLayout(posterWidget_);
    posterLayout->setContentsMargins(14, 14, 14, 14);
    posterLayout->setSpacing(10);

    auto *badgeRow = new QHBoxLayout();
    badgeRow->setContentsMargins(0, 0, 0, 0);
    badgeRow->setSpacing(8);

    auto *recommendedBadge = new QLabel("RECOMMENDED", posterWidget_);
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

    auto *playLabel = new QLabel("Click anywhere on the card to play", posterWidget_);
    playLabel->setWordWrap(true);
    playLabel->setStyleSheet(
        "color: #f8fafc;"
        "font-size: 18px;"
        "font-weight: 700;");
    makeTransparentForMouse(playLabel);
    posterLayout->addWidget(playLabel);

    layout->addWidget(posterWidget_);

    titleLabel_ = new QLabel(data.title, this);
    titleLabel_->setObjectName("videoCardTitle");
    titleLabel_->setWordWrap(true);
    makeTransparentForMouse(titleLabel_);
    layout->addWidget(titleLabel_);

    metaLabel_ = new QLabel(QString("%1  |  Short video").arg(data.creator), this);
    metaLabel_->setObjectName("videoCardMeta");
    metaLabel_->setWordWrap(true);
    makeTransparentForMouse(metaLabel_);
    layout->addWidget(metaLabel_);

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

void VideoCard::updatePreviewHeight()
{
    const int previewHeight = qMax(260, (width() * 5) / 4);
    const int textWidth = qMax(120, width() - 28);

    posterWidget_->setFixedHeight(previewHeight);
    titleLabel_->setFixedWidth(textWidth);
    metaLabel_->setFixedWidth(textWidth);
    setFixedHeight(previewHeight + titleLabel_->sizeHint().height() + metaLabel_->sizeHint().height() + 52);
}

}  // namespace frontend::components
