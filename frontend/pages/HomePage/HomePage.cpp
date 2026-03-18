#include "pages/HomePage/HomePage.hpp"

#include "components/VideoCard/VideoCard.hpp"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QResizeEvent>
#include <QScrollArea>
#include <QVBoxLayout>

namespace frontend::pages {

HomePage::HomePage(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
    populateFeed();
    relayoutCards();
}

void HomePage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    relayoutCards();
}

void HomePage::buildUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(32, 28, 32, 28);
    layout->setSpacing(20);

    auto *headerLayout = new QHBoxLayout();
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(16);

    auto *titleBlock = new QVBoxLayout();
    titleBlock->setContentsMargins(0, 0, 0, 0);
    titleBlock->setSpacing(6);

    // auto *eyebrow = new QLabel("PHASE 3", this);
    // eyebrow->setStyleSheet(
    //     "font-size: 12px; font-weight: 700; letter-spacing: 1px; color: #2563eb;");
    // titleBlock->addWidget(eyebrow);

    // auto *title = new QLabel("Short video feed", this);
    // title->setStyleSheet("font-size: 28px; font-weight: 700; color: #0f172a;");
    // titleBlock->addWidget(title);

    // auto *summary = new QLabel(
    //     "响应式卡片会根据页面大小进行调整，因此在较大的屏幕上，桌面首页可以展示更多的视频内容。",
    //     this);
    // summary->setWordWrap(true);
    // summary->setStyleSheet("font-size: 14px; color: #475569;");
    // titleBlock->addWidget(summary);

    headerLayout->addLayout(titleBlock, 1);

    feedStatsLabel_ = new QLabel(this);
    feedStatsLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    feedStatsLabel_->setStyleSheet(
        "padding: 12px 16px;"
        "border: 1px solid #dbe4f0;"
        "border-radius: 14px;"
        "background: #ffffff;"
        "font-size: 13px;"
        "font-weight: 600;"
        "color: #334155;");
    headerLayout->addWidget(feedStatsLabel_);

    layout->addLayout(headerLayout);

    feedScrollArea_ = new QScrollArea(this);
    feedScrollArea_->setFrameShape(QFrame::NoFrame);
    feedScrollArea_->setWidgetResizable(true);
    feedScrollArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    feedScrollArea_->setStyleSheet(
        "QScrollArea { background: transparent; }"
        "QScrollArea > QWidget > QWidget { background: transparent; }");

    feedContainer_ = new QWidget(feedScrollArea_);
    feedGrid_ = new QGridLayout(feedContainer_);
    feedGrid_->setContentsMargins(0, 4, 0, 0);
    feedGrid_->setHorizontalSpacing(18);
    feedGrid_->setVerticalSpacing(18);
    feedGrid_->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    feedScrollArea_->setWidget(feedContainer_);
    layout->addWidget(feedScrollArea_, 1);
}

void HomePage::populateFeed()
{
    using frontend::components::VideoCard;
    using frontend::components::VideoCardData;

    const QVector<VideoCardData> demoFeed = {
        {"video-001", "City light walk after work", "Ari Studio", "00:42", "#2563eb", "#38bdf8"},
        {"video-002", "Street food in one minute", "North Pier", "01:08", "#f97316", "#fb7185"},
        {"video-003", "Desk setup refresh for editing", "Cut Lab", "00:35", "#0f766e", "#2dd4bf"},
        {"video-004", "Weekend mountain ride log", "Miles Daily", "00:58", "#7c3aed", "#c084fc"},
        {"video-005", "How the thumbnail was framed", "Frame Notes", "00:46", "#dc2626", "#fb7185"},
        {"video-006", "Coffee bar workflow montage", "Daybreak", "00:51", "#ca8a04", "#facc15"},
        {"video-007", "Late-night coding sprint", "Terminal FM", "01:12", "#1d4ed8", "#22d3ee"},
        {"video-008", "Quick keyboard sound test", "Studio 87", "00:29", "#059669", "#34d399"},
        {"video-009", "Minimal room makeover", "Soft Corner", "00:54", "#9333ea", "#f472b6"},
        {"video-010", "Five cuts for a punchier intro", "Edit Coach", "01:04", "#ea580c", "#fdba74"},
        {"video-011", "Rainy window b-roll pack", "Mono Weather", "00:39", "#334155", "#60a5fa"},
        {"video-012", "Fast meal prep before stream", "Kitchen Loop", "00:44", "#be123c", "#fb7185"}
    };

    cards_.reserve(demoFeed.size());

    for (const auto &video : demoFeed) {
        auto *card = new VideoCard(video, feedContainer_);
        connect(card, &QPushButton::clicked, this, [this, video]() {
            emit playRequested(video.id, video.title, video.creator, video.duration);
        });
        cards_.append(card);
    }
}

void HomePage::relayoutCards()
{
    if (!feedScrollArea_ || !feedGrid_ || cards_.isEmpty()) {
        return;
    }

    while (auto *item = feedGrid_->takeAt(0)) {
        delete item;
    }

    const QMargins margins = feedGrid_->contentsMargins();
    const int availableWidth = feedScrollArea_->viewport()->width() - margins.left() - margins.right();
    if (availableWidth <= 0) {
        return;
    }

    constexpr int preferredCardWidth = 248;
    constexpr int minimumCardWidth = 216;
    const int spacing = feedGrid_->horizontalSpacing();

    int columns = qMax(1, (availableWidth + spacing) / (preferredCardWidth + spacing));
    while (columns > 1) {
        const int candidateWidth = (availableWidth - ((columns - 1) * spacing)) / columns;
        if (candidateWidth >= minimumCardWidth) {
            break;
        }

        --columns;
    }

    const int cardWidth = qMax(
        minimumCardWidth,
        (availableWidth - ((columns - 1) * spacing)) / columns);

    for (int index = 0; index < cards_.size(); ++index) {
        auto *card = cards_.at(index);
        card->setCardWidth(cardWidth);
        feedGrid_->addWidget(card, index / columns, index % columns, Qt::AlignTop | Qt::AlignLeft);
    }

    const int estimatedVisibleRows = qMax(1, feedScrollArea_->viewport()->height() / 410);
    const int visibleCardCount = qMin(cards_.size(), estimatedVisibleRows * columns);
    feedStatsLabel_->setText(
        QString("%1 videos  |  %2 per row  |  about %3 visible")
            .arg(cards_.size())
            .arg(columns)
            .arg(visibleCardCount));
}

}  // namespace frontend::pages
