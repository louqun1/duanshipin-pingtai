#include "pages/AccountPage/AccountPage.hpp"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace frontend::pages
{

    namespace
    {
        void styleFormLabel(QFormLayout *form, QWidget *field)
        {
            if (!form || !field)
            {
                return;
            }

            auto *label = qobject_cast<QLabel *>(form->labelForField(field));
            if (!label)
            {
                return;
            }

            label->setStyleSheet("color: #2f2626; font-size: 14px; font-weight: 600; background: transparent; border: none;");
        }
    }

    AccountPage::AccountPage(QWidget *parent)
        : QWidget(parent)
    {
        auto *rootLayout = new QVBoxLayout(this);
        rootLayout->setContentsMargins(0, 0, 0, 0);
        rootLayout->setSpacing(0);

        auto *scrollArea = new QScrollArea(this);
        scrollArea->setWidgetResizable(true);
        scrollArea->setFrameShape(QFrame::NoFrame);
        scrollArea->setStyleSheet("background: transparent; border: none;");
        rootLayout->addWidget(scrollArea);

        auto *content = new QWidget(scrollArea);
        scrollArea->setWidget(content);

        auto *layout = new QVBoxLayout(content);
        layout->setContentsMargins(40, 36, 40, 36);
        layout->setSpacing(18);

        auto *title = new QLabel("用户中心", this);
        title->setStyleSheet("font-size: 28px; font-weight: 700; color: #0f172a;");
        layout->addWidget(title);

        auto *summary = new QLabel(
            "登录以查看您的账户详情。登录成功后，此页面将自动切换到个人资料视图。",
            this);
        summary->setWordWrap(true);
        summary->setStyleSheet("font-size: 14px; color: #475569;");
        layout->addWidget(summary);

        stateStack_ = new QStackedWidget(this);
        stateStack_->addWidget(buildGuestPanel());
        stateStack_->addWidget(buildProfilePanel());
        layout->addWidget(stateStack_, 1);

        showLoggedOutState("请登录以查看您的账户详情。");
        // 添加调试
        qDebug() << "\n=== AccountPage constructor end ===";

    }

    QWidget *AccountPage::buildGuestPanel()
    {
        qDebug() << "\n=== Starting buildGuestPanel ===";
        auto *panel = new QWidget(this);
        panel->setObjectName("accountGuestPanel");
        panel->setStyleSheet("#accountGuestPanel { background: white; border-radius: 18px; border: 1px solid #dbe3ef; }");

        auto *layout = new QVBoxLayout(panel);
        qDebug() << "Panel created:" << panel;
        // qDebug() << "Panel initial visible:" << panel->isVisible();
        layout->setContentsMargins(28, 28, 28, 28);
        layout->setSpacing(16);
        // qDebug() << "Layout created, margins:" << layout->contentsMargins();
        auto *headline = new QLabel("未登录", panel);
        headline->setStyleSheet("font-size: 22px; font-weight: 700; color: #0f172a;");
        layout->addWidget(headline);
        // qDebug() << "Headline added, visible:" << headline->isVisible();

        auto *description = new QLabel(
            "使用同一个账户页面进行登录、注册，然后继续进入您的基本个人资料页面。",
            panel);
        description->setWordWrap(true);
        description->setStyleSheet("font-size: 14px; color: #64748b;");
        layout->addWidget(description);
        // qDebug() << "Description added, visible:" << description->isVisible();

        guestStatusLabel_ = new QLabel(panel);
        guestStatusLabel_->setWordWrap(true);
        guestStatusLabel_->setStyleSheet("font-size: 13px;");
        layout->addWidget(guestStatusLabel_);
        // qDebug() << "Guest status label created:" << guestStatusLabel_;
        // qDebug() << "Guest status label visible:" << guestStatusLabel_->isVisible();

        // 创建表单
        qDebug() << "\n--- Creating form ---";
        auto *form = new QFormLayout();
        // qDebug() << "Form layout created:" << form;
        // // 调试：检查 form 的属性
        // qDebug() << "Form spacing - horizontal:" << form->horizontalSpacing()
        //          << "vertical:" << form->verticalSpacing();
        // qDebug() << "Form margins:" << form->contentsMargins();
        form->setLabelAlignment(Qt::AlignLeft);
        form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
        form->setHorizontalSpacing(16);
        form->setVerticalSpacing(12);

        // 创建用户名输入框
        qDebug() << "\n--- Creating username edit ---";
        usernameEdit_ = new QLineEdit(panel);

        usernameEdit_->setPlaceholderText("用户名");
        usernameEdit_->setStyleSheet(
            "QLineEdit {"
            "   color: #000000;"           // 输入文字黑色
            "   background-color: #FFFFFF;" // 背景白色
            "   border: 1px solid #d1d5db;"
            "   border-radius: 6px;"
            "   padding: 8px 12px;"
            "}"
        );
        form->addRow("用户名", usernameEdit_);
        styleFormLabel(form, usernameEdit_);

        // 创建密码输入框
        qDebug() << "\n--- Creating password edit ---";
        passwordEdit_ = new QLineEdit(panel);
        passwordEdit_->setStyleSheet(
            "QLineEdit {"
            "   color: #000000;"           // 输入文字黑色
            "   background-color: #FFFFFF;" // 背景白色
            "   border: 1px solid #d1d5db;"
            "   border-radius: 6px;"
            "   padding: 8px 12px;"
            "}"
        );
        passwordEdit_->setPlaceholderText("密码");
        passwordEdit_->setEchoMode(QLineEdit::Password);
        // qDebug() << "Password mode set, visible:" << passwordEdit_->isVisible();
        form->addRow("密码", passwordEdit_);
        styleFormLabel(form, passwordEdit_);
        // qDebug() << "After addRow - password edit visible:" << passwordEdit_->isVisible();
        // qDebug() << "After addRow - password edit parent:" << passwordEdit_->parent();

        // 创建邮箱输入框
        emailEdit_ = new QLineEdit(panel);
        emailEdit_->setPlaceholderText("注册邮箱");
        emailEdit_->setStyleSheet(
            "QLineEdit {"
            "   color: #000000;"           // 输入文字黑色
            "   background-color: #FFFFFF;" // 背景白色
            "   border: 1px solid #d1d5db;"
            "   border-radius: 6px;"
            "   padding: 8px 12px;"
            "}"
        );
        form->addRow("邮箱", emailEdit_);
        styleFormLabel(form, emailEdit_);
        // qDebug() << "Form row count:" << form->rowCount(); // 应该输出 3

        layout->addLayout(form); // 添加 form 到主布局


        auto *actions = new QHBoxLayout();
        actions->setSpacing(12);

        auto *loginButton = new QPushButton("登录", panel);
        actions->addWidget(loginButton);
        //设置样式跟上面标签相似
        loginButton->setStyleSheet("color: #2563eb; font-size: 14px; font-weight: 600; background: transparent; border: none;");

        auto *registerButton = new QPushButton("注册", panel);
        actions->addWidget(registerButton);
        registerButton->setStyleSheet("color: #2563eb; font-size: 14px; font-weight: 600; background: transparent; border: none;");

        actions->addStretch();
        layout->addLayout(actions);
        layout->addStretch();

        connect(loginButton, &QPushButton::clicked, this, [this]()
                { emit loginRequested(usernameEdit_->text().trimmed(), passwordEdit_->text()); });

        connect(registerButton, &QPushButton::clicked, this, [this]()
                { emit registerRequested(
                      usernameEdit_->text().trimmed(),
                      passwordEdit_->text(),
                      emailEdit_->text().trimmed()); });

        return panel;
    }

    QWidget *AccountPage::buildProfilePanel()
    {
        auto *panel = new QWidget(this);
        panel->setObjectName("accountProfilePanel");
        panel->setStyleSheet("#accountProfilePanel { background: white; border-radius: 18px; border: 1px solid #dbe3ef; }");

        auto *layout = new QVBoxLayout(panel);
        layout->setContentsMargins(28, 28, 28, 28);
        layout->setSpacing(16);

        auto *headline = new QLabel("个人资料", panel);
        headline->setStyleSheet("font-size: 22px; font-weight: 700; color: #0f172a;");
        layout->addWidget(headline);

        auto *description = new QLabel(
            "这是第二阶段的基本个人页面。之后还会添加更多的个人资料编辑功能和创作者工具。",
            panel);
        description->setWordWrap(true);
        description->setStyleSheet("font-size: 14px; color: #64748b;");
        layout->addWidget(description);

        auto *infoLayout = new QFormLayout();
        infoLayout->setHorizontalSpacing(16);
        infoLayout->setVerticalSpacing(12);

        profileUsernameValue_ = new QLabel(panel);
        infoLayout->addRow("Username", profileUsernameValue_);
        styleFormLabel(infoLayout, profileUsernameValue_);

        profileEmailValue_ = new QLabel(panel);
        infoLayout->addRow("Email", profileEmailValue_);
        styleFormLabel(infoLayout, profileEmailValue_);

        layout->addLayout(infoLayout);

        profileStatusLabel_ = new QLabel(panel);
        profileStatusLabel_->setWordWrap(true);
        profileStatusLabel_->setStyleSheet("font-size: 13px; color: #2563eb;");
        layout->addWidget(profileStatusLabel_);

        auto *logoutButton = new QPushButton("Log out", panel);
        logoutButton->setFixedWidth(120);
        logoutButton->setStyleSheet("color: #b91c1c; font-size: 14px; font-weight: 600; background: transparent; border: 1px solid #b91c1c;");
        layout->addWidget(logoutButton, 0, Qt::AlignLeft);
        layout->addStretch();

        connect(logoutButton, &QPushButton::clicked, this, &AccountPage::logoutRequested);

        return panel;
    }

    void AccountPage::showLoggedOutState(const QString &message)
    {
        if (stateStack_)
        {
            stateStack_->setCurrentIndex(0);
        }

        if (usernameEdit_)
        {
            usernameEdit_->clear();
        }
        if (passwordEdit_)
        {
            passwordEdit_->clear();
        }
        if (emailEdit_)
        {
            emailEdit_->clear();
        }

        updateGuestStatus(message.isEmpty() ? "请登陆后使用此功能。" : message, true);
        updateProfileStatus(QString());
        if (profileNicknameValue_)
        {
            profileNicknameValue_->clear();
        }
    }

    void AccountPage::showAuthenticatedState(const QString &username, const QString &email, const QString &message)
    {
        if (profileUsernameValue_)
        {
            profileUsernameValue_->setText(username);
        }
        if (profileEmailValue_)
        {
            profileEmailValue_->setText(email.isEmpty() ? "未设置" : email);
        }

        updateGuestStatus(QString(), false);
        updateProfileStatus(message.isEmpty() ? QString("已登录为 %1").arg(username) : message);

        if (stateStack_)
        {
            stateStack_->setCurrentIndex(1);
        }
    }

    void AccountPage::updateGuestStatus(const QString &message, bool isError)
    {
        if (!guestStatusLabel_)
        {
            return;
        }

        guestStatusLabel_->setText(message);
        guestStatusLabel_->setStyleSheet(
            QString("font-size: 13px; color: %1;").arg(isError ? "#b91c1c" : "#2563eb"));
    }

    void AccountPage::updateProfileStatus(const QString &message)
    {
        if (!profileStatusLabel_)
        {
            return;
        }

        profileStatusLabel_->setText(message);
    }

} // namespace frontend::pages
