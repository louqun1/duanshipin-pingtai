#include "pages/UploadPage/UploadPage.hpp"

#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QHttpMultiPart>
#include <QHttpPart>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMimeDatabase>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QTextEdit>
#include <QUrl>
#include <QVBoxLayout>

namespace frontend::pages {

namespace {

QString apiBaseUrl()
{
    const QString configured = qEnvironmentVariable("FLASHPOINT_API_BASE_URL").trimmed();
    if (!configured.isEmpty()) {
        return configured;
    }

    return QStringLiteral("http://192.168.99.128:8080");
}

QString apiVideoUploadUrl(const QString &baseUrl)
{
    return QString("%1/api/videos/upload").arg(baseUrl);
}

QString defaultFilePrompt()
{
    return QStringLiteral("No file selected yet. Pick a local video file to upload.");
}

}  // namespace

UploadPage::UploadPage(QWidget *parent)
    : QWidget(parent)
{
    networkManager_ = new QNetworkAccessManager(this);
    buildUi();
}

void UploadPage::setAuthToken(const QString &token)
{
    authToken_ = token.trimmed();
    if (authToken_.isEmpty()) {
        setStatusMessage("Sign in before uploading a video.", true);
        return;
    }

    setStatusMessage(QString("Signed in. Ready to upload to %1").arg(apiVideoUploadUrl(apiBaseUrl())));
}

void UploadPage::buildUi()
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
    layout->setContentsMargins(32, 28, 32, 28);
    layout->setSpacing(20);

    auto *title = new QLabel("Upload workspace", this);
    title->setStyleSheet("font-size: 28px; font-weight: 700; color: #0f172a;");
    layout->addWidget(title);

    auto *summary = new QLabel(
        "Sign in first, then select a local video file and send it to the API server for transcoding. "
        "If your worker is running on the VM, the uploaded video will move from queued to ready automatically.",
        this);
    summary->setWordWrap(true);
    summary->setStyleSheet("font-size: 14px; color: #475569;");
    layout->addWidget(summary);

    auto *contentLayout = new QHBoxLayout();
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(20);

    auto *formPanel = new QFrame(this);
    formPanel->setStyleSheet(
        "background: #ffffff;"
        "border: 1px solid #dbe4f0;"
        "border-radius: 22px;");

    auto *formPanelLayout = new QVBoxLayout(formPanel);
    formPanelLayout->setContentsMargins(24, 24, 24, 24);
    formPanelLayout->setSpacing(18);

    auto *sectionTitle = new QLabel("Create a queued upload", formPanel);
    sectionTitle->setStyleSheet("font-size: 22px; font-weight: 700; color: #0f172a;");
    formPanelLayout->addWidget(sectionTitle);

    auto *sectionBody = new QLabel(
        "The API will store the raw file in MinIO raw-media, insert the database records, and create a transcode job.",
        formPanel);
    sectionBody->setWordWrap(true);
    sectionBody->setStyleSheet("font-size: 14px; color: #64748b;");
    formPanelLayout->addWidget(sectionBody);

    auto *fileCard = new QFrame(formPanel);
    fileCard->setStyleSheet(
        "background: #f8fafc;"
        "border: 1px solid #e2e8f0;"
        "border-radius: 18px;");

    auto *fileCardLayout = new QHBoxLayout(fileCard);
    fileCardLayout->setContentsMargins(18, 18, 18, 18);
    fileCardLayout->setSpacing(14);

    auto *fileInfoLayout = new QVBoxLayout();
    fileInfoLayout->setContentsMargins(0, 0, 0, 0);
    fileInfoLayout->setSpacing(6);

    auto *fileLabel = new QLabel("Video file", fileCard);
    fileLabel->setStyleSheet("font-size: 14px; font-weight: 700; color: #0f172a;");
    fileInfoLayout->addWidget(fileLabel);

    selectedFileLabel_ = new QLabel(defaultFilePrompt(), fileCard);
    selectedFileLabel_->setWordWrap(true);
    selectedFileLabel_->setStyleSheet("font-size: 13px; color: #64748b;");
    fileInfoLayout->addWidget(selectedFileLabel_);
    fileCardLayout->addLayout(fileInfoLayout, 1);

    selectFileButton_ = new QPushButton("Choose file", fileCard);
    selectFileButton_->setCursor(Qt::PointingHandCursor);
    selectFileButton_->setStyleSheet(
        "QPushButton {"
        "  padding: 10px 16px;"
        "  border-radius: 12px;"
        "  border: 1px solid #2563eb;"
        "  background: #eff6ff;"
        "  color: #1d4ed8;"
        "  font-size: 13px;"
        "  font-weight: 700;"
        "}"
        "QPushButton:hover { background: #dbeafe; }"
        "QPushButton:disabled { border-color: #cbd5e1; color: #94a3b8; background: #f8fafc; }");
    fileCardLayout->addWidget(selectFileButton_, 0, Qt::AlignTop);
    formPanelLayout->addWidget(fileCard);

    auto *form = new QFormLayout();
    form->setContentsMargins(0, 0, 0, 0);
    form->setLabelAlignment(Qt::AlignLeft);
    form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    form->setHorizontalSpacing(18);
    form->setVerticalSpacing(14);

    titleEdit_ = new QLineEdit(formPanel);
    titleEdit_->setPlaceholderText("Leave blank to use the file name");
    titleEdit_->setStyleSheet(
        "QLineEdit {"
        "  color: #0f172a;"
        "  background: #ffffff;"
        "  border: 1px solid #cbd5e1;"
        "  border-radius: 12px;"
        "  padding: 10px 12px;"
        "}");
    form->addRow("Title", titleEdit_);

    descriptionEdit_ = new QTextEdit(formPanel);
    descriptionEdit_->setPlaceholderText("Optional description shown in the API response and future detail views");
    descriptionEdit_->setMinimumHeight(96);
    descriptionEdit_->setStyleSheet(
        "QTextEdit {"
        "  color: #0f172a;"
        "  background: #ffffff;"
        "  border: 1px solid #cbd5e1;"
        "  border-radius: 12px;"
        "  padding: 10px 12px;"
        "}");
    form->addRow("Description", descriptionEdit_);

    const QString formLabelStyle = QStringLiteral("color: #334155; font-size: 14px; font-weight: 600;");
    for (int row = 0; row < form->rowCount(); ++row) {
        auto *labelItem = form->itemAt(row, QFormLayout::LabelRole);
        if (auto *label = labelItem ? qobject_cast<QLabel *>(labelItem->widget()) : nullptr) {
            label->setStyleSheet(formLabelStyle);
        }
    }
    formPanelLayout->addLayout(form);

    progressBar_ = new QProgressBar(formPanel);
    progressBar_->setRange(0, 100);
    progressBar_->setValue(0);
    progressBar_->setTextVisible(true);
    progressBar_->setVisible(false);
    progressBar_->setStyleSheet(
        "QProgressBar {"
        "  border: 1px solid #dbe4f0;"
        "  border-radius: 10px;"
        "  background: #f8fafc;"
        "  color: #0f172a;"
        "  text-align: center;"
        "}"
        "QProgressBar::chunk {"
        "  border-radius: 9px;"
        "  background: #2563eb;"
        "}");
    formPanelLayout->addWidget(progressBar_);

    statusLabel_ = new QLabel(formPanel);
    statusLabel_->setWordWrap(true);
    statusLabel_->setStyleSheet("font-size: 13px; color: #475569;");
    formPanelLayout->addWidget(statusLabel_);

    auto *actionRow = new QHBoxLayout();
    actionRow->setContentsMargins(0, 0, 0, 0);
    actionRow->setSpacing(12);

    uploadButton_ = new QPushButton("Start upload", formPanel);
    uploadButton_->setCursor(Qt::PointingHandCursor);
    uploadButton_->setStyleSheet(
        "QPushButton {"
        "  padding: 12px 18px;"
        "  border-radius: 14px;"
        "  border: 0;"
        "  background: #2563eb;"
        "  color: #f8fafc;"
        "  font-size: 14px;"
        "  font-weight: 700;"
        "}"
        "QPushButton:hover { background: #1d4ed8; }"
        "QPushButton:disabled { background: #94a3b8; }");
    actionRow->addWidget(uploadButton_, 0, Qt::AlignLeft);
    actionRow->addStretch();
    formPanelLayout->addLayout(actionRow);

    contentLayout->addWidget(formPanel, 3);

    auto *notesPanel = new QFrame(this);
    notesPanel->setMinimumWidth(260);
    notesPanel->setStyleSheet(
        "background: #ffffff;"
        "border: 1px solid #dbe4f0;"
        "border-radius: 22px;");

    auto *notesLayout = new QVBoxLayout(notesPanel);
    notesLayout->setContentsMargins(22, 22, 22, 22);
    notesLayout->setSpacing(16);

    auto *notesEyebrow = new QLabel("UPLOAD FLOW", notesPanel);
    notesEyebrow->setStyleSheet("font-size: 11px; font-weight: 700; letter-spacing: 1px; color: #1d4ed8;");
    notesLayout->addWidget(notesEyebrow);

    auto *notesTitle = new QLabel("What happens after you click upload", notesPanel);
    notesTitle->setWordWrap(true);
    notesTitle->setStyleSheet("font-size: 22px; font-weight: 700; color: #0f172a;");
    notesLayout->addWidget(notesTitle);

    auto *notesBody = new QLabel(
        "1. The desktop client sends the file to the API server.\n"
        "2. The API stores the original file in MinIO raw-media.\n"
        "3. The API inserts a queued transcode job.\n"
        "4. The worker converts it to HLS and generates the cover image.\n"
        "5. Home feed refresh shows the new item and its current status.",
        notesPanel);
    notesBody->setWordWrap(true);
    notesBody->setStyleSheet(
        "padding: 16px;"
        "background: #f8fafc;"
        "border: 1px solid #e2e8f0;"
        "border-radius: 16px;"
        "font-size: 13px;"
        "color: #334155;");
    notesLayout->addWidget(notesBody);

    auto *workerHint = new QLabel(
        "If the video stays in queued status, check whether the worker is already running on the VM.",
        notesPanel);
    workerHint->setWordWrap(true);
    workerHint->setStyleSheet("font-size: 13px; color: #64748b;");
    notesLayout->addWidget(workerHint);
    notesLayout->addStretch();

    contentLayout->addWidget(notesPanel, 2);
    layout->addLayout(contentLayout, 1);

    setStatusMessage("Sign in before uploading a video.");

    connect(selectFileButton_, &QPushButton::clicked, this, &UploadPage::pickVideoFile);
    connect(uploadButton_, &QPushButton::clicked, this, &UploadPage::submitUpload);
}

void UploadPage::pickVideoFile()
{
    const QString filePath = QFileDialog::getOpenFileName(
        this,
        "Select a video file",
        selectedFilePath_.isEmpty() ? QString() : selectedFilePath_,
        "Video files (*.mp4 *.mov *.mkv *.avi *.m4v *.flv *.wmv);;All files (*.*)");

    if (filePath.isEmpty()) {
        return;
    }

    selectedFilePath_ = filePath;
    const QFileInfo info(filePath);
    selectedFileLabel_->setText(QString("%1\n%2").arg(info.fileName(), filePath));
    selectedFileLabel_->setToolTip(filePath);

    if (titleEdit_ && titleEdit_->text().trimmed().isEmpty()) {
        titleEdit_->setText(info.completeBaseName());
    }

    setStatusMessage(QString("Selected %1").arg(info.fileName()));
}

void UploadPage::submitUpload()
{
    QString validationError;
    if (!validateForm(&validationError)) {
        setStatusMessage(validationError, true);
        return;
    }

    auto *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    auto addTextPart = [multiPart](const QByteArray &name, const QString &value) {
        QHttpPart textPart;
        textPart.setHeader(
            QNetworkRequest::ContentDispositionHeader,
            QString("form-data; name=\"%1\"").arg(QString::fromUtf8(name)));
        textPart.setBody(value.toUtf8());
        multiPart->append(textPart);
    };

    addTextPart("title", titleEdit_->text().trimmed());
    addTextPart("desc", descriptionEdit_->toPlainText().trimmed());

    auto *file = new QFile(selectedFilePath_);
    if (!file->open(QIODevice::ReadOnly)) {
        file->deleteLater();
        multiPart->deleteLater();
        setStatusMessage("Failed to open the selected file for reading.", true);
        return;
    }

    const QFileInfo fileInfo(selectedFilePath_);
    const QMimeDatabase mimeDatabase;

    QHttpPart filePart;
    filePart.setHeader(
        QNetworkRequest::ContentDispositionHeader,
        QString("form-data; name=\"file\"; filename=\"%1\"").arg(fileInfo.fileName()));
    filePart.setHeader(
        QNetworkRequest::ContentTypeHeader,
        mimeDatabase.mimeTypeForFile(fileInfo).name());
    filePart.setBodyDevice(file);
    file->setParent(multiPart);
    multiPart->append(filePart);

    QNetworkRequest request(QUrl(apiVideoUploadUrl(apiBaseUrl())));
    request.setRawHeader("Authorization", QByteArray("Bearer ") + authToken_.toUtf8());
    activeReply_ = networkManager_->post(request, multiPart);
    multiPart->setParent(activeReply_);

    setUploading(true);
    setStatusMessage(QString("Uploading %1 ...").arg(fileInfo.fileName()));

    connect(activeReply_, &QNetworkReply::uploadProgress, this, [this](qint64 sent, qint64 total) {
        if (!progressBar_) {
            return;
        }

        progressBar_->setVisible(true);
        if (total <= 0) {
            progressBar_->setRange(0, 0);
            return;
        }

        progressBar_->setRange(0, 100);
        progressBar_->setValue(static_cast<int>((sent * 100) / total));
    });

    connect(activeReply_, &QNetworkReply::finished, this, [this, reply = activeReply_]() {
        handleUploadFinished(reply);
    });
}

void UploadPage::handleUploadFinished(QNetworkReply *reply)
{
    if (!reply) {
        return;
    }

    setUploading(false);
    if (reply == activeReply_) {
        activeReply_ = nullptr;
    }

    const QByteArray responseBytes = reply->readAll();
    const QJsonDocument responseDocument = QJsonDocument::fromJson(responseBytes);
    const QJsonObject responseObject = responseDocument.isObject() ? responseDocument.object() : QJsonObject();

    if (reply->error() != QNetworkReply::NoError) {
        QString errorMessage = reply->errorString();
        const QString apiMessage = responseObject.value("error").toString();
        if (!apiMessage.isEmpty()) {
            errorMessage = apiMessage;
        }

        setStatusMessage(QString("Upload failed: %1").arg(errorMessage), true);
        reply->deleteLater();
        return;
    }

    const QString title = responseObject.value("title").toString(titleEdit_->text().trimmed());
    const QString status = responseObject.value("status").toString("queued");
    const QString id = QString::number(responseObject.value("id").toVariant().toLongLong());

    selectedFilePath_.clear();
    if (selectedFileLabel_) {
        selectedFileLabel_->setText(defaultFilePrompt());
        selectedFileLabel_->setToolTip(QString());
    }
    if (titleEdit_) {
        titleEdit_->clear();
    }
    if (descriptionEdit_) {
        descriptionEdit_->clear();
    }

    setStatusMessage(
        QString("Upload accepted: video %1 (%2) is now %3. Refreshing the home feed.").arg(id, title, status));
    emit uploadSucceeded();
    reply->deleteLater();
}

void UploadPage::setUploading(bool uploading)
{
    if (selectFileButton_) {
        selectFileButton_->setEnabled(!uploading);
    }
    if (uploadButton_) {
        uploadButton_->setEnabled(!uploading);
        uploadButton_->setText(uploading ? "Uploading..." : "Start upload");
    }
    if (titleEdit_) {
        titleEdit_->setEnabled(!uploading);
    }
    if (descriptionEdit_) {
        descriptionEdit_->setEnabled(!uploading);
    }
    if (progressBar_) {
        progressBar_->setVisible(uploading);
        if (!uploading) {
            progressBar_->setRange(0, 100);
            progressBar_->setValue(0);
        }
    }
}

void UploadPage::setStatusMessage(const QString &message, bool isError)
{
    if (!statusLabel_) {
        return;
    }

    statusLabel_->setStyleSheet(
        QString("font-size: 13px; color: %1;").arg(isError ? "#b91c1c" : "#2563eb"));
    statusLabel_->setText(message);
}

bool UploadPage::validateForm(QString *errorMessage) const
{
    if (selectedFilePath_.trimmed().isEmpty()) {
        if (errorMessage) {
            *errorMessage = "Select a local video file before starting the upload.";
        }
        return false;
    }

    const QFileInfo info(selectedFilePath_);
    if (!info.exists() || !info.isFile()) {
        if (errorMessage) {
            *errorMessage = "The selected file no longer exists. Please choose it again.";
        }
        return false;
    }

    if (activeReply_) {
        if (errorMessage) {
            *errorMessage = "An upload is already in progress.";
        }
        return false;
    }

    if (authToken_.trimmed().isEmpty()) {
        if (errorMessage) {
            *errorMessage = "Sign in before uploading a video.";
        }
        return false;
    }

    return true;
}

}  // namespace frontend::pages
