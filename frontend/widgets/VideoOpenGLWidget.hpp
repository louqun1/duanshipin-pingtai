#pragma once

#include <QByteArray>
#include <QMatrix3x3>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QOpenGLWidget>
#include <QVector3D>

#include <memory>

class VideoOpenGLWidget final : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT

public:
    enum class ColorMatrix
    {
        Bt601 = 0,
        Bt709 = 1
    };

    explicit VideoOpenGLWidget(QWidget *parent = nullptr);
    ~VideoOpenGLWidget() override;

    Q_INVOKABLE void presentFrame(
        int frameWidth,
        int frameHeight,
        const QByteArray &planeY,
        const QByteArray &planeU,
        const QByteArray &planeV,
        int colorMatrix,
        bool fullRange);
    Q_INVOKABLE void clearFrame();

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

private:
    void ensureTextures();
    void releaseTextures();
    void uploadTexture(QOpenGLTexture *texture, const QByteArray &planeData);
    QMatrix3x3 colorConversionMatrix() const;
    QVector3D colorOffset() const;
    QVector3D colorScale() const;

    QOpenGLShaderProgram program_;
    std::unique_ptr<QOpenGLTexture> yTexture_;
    std::unique_ptr<QOpenGLTexture> uTexture_;
    std::unique_ptr<QOpenGLTexture> vTexture_;
    QByteArray planeY_;
    QByteArray planeU_;
    QByteArray planeV_;
    int frameWidth_ = 0;
    int frameHeight_ = 0;
    ColorMatrix colorMatrix_ = ColorMatrix::Bt601;
    bool fullRange_ = false;
    bool frameDirty_ = false;
};
