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

    auto *title = new QLabel("Login workspace", this);
    title->setStyleSheet("font-size: 28px; font-weight: 700; color: #0f172a;");
    rootLayout->addWidget(title);

    auto *summary = new QLabel(
        "Signals are ready here for the future AuthController, but the actual auth flow will be added in the next phase.",
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
    usernameEdit_->setPlaceholderText("Username");
    cardLayout->addWidget(usernameEdit_);

    passwordEdit_ = new QLineEdit(card);
    passwordEdit_->setPlaceholderText("Password");
    passwordEdit_->setEchoMode(QLineEdit::Password);
    cardLayout->addWidget(passwordEdit_);

    emailEdit_ = new QLineEdit(card);
    emailEdit_->setPlaceholderText("Email (for registration)");
    cardLayout->addWidget(emailEdit_);

    auto *actions = new QHBoxLayout();

    auto *loginButton = new QPushButton("Emit login signal", card);
    actions->addWidget(loginButton);

    auto *registerButton = new QPushButton("Emit register signal", card);
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
