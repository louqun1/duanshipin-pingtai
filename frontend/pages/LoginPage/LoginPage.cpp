#include "pages/LoginPage/LoginPage.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

namespace frontend::pages {

LoginPage::LoginPage(QWidget *parent)
    : QWidget(parent)
{
    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(40, 36, 40, 36);

    auto *title = new QLabel("登录工作区", this);
    title->setStyleSheet("font-size: 28px; font-weight: 700; color: #0f172a;");
    rootLayout->addWidget(title);

    auto *summary = new QLabel(
        "此处为未来 AuthController 预留了信号接口，实际认证流程将在下一阶段添加。",
        this);
    summary->setWordWrap(true);
    summary->setStyleSheet("font-size: 14px; color: #475569;");
    rootLayout->addWidget(summary);
    rootLayout->addSpacing(20);

    auto *card = new QWidget(this);
    card->setStyleSheet(
        "background: white;"
        "border-radius: 16px;"
        "border: 1px solid #dbe3ef;");

    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(24, 24, 24, 24);
    cardLayout->setSpacing(12);

    usernameEdit_ = new QLineEdit(card);
    usernameEdit_->setPlaceholderText("用户名");
    cardLayout->addWidget(usernameEdit_);

    passwordEdit_ = new QLineEdit(card);
    passwordEdit_->setPlaceholderText("密码");
    passwordEdit_->setEchoMode(QLineEdit::Password);
    cardLayout->addWidget(passwordEdit_);

    emailEdit_ = new QLineEdit(card);
    emailEdit_->setPlaceholderText("邮箱（注册用）");
    cardLayout->addWidget(emailEdit_);

    auto *actions = new QHBoxLayout();

    auto *loginButton = new QPushButton("发送登录信号", card);
    actions->addWidget(loginButton);

    auto *registerButton = new QPushButton("发送注册信号", card);
    actions->addWidget(registerButton);

    cardLayout->addLayout(actions);
    rootLayout->addWidget(card);
    rootLayout->addStretch();

    connect(loginButton, &QPushButton::clicked, this, [this]() {
        emit loginRequested(usernameEdit_->text().trimmed(), passwordEdit_->text());
    });

    connect(registerButton, &QPushButton::clicked, this, [this]() {
        emit registerRequested(
            usernameEdit_->text().trimmed(),
            passwordEdit_->text(),
            emailEdit_->text().trimmed());
    });
}

}  // namespace frontend::pages
