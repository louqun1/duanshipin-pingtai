#pragma once

#include <QString>
#include <QWidget>

class QLabel;
class QLineEdit;
class QNetworkAccessManager;
class QNetworkReply;
class QProgressBar;
class QPushButton;
class QTextEdit;

namespace frontend::pages {

class UploadPage final : public QWidget
{
    Q_OBJECT

public:
    explicit UploadPage(QWidget *parent = nullptr);

signals:
    void uploadSucceeded();

private:
    void buildUi();
    void pickVideoFile();
    void submitUpload();
    void handleUploadFinished(QNetworkReply *reply);
    void setUploading(bool uploading);
    void setStatusMessage(const QString &message, bool isError = false);
    bool validateForm(QString *errorMessage = nullptr) const;

    QString selectedFilePath_;
    QLabel *selectedFileLabel_ = nullptr;
    QLineEdit *titleEdit_ = nullptr;
    QTextEdit *descriptionEdit_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QProgressBar *progressBar_ = nullptr;
    QPushButton *selectFileButton_ = nullptr;
    QPushButton *uploadButton_ = nullptr;
    QNetworkAccessManager *networkManager_ = nullptr;
    QNetworkReply *activeReply_ = nullptr;
};

}  // namespace frontend::pages
