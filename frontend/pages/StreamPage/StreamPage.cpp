#include "pages/StreamPage/StreamPage.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace frontend::pages {

StreamPage::StreamPage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(32, 28, 32, 28);
    layout->setSpacing(20);

    auto *title = new QLabel("Live stream workspace", this);
    title->setStyleSheet("font-size: 28px; font-weight: 700; color: #0f172a;");
    layout->addWidget(title);

    auto *summary = new QLabel(
        "This page stays dedicated to live streaming. Short-video cards from Home now open in a separate playback window instead of switching here.",
        this);
    summary->setWordWrap(true);
    summary->setStyleSheet("font-size: 14px; color: #475569;");
    layout->addWidget(summary);

    auto *contentLayout = new QHBoxLayout();
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(20);

    auto *previewPanel = new QFrame(this);
    previewPanel->setMinimumSize(640, 420);
    previewPanel->setStyleSheet(
        "background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #111827, stop:1 #0f766e);"
        "border-radius: 26px;");

    auto *previewLayout = new QVBoxLayout(previewPanel);
    previewLayout->setContentsMargins(28, 28, 28, 28);
    previewLayout->setSpacing(12);

    auto *previewTitle = new QLabel("Live preview placeholder", previewPanel);
    previewTitle->setStyleSheet("font-size: 26px; font-weight: 700; color: #f8fafc;");
    previewLayout->addWidget(previewTitle);

    auto *previewHint = new QLabel(
        "Reserve this surface for future live preview, streaming diagnostics, or channel control widgets.",
        previewPanel);
    previewHint->setWordWrap(true);
    previewHint->setStyleSheet("font-size: 14px; color: rgba(248, 250, 252, 0.82);");
    previewLayout->addWidget(previewHint);
    previewLayout->addStretch();

    contentLayout->addWidget(previewPanel, 2);

    auto *sidePanel = new QFrame(this);
    sidePanel->setMinimumWidth(300);
    sidePanel->setStyleSheet(
        "background: #ffffff;"
        "border: 1px solid #dbe4f0;"
        "border-radius: 22px;");

    auto *sideLayout = new QVBoxLayout(sidePanel);
    sideLayout->setContentsMargins(22, 22, 22, 22);
    sideLayout->setSpacing(16);

    auto *eyebrow = new QLabel("LIVE MODULE", sidePanel);
    eyebrow->setStyleSheet(
        "font-size: 11px; font-weight: 700; letter-spacing: 1px; color: #0f766e;");
    sideLayout->addWidget(eyebrow);

    auto *panelTitle = new QLabel("Reserved for future stream entry flow", sidePanel);
    panelTitle->setWordWrap(true);
    panelTitle->setStyleSheet("font-size: 22px; font-weight: 700; color: #0f172a;");
    sideLayout->addWidget(panelTitle);

    auto *panelBody = new QLabel(
        "When the live streaming phase starts, this page can host room selection, push status, live preview, and broadcaster controls without colliding with the short-video feed.",
        sidePanel);
    panelBody->setWordWrap(true);
    panelBody->setStyleSheet("font-size: 14px; color: #334155;");
    sideLayout->addWidget(panelBody);

    auto *checklist = new QLabel(
        "Suggested next live features\n1. Stream room list\n2. Push / stop controls\n3. Bitrate and device status\n4. Live chat entrance",
        sidePanel);
    checklist->setWordWrap(true);
    checklist->setStyleSheet(
        "padding: 16px;"
        "background: #f8fafc;"
        "border: 1px solid #e2e8f0;"
        "border-radius: 16px;"
        "font-size: 13px;"
        "color: #334155;");
    sideLayout->addWidget(checklist);
    sideLayout->addStretch();

    contentLayout->addWidget(sidePanel, 1);
    layout->addLayout(contentLayout, 1);
}

}  // namespace frontend::pages
