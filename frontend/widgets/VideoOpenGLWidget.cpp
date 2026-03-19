#include "widgets/VideoOpenGLWidget.hpp"

#include <QOpenGLShader>

namespace
{

constexpr const char *kVertexShaderSource = R"(
attribute vec2 position;
attribute vec2 texCoord;
varying vec2 vTexCoord;

void main()
{
    vTexCoord = texCoord;
    gl_Position = vec4(position, 0.0, 1.0);
}
)";

constexpr const char *kFragmentShaderSource = R"(
uniform sampler2D yTexture;
uniform sampler2D uTexture;
uniform sampler2D vTexture;
varying vec2 vTexCoord;

void main()
{
    float y = texture2D(yTexture, vTexCoord).r;
    float u = texture2D(uTexture, vTexCoord).r - 0.5;
    float v = texture2D(vTexture, vTexCoord).r - 0.5;

    y = 1.16438356 * (y - 0.0625);

    vec3 rgb;
    rgb.r = y + 1.59602678 * v;
    rgb.g = y - 0.39176229 * u - 0.81296764 * v;
    rgb.b = y + 2.01723214 * u;

    gl_FragColor = vec4(rgb, 1.0);
}
)";

std::unique_ptr<QOpenGLTexture> createPlaneTexture(int width, int height)
{
    if (width <= 0 || height <= 0)
    {
        return nullptr;
    }

    auto texture = std::make_unique<QOpenGLTexture>(QOpenGLTexture::Target2D);
    texture->create();
    texture->setFormat(QOpenGLTexture::R8_UNorm);
    texture->setSize(width, height);
    texture->allocateStorage(QOpenGLTexture::Red, QOpenGLTexture::UInt8);
    texture->setMinificationFilter(QOpenGLTexture::Linear);
    texture->setMagnificationFilter(QOpenGLTexture::Linear);
    texture->setWrapMode(QOpenGLTexture::ClampToEdge);
    return texture;
}

} // namespace

VideoOpenGLWidget::VideoOpenGLWidget(QWidget *parent)
    : QOpenGLWidget(parent)
{
    setAutoFillBackground(false);
}

VideoOpenGLWidget::~VideoOpenGLWidget()
{
    if (context())
    {
        makeCurrent();
        releaseTextures();
        doneCurrent();
    }
}

void VideoOpenGLWidget::presentFrame(
    int frameWidth,
    int frameHeight,
    const QByteArray &planeY,
    const QByteArray &planeU,
    const QByteArray &planeV)
{
    if (frameWidth <= 0 || frameHeight <= 0)
    {
        clearFrame();
        return;
    }

    frameWidth_ = frameWidth;
    frameHeight_ = frameHeight;
    planeY_ = planeY;
    planeU_ = planeU;
    planeV_ = planeV;
    frameDirty_ = true;
    update();
}

void VideoOpenGLWidget::clearFrame()
{
    frameWidth_ = 0;
    frameHeight_ = 0;
    planeY_.clear();
    planeU_.clear();
    planeV_.clear();
    frameDirty_ = false;

    if (context())
    {
        makeCurrent();
        releaseTextures();
        doneCurrent();
    }

    update();
}

void VideoOpenGLWidget::initializeGL()
{
    initializeOpenGLFunctions();
    glClearColor(0.05f, 0.09f, 0.16f, 1.0f);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    program_.addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShaderSource);
    program_.addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShaderSource);
    program_.link();
}

void VideoOpenGLWidget::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
}

void VideoOpenGLWidget::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT);

    if (frameWidth_ <= 0 || frameHeight_ <= 0 || !program_.isLinked())
    {
        return;
    }

    ensureTextures();
    if (!yTexture_ || !uTexture_ || !vTexture_)
    {
        return;
    }

    if (frameDirty_)
    {
        uploadTexture(yTexture_.get(), planeY_);
        uploadTexture(uTexture_.get(), planeU_);
        uploadTexture(vTexture_.get(), planeV_);
        frameDirty_ = false;
    }

    const float widgetAspect = height() > 0 ? static_cast<float>(width()) / static_cast<float>(height()) : 1.0f;
    const float frameAspect = frameHeight_ > 0
                                  ? static_cast<float>(frameWidth_) / static_cast<float>(frameHeight_)
                                  : 1.0f;

    float scaleX = 1.0f;
    float scaleY = 1.0f;
    if (frameAspect > widgetAspect)
    {
        scaleY = widgetAspect / frameAspect;
    }
    else
    {
        scaleX = frameAspect / widgetAspect;
    }

    const GLfloat vertices[] = {
        -scaleX, -scaleY,
         scaleX, -scaleY,
        -scaleX,  scaleY,
         scaleX,  scaleY,
    };

    const GLfloat texCoords[] = {
        0.0f, 1.0f,
        1.0f, 1.0f,
        0.0f, 0.0f,
        1.0f, 0.0f,
    };

    program_.bind();

    yTexture_->bind(0);
    uTexture_->bind(1);
    vTexture_->bind(2);

    program_.setUniformValue("yTexture", 0);
    program_.setUniformValue("uTexture", 1);
    program_.setUniformValue("vTexture", 2);
    program_.enableAttributeArray("position");
    program_.enableAttributeArray("texCoord");
    program_.setAttributeArray("position", GL_FLOAT, vertices, 2);
    program_.setAttributeArray("texCoord", GL_FLOAT, texCoords, 2);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    program_.disableAttributeArray("position");
    program_.disableAttributeArray("texCoord");
    vTexture_->release();
    uTexture_->release();
    yTexture_->release();
    program_.release();
}

void VideoOpenGLWidget::ensureTextures()
{
    if (frameWidth_ <= 0 || frameHeight_ <= 0)
    {
        releaseTextures();
        return;
    }

    const int chromaWidth = (frameWidth_ + 1) / 2;
    const int chromaHeight = (frameHeight_ + 1) / 2;

    if (yTexture_ &&
        uTexture_ &&
        vTexture_ &&
        yTexture_->width() == frameWidth_ &&
        yTexture_->height() == frameHeight_ &&
        uTexture_->width() == chromaWidth &&
        uTexture_->height() == chromaHeight &&
        vTexture_->width() == chromaWidth &&
        vTexture_->height() == chromaHeight)
    {
        return;
    }

    releaseTextures();
    yTexture_ = createPlaneTexture(frameWidth_, frameHeight_);
    uTexture_ = createPlaneTexture(chromaWidth, chromaHeight);
    vTexture_ = createPlaneTexture(chromaWidth, chromaHeight);
    frameDirty_ = true;
}

void VideoOpenGLWidget::releaseTextures()
{
    yTexture_.reset();
    uTexture_.reset();
    vTexture_.reset();
}

void VideoOpenGLWidget::uploadTexture(QOpenGLTexture *texture, const QByteArray &planeData)
{
    if (!texture || planeData.isEmpty())
    {
        return;
    }

    const int expectedBytes = texture->width() * texture->height();
    if (planeData.size() < expectedBytes)
    {
        return;
    }

    texture->setData(QOpenGLTexture::Red, QOpenGLTexture::UInt8, planeData.constData());
}
