// Copyright (C) 2026
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#include "yuvviewer.h"

#include "rawimagedecoder.h"

#include <opencv2/core.hpp>

#include <QAction>
#include <QComboBox>
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
constexpr int minimumDimension = RawImageDecoder::minimumDimension;
constexpr int maximumDimension = RawImageDecoder::maximumDimension;

using OptionalLayoutResult = std::expected<std::optional<RawImageLayout>, QString>;

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

OptionalLayoutResult layoutFromFileName(const QString &fileName, const RawImageDecoder &decoder)
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

        const auto layout = decoder.validateLayout({*width, *height, *stride, *scanline});
        if (!layout)
            return std::unexpected(layout.error());
        return std::optional<RawImageLayout>(*layout);
    }

    if (baseName.contains(QStringLiteral("_w["), Qt::CaseInsensitive)
        || baseName.contains(QStringLiteral("_h["), Qt::CaseInsensitive)) {
        return std::unexpected(YuvViewer::tr(
            "The file name contains incomplete image metadata. Expected "
            "\"_w[width]_h[height]_stride[stride]_scanline[scanline]\"."));
    }

    static const QRegularExpression dimensionsPattern(QStringLiteral(R"((\d+)[xX](\d+))"));
    auto matches = dimensionsPattern.globalMatch(baseName);
    QRegularExpressionMatch lastMatch;
    while (matches.hasNext())
        lastMatch = matches.next();

    if (!lastMatch.hasMatch())
        return std::optional<RawImageLayout>();

    const auto width = capturedInteger(lastMatch, 1, YuvViewer::tr("width"));
    if (!width)
        return std::unexpected(width.error());
    const auto height = capturedInteger(lastMatch, 2, YuvViewer::tr("height"));
    if (!height)
        return std::unexpected(height.error());

    const auto layout = decoder.validateLayout({*width, *height, *width, *height});
    if (!layout)
        return std::unexpected(layout.error());
    return std::optional<RawImageLayout>(*layout);
}
}

YuvViewer::YuvViewer()
    : m_reloadAction(new QAction(this)),
      m_zoomInAction(new QAction(this)),
      m_zoomOutAction(new QAction(this)),
      m_resetZoomAction(new QAction(this))
{
    // OpenCV 4.12's MinGW AVX2 semi-planar YUV converters can fault on some
    // Windows systems instead of reporting an exception. Select the portable
    // path before this plugin performs any OpenCV work.
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
    m_formatComboBox = new QComboBox(toolBar);
    for (const RawImageDecoder *decoder : RawImageDecoders::all())
        m_formatComboBox->addItem(decoder->displayName());

    m_decoder = RawImageDecoders::findByExtension(QFileInfo(file->fileName()).suffix());
    if (!m_decoder)
        m_decoder = RawImageDecoders::defaultDecoder();
    m_formatComboBox->setCurrentIndex(RawImageDecoders::all().indexOf(m_decoder));
    connect(m_formatComboBox, &QComboBox::activated, this, &YuvViewer::onFormatChanged);

    toolBar->addWidget(m_widthLabel);
    toolBar->addWidget(m_widthSpinBox);
    toolBar->addWidget(m_heightLabel);
    toolBar->addWidget(m_heightSpinBox);
    toolBar->addWidget(m_formatLabel);
    toolBar->addWidget(m_formatComboBox);
    toolBar->addAction(m_reloadAction);
    toolBar->addSeparator();
    toolBar->addAction(m_zoomInAction);
    toolBar->addAction(m_zoomOutAction);
    toolBar->addAction(m_resetZoomAction);

    m_hasFileLayout = false;
    m_fileWidth = m_fileHeight = m_fileStride = m_fileScanline = 0;
    m_metadataError.clear();

    const auto parsedLayout = layoutFromFileName(file->fileName(), *m_decoder);
    if (!parsedLayout) {
        m_metadataError = parsedLayout.error();
    } else if (parsedLayout->has_value()) {
        const RawImageLayout &layout = parsedLayout->value();
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
    QStringList mimeTypes;
    for (const RawImageDecoder *decoder : RawImageDecoders::all())
        mimeTypes << decoder->mimeType();
    return mimeTypes;
}

QStringList YuvViewer::supportedExtensions() const
{
    // ViewerFactory currently compares suffixes case-sensitively.
    QStringList extensions;
    for (const RawImageDecoder *decoder : RawImageDecoders::all())
        extensions << decoder->fileExtensions();
    extensions << "yuv"_L1 << "YUV"_L1;
    return extensions;
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
    stream << (m_decoder ? QString(m_decoder->id()) : QString());
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
    if (width < minimumDimension || width > maximumDimension
        || height < minimumDimension || height > maximumDimension) {
        return false;
    }

    // States saved before multi-format support end after the height field.
    QString formatId;
    if (!stream.atEnd())
        stream >> formatId;
    if (!formatId.isEmpty()) {
        if (const RawImageDecoder *decoder = RawImageDecoders::findById(formatId)) {
            m_decoder = decoder;
            if (m_formatComboBox)
                m_formatComboBox->setCurrentIndex(RawImageDecoders::all().indexOf(decoder));
        }
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

    if (!m_file || !m_widthSpinBox || !m_heightSpinBox || !m_decoder) {
        reportError(tr("The YUV viewer is not fully initialized."));
        return;
    }

    const int width = m_widthSpinBox->value();
    const int height = m_heightSpinBox->value();
    RawImageLayout requestedLayout{width, height, width, height};
    if (m_hasFileLayout && width == m_fileWidth && height == m_fileHeight) {
        requestedLayout.stride = m_fileStride;
        requestedLayout.scanline = m_fileScanline;
    }

    const auto layout = m_decoder->validateLayout(requestedLayout);
    if (!layout) {
        reportError(layout.error());
        return;
    }

    const auto data = m_decoder->readData(m_file->fileName(), *layout);
    if (!data) {
        reportError(data.error());
        return;
    }

    const auto image = m_decoder->convertToImage(*data, *layout);
    if (!image) {
        reportError(image.error());
        return;
    }

    displayImage(*image);
    statusMessage(tr("Opened \"%1\", %2x%3, %4 (stride=%5, scanline=%6).")
                      .arg(QDir::toNativeSeparators(m_file->fileName()))
                      .arg(layout->width)
                      .arg(layout->height)
                      .arg(m_decoder->displayName())
                      .arg(layout->stride)
                      .arg(layout->scanline));
}

void YuvViewer::onFormatChanged()
{
    const int index = m_formatComboBox ? m_formatComboBox->currentIndex() : -1;
    if (index >= 0)
        m_decoder = RawImageDecoders::all().at(index);

    // Without a known layout the viewer still waits for width/height input.
    if (m_hasFileLayout || hasContent())
        reload();
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
    m_formatLabel->setText(tr("Format:"));
    m_widthSpinBox->setSuffix(tr(" px"));
    m_heightSpinBox->setSuffix(tr(" px"));
    m_reloadAction->setText(tr("&Reload"));
    m_zoomInAction->setText(tr("Zoom &In"));
    m_zoomOutAction->setText(tr("Zoom &Out"));
    m_resetZoomAction->setText(tr("Reset Zoom"));
}
