#include "pages/ProfilePage/ProfilePage.hpp"

#include <QLabel>
#include <QVBoxLayout>

namespace frontend::pages {

ProfilePage::ProfilePage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(40, 36, 40, 36);
    layout->setSpacing(12);

    auto *title = new QLabel("Profile center", this);
    title->setStyleSheet("font-size: 28px; font-weight: 700; color: #0f172a;");
    layout->addWidget(title);

    auto *summary = new QLabel(
        "User profile, creator center, and account settings will be driven by backend services rather than stored inside MainWindow.",
        this);
    summary->setWordWrap(true);
    summary->setStyleSheet("font-size: 14px; color: #475569;");
    layout->addWidget(summary);

    layout->addStretch();
}

}  // namespace frontend::pages
