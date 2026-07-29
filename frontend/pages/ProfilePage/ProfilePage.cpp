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

    auto *title = new QLabel("个人中心", this);
    title->setStyleSheet("font-size: 28px; font-weight: 700; color: #0f172a;");
    layout->addWidget(title);

    auto *summary = new QLabel(
        "用户资料、创作者中心和账户设置将由后端服务驱动，而非在 MainWindow 中存储。",
        this);
    summary->setWordWrap(true);
    summary->setStyleSheet("font-size: 14px; color: #475569;");
    layout->addWidget(summary);

    layout->addStretch();
}

}  // namespace frontend::pages
