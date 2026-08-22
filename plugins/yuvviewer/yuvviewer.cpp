// Copyright (C) 2026
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#include "yuvviewer.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <QAction>
#include <QDataStream>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QIcon>
#include <QImage>
#include <QKeySequence>
#include <QLabel>
#include <QPixmap>
#include <QRegularExpression>
#include <QSpinBox>
#include <QToolBar>

#include <expected>
#include <limits>
#include <optional>
#include <stdexcept>

using namespace Qt::StringLiterals;

namespace {
constexpr int minimumDimension = 2;
constexpr int maximumDimension = 32768;

struct Nv12Layout
{
    int width = 0;
    int height = 0;
    int stride = 0;
    int scanline = 0;

    qint64 expectedByteSize() const
    {
        return qint64(stride) * qint64(scanline) * 3 / 2;
    }
};

using LayoutResult = std::expected<Nv12Layout, QString>;
using OptionalLayoutResult = std::expected<std::optional<Nv12Layout>, QString>;
using DataResult = std::expected<QByteArray, QString>;
using ImageResult = std::expected<QImage, QString>;

LayoutResult validateLayout(const Nv12Layout &layout)
{
    if (layout.width < minimumDimension || layout.width > maximumDimension
        || layout.height < minimumDimension || layout.height > maximumDimension) {
        return std::unexpected(YuvViewer::tr("Width and height must be between %1 and %2.")
                                   .arg(minimumDimension)
                                   .arg(maximumDimension));
    }
    if ((layout.width % 2) != 0 || (layout.height % 2) != 0) {
        return std::unexpected(YuvViewer::tr("NV12 width and height must both be even. "
                                             "Received %1x%2.")
                                   .arg(layout.width)
                                   .arg(layout.height));
    }
    if (layout.stride < layout.width || (layout.stride % 2) != 0) {
        return std::unexpected(YuvViewer::tr("NV12 stride must be even and at least the width. "
                                             "Received width %1, stride %2.")
                                   .arg(layout.width)
                                   .arg(layout.stride));
    }
    if (layout.scanline < layout.height || (layout.scanline % 2) != 0) {
        return std::unexpected(YuvViewer::tr("NV12 scanline must be even and at least the height. "
                                             "Received height %1, scanline %2.")
                                   .arg(layout.height)
                                   .arg(layout.scanline));
    }
    if (layout.stride > maximumDimension || layout.scanline > maximumDimension) {
        return std::unexpected(YuvViewer::tr("Stride and scanline must not exceed %1.")
                                   .arg(maximumDimension));
    }

    return layout;
}

std::expected<int, QString> capturedInteger(const QRegularExpressionMatch &match, int index,
                                            const QString &fieldName)
{
    bool ok = false;
    const int value = match.captured(index).toInt(&ok);
    if (!ok) {
        return std::unexpected(YuvViewer::tr("Invalid %1 value \"%2\" in the file name.")
                                   .arg(fieldName, match.captured(index)));
    }
    return value;
}

OptionalLayoutResult layoutFromFileName(const QString &fileName)
{
    const QString baseName = QFileInfo(fileName).completeBaseName();
    static const QRegularExpression taggedPattern(
        QStringLiteral(R"((?:^|_)w\[(\d+)\]_h\[(\d+)\](?:_stride\[(\d+)\])?(?:_scanline\[(\d+)\])?)"),
        QRegularExpression::CaseInsensitiveOption);

    const QRegularExpressionMatch taggedMatch = taggedPattern.match(baseName);
    if (taggedMatch.hasMatch()) {
        const auto width = capturedInteger(taggedMatch, 1, YuvViewer::tr("width"));
        if (!width)
            return std::unexpected(width.error());
        const auto height = capturedInteger(taggedMatch, 2, YuvViewer::tr("height"));
        if (!height)
            return std::unexpected(height.error());

        const auto stride = taggedMatch.captured(3).isEmpty()
            ? std::expected<int, QString>(*width)
            : capturedInteger(taggedMatch, 3, YuvViewer::tr("stride"));
        if (!stride)
            return std::unexpected(stride.error());
        const auto scanline = taggedMatch.captured(4).isEmpty()
            ? std::expected<int, QString>(*height)
            : capturedInteger(taggedMatch, 4, YuvViewer::tr("scanline"));
        if (!scanline)
            return std::unexpected(scanline.error());

        const auto layout = validateLayout({*width, *height, *stride, *scanline});
        if (!layout)
            return std::unexpected(layout.error());
        return std::optional<Nv12Layout>(*layout);
    }

    if (baseName.contains(QStringLiteral("_w["), Qt::CaseInsensitive)
        || baseName.contains(QStringLiteral("_h["), Qt::CaseInsensitive)) {
        return std::unexpected(YuvViewer::tr(
            "The file name contains incomplete NV12 metadata. Expected "
            "\"_w[width]_h[height]_stride[stride]_scanline[scanline]\"."));
    }

    static const QRegularExpression dimensionsPattern(QStringLiteral(R"((\d+)[xX](\d+))"));
    auto matches = dimensionsPattern.globalMatch(baseName);
    QRegularExpressionMatch lastMatch;
    while (matches.hasNext())
        lastMatch = matches.next();

    if (!lastMatch.hasMatch())
        return std::optional<Nv12Layout>();

    const auto width = capturedInteger(lastMatch, 1, YuvViewer::tr("width"));
    if (!width)
        return std::unexpected(width.error());
    const auto height = capturedInteger(lastMatch, 2, YuvViewer::tr("height"));
    if (!height)
        return std::unexpected(height.error());

    const auto layout = validateLayout({*width, *height, *width, *height});
    if (!layout)
        return std::unexpected(layout.error());
    return std::optional<Nv12Layout>(*layout);
}

DataResult readNv12File(const QString &fileName, const Nv12Layout &layout)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        return std::unexpected(YuvViewer::tr("Cannot open the file: %1")
                                   .arg(file.errorString()));
    }

    const qint64 expectedSize = layout.expectedByteSize();
    const qint64 actualSize = file.size();
    if (actualSize != expectedSize) {
        return std::unexpected(
            YuvViewer::tr("File size does not match the NV12 layout. "
                          "Expected %1 bytes, found %2 bytes "
                          "(width=%3, height=%4, stride=%5, scanline=%6).")
                .arg(expectedSize)
                .arg(actualSize)
                .arg(layout.width)
                .arg(layout.height)
                .arg(layout.stride)
                .arg(layout.scanline));
    }

    QByteArray data = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        return std::unexpected(YuvViewer::tr("Failed while reading the file: %1")
                                   .arg(file.errorString()));
    }
    if (data.size() != expectedSize) {
        return std::unexpected(YuvViewer::tr("The file read was incomplete. "
                                             "Expected %1 bytes, received %2 bytes.")
                                   .arg(expectedSize)
                                   .arg(data.size()));
    }

    return data;
}

ImageResult convertNv12ToImage(const QByteArray &data, const Nv12Layout &layout)
{
    try {
        auto *pixels = reinterpret_cast<uchar *>(const_cast<char *>(data.constData()));
        const qint64 yPlaneBytes = qint64(layout.stride) * qint64(layout.scanline);

        cv::Mat yPlane(layout.height, layout.width, CV_8UC1, pixels,
                       static_cast<size_t>(layout.stride));
        cv::Mat uvPlane(layout.height / 2, layout.width / 2, CV_8UC2,
                        pixels + yPlaneBytes, static_cast<size_t>(layout.stride));
        cv::Mat rgba;
        cv::cvtColorTwoPlane(yPlane, uvPlane, rgba, cv::COLOR_YUV2RGBA_NV12);

        if (rgba.empty() || rgba.cols != layout.width || rgba.rows != layout.height) {
            return std::unexpected(YuvViewer::tr(
                "OpenCV returned an empty image or unexpected dimensions."));
        }

        const QImage wrappedImage(rgba.data, rgba.cols, rgba.rows,
                                  static_cast<qsizetype>(rgba.step),
                                  QImage::Format_RGBA8888);
        QImage image = wrappedImage.copy();
        if (image.isNull())
            return std::unexpected(YuvViewer::tr("Could not allocate the converted QImage."));
        return image;
    } catch (const cv::Exception &exception) {
        return std::unexpected(YuvViewer::tr("OpenCV conversion failed: %1")
                                   .arg(QString::fromLocal8Bit(exception.what())));
    } catch (const std::exception &exception) {
        return std::unexpected(YuvViewer::tr("NV12 conversion failed: %1")
                                   .arg(QString::fromLocal8Bit(exception.what())));
    } catch (...) {
        return std::unexpected(YuvViewer::tr(
            "NV12 conversion failed with an unknown exception."));
    }
}
}

YuvViewer::YuvViewer()
    : m_reloadAction(new QAction(this)),
      m_zoomInAction(new QAction(this)),
      m_zoomOutAction(new QAction(this)),
      m_resetZoomAction(new QAction(this))
{
    // OpenCV 4.12's MinGW AVX2 NV12 converter can fault on some Windows
    // systems instead of reporting an exception. Select the portable path
    // before this plugin performs any OpenCV work.
    cv::setUseOptimized(false);

    connect(this, &AbstractViewer::uiInitialized, this, &YuvViewer::setupYuvUi);

    m_reloadAction->setShortcut(QKeySequence(Qt::ControlModifier | Qt::Key_R));
    connect(m_reloadAction, &QAction::triggered, this, &YuvViewer::reload);

    m_zoomInAction->setIcon(QIcon::fromTheme(QIcon::ThemeIcon::ZoomIn));
    m_zoomInAction->setShortcut(QKeySequence::ZoomIn);
    connect(m_zoomInAction, &QAction::triggered, this, &YuvViewer::zoomIn);

    m_zoomOutAction->setIcon(QIcon::fromTheme(QIcon::ThemeIcon::ZoomOut));
    m_zoomOutAction->setShortcut(QKeySequence::ZoomOut);
    connect(m_zoomOutAction, &QAction::triggered, this, &YuvViewer::zoomOut);

    m_resetZoomAction->setIcon(QIcon::fromTheme(QIcon::ThemeIcon::ZoomFitBest));
    m_resetZoomAction->setShortcut(QKeySequence(Qt::ControlModifier | Qt::Key_0));
    connect(m_resetZoomAction, &QAction::triggered, this, &YuvViewer::resetZoom);

    m_zoomInAction->setEnabled(false);
    m_zoomOutAction->setEnabled(false);
    m_resetZoomAction->setEnabled(false);
}

YuvViewer::~YuvViewer() = default;

void YuvViewer::init(QFile *file, QWidget *parent, QMainWindow *mainWindow)
{
    m_imageLabel = new QLabel(parent);
    m_imageLabel->setFrameShape(QFrame::Box);
    m_imageLabel->setAlignment(Qt::AlignCenter);
    m_imageLabel->setScaledContents(true);

    AbstractViewer::init(file, m_imageLabel, mainWindow);

    QToolBar *toolBar = addToolBar();
    m_widthLabel = new QLabel(toolBar);
    m_widthSpinBox = new QSpinBox(toolBar);
    m_widthSpinBox->setRange(minimumDimension, maximumDimension);
    m_widthSpinBox->setSingleStep(2);
    m_widthSpinBox->setValue(1920);
    m_widthSpinBox->setKeyboardTracking(false);

    m_heightLabel = new QLabel(toolBar);
    m_heightSpinBox = new QSpinBox(toolBar);
    m_heightSpinBox->setRange(minimumDimension, maximumDimension);
    m_heightSpinBox->setSingleStep(2);
    m_heightSpinBox->setValue(1080);
    m_heightSpinBox->setKeyboardTracking(false);

    m_formatLabel = new QLabel(toolBar);

    toolBar->addWidget(m_widthLabel);
    toolBar->addWidget(m_widthSpinBox);
    toolBar->addWidget(m_heightLabel);
    toolBar->addWidget(m_heightSpinBox);
    toolBar->addWidget(m_formatLabel);
    toolBar->addAction(m_reloadAction);
    toolBar->addSeparator();
    toolBar->addAction(m_zoomInAction);
    toolBar->addAction(m_zoomOutAction);
    toolBar->addAction(m_resetZoomAction);

    m_hasFileLayout = false;
    m_fileWidth = m_fileHeight = m_fileStride = m_fileScanline = 0;
    m_metadataError.clear();

    const auto parsedLayout = layoutFromFileName(file->fileName());
    if (!parsedLayout) {
        m_metadataError = parsedLayout.error();
    } else if (parsedLayout->has_value()) {
        const Nv12Layout &layout = parsedLayout->value();
        m_hasFileLayout = true;
        m_fileWidth = layout.width;
        m_fileHeight = layout.height;
        m_fileStride = layout.stride;
        m_fileScanline = layout.scanline;
        m_widthSpinBox->setValue(layout.width);
        m_heightSpinBox->setValue(layout.height);
    }

    clear();
    retranslate();
}

QStringList YuvViewer::supportedMimeTypes() const
{
    return {"video/x-raw-nv12"_L1};
}

QStringList YuvViewer::supportedExtensions() const
{
    // ViewerFactory currently compares suffixes case-sensitively.
    return {"nv12"_L1, "NV12"_L1, "yuv"_L1, "YUV"_L1};
}

bool YuvViewer::hasContent() const
{
    return m_imageLabel && !m_imageLabel->pixmap().isNull();
}

QByteArray YuvViewer::saveState() const
{
    if (!m_widthSpinBox || !m_heightSpinBox)
        return {};

    QByteArray state;
    QDataStream stream(&state, QIODevice::WriteOnly);
    stream << QString(viewerName());
    stream << m_widthSpinBox->value();
    stream << m_heightSpinBox->value();
    return state;
}

bool YuvViewer::restoreState(QByteArray &state)
{
    QDataStream stream(&state, QIODevice::ReadOnly);
    QString viewer;
    int width = 0;
    int height = 0;
    stream >> viewer >> width >> height;

    if (stream.status() != QDataStream::Ok || viewer != viewerName())
        return false;
    if (width < minimumDimension || width > maximumDimension || (width % 2) != 0
        || height < minimumDimension || height > maximumDimension || (height % 2) != 0) {
        return false;
    }

    if (!m_hasFileLayout && m_metadataError.isEmpty()
        && m_widthSpinBox && m_heightSpinBox) {
        m_widthSpinBox->setValue(width);
        m_heightSpinBox->setValue(height);
        reload();
    }
    return true;
}

void YuvViewer::setupYuvUi()
{
    if (m_hasFileLayout) {
        reload();
        return;
    }

    const QString prompt = tr("Enter the image width and height, then select Reload.");
    if (!m_metadataError.isEmpty()) {
        reportError(m_metadataError + QStringLiteral("\n") + prompt);
        return;
    }

    m_imageLabel->setText(prompt);
    m_imageLabel->setWordWrap(true);
    statusMessage(prompt, tr("open"));
}

void YuvViewer::reload()
{
    clear();

    if (!m_file || !m_widthSpinBox || !m_heightSpinBox) {
        reportError(tr("The YUV viewer is not fully initialized."));
        return;
    }

    const int width = m_widthSpinBox->value();
    const int height = m_heightSpinBox->value();
    Nv12Layout requestedLayout{width, height, width, height};
    if (m_hasFileLayout && width == m_fileWidth && height == m_fileHeight) {
        requestedLayout.stride = m_fileStride;
        requestedLayout.scanline = m_fileScanline;
    }

    const auto layout = validateLayout(requestedLayout);
    if (!layout) {
        reportError(layout.error());
        return;
    }

    const auto data = readNv12File(m_file->fileName(), *layout);
    if (!data) {
        reportError(data.error());
        return;
    }

    const auto image = convertNv12ToImage(*data, *layout);
    if (!image) {
        reportError(image.error());
        return;
    }

    displayImage(*image);
    statusMessage(tr("Opened \"%1\", %2x%3, NV12 (stride=%4, scanline=%5).")
                      .arg(QDir::toNativeSeparators(m_file->fileName()))
                      .arg(layout->width)
                      .arg(layout->height)
                      .arg(layout->stride)
                      .arg(layout->scanline));
}

void YuvViewer::clear()
{
    if (m_imageLabel) {
        m_imageLabel->setPixmap({});
        m_imageLabel->clear();
        m_imageLabel->setWordWrap(false);
        m_imageLabel->setMinimumSize(0, 0);
        m_imageLabel->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    }

    m_imageSize = {};
    m_maxScaleFactor = m_minScaleFactor = m_initialScaleFactor = m_scaleFactor = 1;
    m_zoomInAction->setEnabled(false);
    m_zoomOutAction->setEnabled(false);
    m_resetZoomAction->setEnabled(false);
    disablePrinting();
}

void YuvViewer::displayImage(const QImage &image)
{
    m_imageLabel->clear();
    m_imageLabel->setWordWrap(false);

    const qreal devicePixelRatio = m_imageLabel->devicePixelRatioF();
    m_imageSize = QSizeF(image.size()) / devicePixelRatio;

    QPixmap pixmap = QPixmap::fromImage(image);
    pixmap.setDevicePixelRatio(devicePixelRatio);
    m_imageLabel->setPixmap(pixmap);

    const QWidget *parent = m_imageLabel->parentWidget();
    const QSizeF targetSize = parent ? QSizeF(parent->size()) : m_imageSize;
    if (targetSize.width() > 0 && targetSize.height() > 0
        && (m_imageSize.width() > targetSize.width()
            || m_imageSize.height() > targetSize.height())) {
        m_initialScaleFactor = qMin(targetSize.width() / m_imageSize.width(),
                                    targetSize.height() / m_imageSize.height());
    }

    m_maxScaleFactor = 3 * m_initialScaleFactor;
    m_minScaleFactor = m_initialScaleFactor / 3;
    doSetScaleFactor(m_initialScaleFactor);
}

void YuvViewer::reportError(const QString &message)
{
    QString detailedMessage = message;
    if (m_file) {
        detailedMessage += tr("\nFile: %1")
                               .arg(QDir::toNativeSeparators(m_file->fileName()));
    }

    qWarning().noquote() << viewerName() + "/open:"_L1 << detailedMessage;
    if (m_imageLabel) {
        m_imageLabel->setText(detailedMessage);
        m_imageLabel->setWordWrap(true);
        m_imageLabel->setAlignment(Qt::AlignCenter);
    }
    statusMessage(detailedMessage, tr("open"), 15000);
}

void YuvViewer::setScaleFactor(qreal scaleFactor)
{
    if (!qFuzzyCompare(m_scaleFactor, scaleFactor))
        doSetScaleFactor(scaleFactor);
}

void YuvViewer::doSetScaleFactor(qreal scaleFactor)
{
    m_scaleFactor = scaleFactor;
    m_imageLabel->setFixedSize((m_imageSize * m_scaleFactor).toSize());
    enableZoomActions();
}

void YuvViewer::enableZoomActions()
{
    m_resetZoomAction->setEnabled(!qFuzzyCompare(m_scaleFactor, m_initialScaleFactor));
    m_zoomInAction->setEnabled(m_scaleFactor < m_maxScaleFactor);
    m_zoomOutAction->setEnabled(m_scaleFactor > m_minScaleFactor);
}

void YuvViewer::zoomIn()
{
    setScaleFactor(m_scaleFactor * 1.25);
}

void YuvViewer::zoomOut()
{
    setScaleFactor(m_scaleFactor * 0.8);
}

void YuvViewer::resetZoom()
{
    setScaleFactor(m_initialScaleFactor);
}

void YuvViewer::retranslate()
{
    if (toolBars().isEmpty())
        return;

    toolBars().constFirst()->setWindowTitle(tr("YUV Image"));
    m_widthLabel->setText(tr("Width:"));
    m_heightLabel->setText(tr("Height:"));
    m_formatLabel->setText(tr("Format: NV12"));
    m_widthSpinBox->setSuffix(tr(" px"));
    m_heightSpinBox->setSuffix(tr(" px"));
    m_reloadAction->setText(tr("&Reload"));
    m_zoomInAction->setText(tr("Zoom &In"));
    m_zoomOutAction->setText(tr("Zoom &Out"));
    m_resetZoomAction->setText(tr("Reset Zoom"));
}
