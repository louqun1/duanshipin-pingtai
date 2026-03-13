#include "pages/UploadPage/UploadPage.hpp"

#include <QLabel>
#include <QVBoxLayout>

namespace frontend::pages {

UploadPage::UploadPage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(40, 36, 40, 36);
    layout->setSpacing(12);

    auto *title = new QLabel("Upload workspace", this);
    title->setStyleSheet("font-size: 28px; font-weight: 700; color: #0f172a;");
    layout->addWidget(title);

    auto *summary = new QLabel(
        "The upload pipeline, metadata editor, and publish flow will be built in the next stages.",
        this);
    summary->setWordWrap(true);
    summary->setStyleSheet("font-size: 14px; color: #475569;");
    layout->addWidget(summary);

    layout->addStretch();
}

}  // namespace frontend::pages
