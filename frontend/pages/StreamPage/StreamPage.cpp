#include "pages/StreamPage/StreamPage.hpp"

#include <QLabel>
#include <QVBoxLayout>

namespace frontend::pages {

StreamPage::StreamPage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(40, 36, 40, 36);
    layout->setSpacing(12);

    auto *title = new QLabel("Stream entry", this);
    title->setStyleSheet("font-size: 28px; font-weight: 700; color: #0f172a;");
    layout->addWidget(title);

    auto *summary = new QLabel(
        "Live streaming and real-time media features will be added here after the base architecture is stable.",
        this);
    summary->setWordWrap(true);
    summary->setStyleSheet("font-size: 14px; color: #475569;");
    layout->addWidget(summary);

    layout->addStretch();
}

}  // namespace frontend::pages
