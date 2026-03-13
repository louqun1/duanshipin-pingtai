#include "pages/HomePage/HomePage.hpp"

#include <QLabel>
#include <QVBoxLayout>

namespace frontend::pages {

HomePage::HomePage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(40, 36, 40, 36);
    layout->setSpacing(12);

    auto *title = new QLabel("Home feed", this);
    title->setStyleSheet("font-size: 28px; font-weight: 700; color: #0f172a;");
    layout->addWidget(title);

    auto *summary = new QLabel(
        "This page will host the short-video feed, recommendation cards, and playback entry points.",
        this);
    summary->setWordWrap(true);
    summary->setStyleSheet("font-size: 14px; color: #475569;");
    layout->addWidget(summary);

    layout->addStretch();
}

}  // namespace frontend::pages
