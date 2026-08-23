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
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QIcon>
#include <QImage>
#include <QKeySequence>
#include <QLabel>
#include <QLocale>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStatusBar>
#include <QTableWidget>
#include <QTabWidget>
#include <QToolBar>

#include <climits>
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
            ? std::expected<int, QString>(decoder.defaultStride(*width))
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

    const auto layout = decoder.validateLayout(
        {*width, *height, decoder.defaultStride(*width), *height});
    if (!layout)
        return std::unexpected(layout.error());
    return std::optional<RawImageLayout>(*layout);
}

// Number of whole frames of the given layout in a file of fileSize bytes;
// 0 if the file size is not a positive multiple of the frame size.
qint64 frameCount(const RawImageDecoder &decoder, const RawImageLayout &layout,
                  qint64 fileSize)
{
    const qint64 frameSize = decoder.expectedByteSize(layout);
    if (frameSize <= 0 || fileSize < frameSize || (fileSize % frameSize) != 0)
        return 0;
    return fileSize / frameSize;
}

// Renders the composite image (plane < 0) or a single component plane.
RawImageDecoder::ImageResult renderImage(const RawImageDecoder *decoder,
                                         const QByteArray &data,
                                         const RawImageLayout &layout, int plane)
{
    if (plane < 0)
        return decoder->convertToImage(data, layout);
    return decoder->extractPlane(data, layout, plane);
}

// One loaded frame: the raw samples plus the rendered view of them.
struct LoadedFrame
{
    QByteArray data;
    QImage image;
};
using LoadResult = std::expected<LoadedFrame, QString>;
}

// Image display widget for the YUV viewer. Unlike a QLabel with
// scaledContents, painting is fully controlled here: nearest-neighbor
// vs. smooth scaling, a pixel grid at high zoom levels, and text
// prompts when no image is loaded. The widget reports the scaled image
// size as its size hint so the enclosing scroll area adds scroll bars.
class YuvImageWidget : public QFrame
{
public:
    explicit YuvImageWidget(QWidget *parent = nullptr) : QFrame(parent) {}

    const QImage &image() const { return m_image; }
    QRectF imageRect() const { return m_imageRect; }

    void setImage(const QImage &image)
    {
        m_image = image;
        updateGeometry();
        update();
    }

    void setText(const QString &text)
    {
        m_text = text;
        update();
    }

    void setScaleFactor(qreal scaleFactor)
    {
        if (qFuzzyCompare(m_scaleFactor, scaleFactor))
            return;
        m_scaleFactor = scaleFactor;
        updateGeometry();
        update();
    }

    void setSmoothScaling(bool smooth)
    {
        m_smoothScaling = smooth;
        update();
    }

    void setPixelGrid(bool grid)
    {
        m_pixelGrid = grid;
        update();
    }

    QSize sizeHint() const override
    {
        if (m_image.isNull())
            return QFrame::sizeHint();
        const QSizeF logical = QSizeF(m_image.size()) / devicePixelRatioF();
        return (logical * m_scaleFactor).toSize() + QSize(2 * frameWidth(), 2 * frameWidth());
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QFrame::paintEvent(event);
        QPainter painter(this);
        const QRectF area = contentsRect();

        if (m_image.isNull()) {
            if (!m_text.isEmpty())
                painter.drawText(area, Qt::AlignCenter | Qt::TextWordWrap, m_text);
            return;
        }

        const qreal dpr = devicePixelRatioF();
        const QSizeF targetSize = QSizeF(m_image.size()) / dpr * m_scaleFactor;
        // Center within the viewport while the image is smaller than it;
        // once larger, the widget itself is resized to the target size.
        const QPointF topLeft(area.x() + qMax<qreal>(0, (area.width() - targetSize.width()) / 2),
                              area.y() + qMax<qreal>(0, (area.height() - targetSize.height()) / 2));
        m_imageRect = QRectF(topLeft, targetSize);

        painter.setRenderHint(QPainter::SmoothPixmapTransform, m_smoothScaling);
        painter.drawImage(m_imageRect, m_image);

        const qreal step = m_scaleFactor / dpr;  // widget pixels per image pixel
        if (m_pixelGrid && step >= 4.0) {
            QPen pen(QColor(128, 128, 128, 90));
            pen.setCosmetic(true);
            painter.setPen(pen);
            const QRectF clip = event->rect();
            const int firstCol = qMax(0, qFloor((clip.left() - topLeft.x()) / step));
            const int lastCol = qMin(m_image.width(), qCeil((clip.right() - topLeft.x()) / step));
            for (int col = firstCol; col <= lastCol; ++col) {
                const qreal x = topLeft.x() + col * step;
                painter.drawLine(QPointF(x, topLeft.y()),
                                 QPointF(x, topLeft.y() + targetSize.height()));
            }
            const int firstRow = qMax(0, qFloor((clip.top() - topLeft.y()) / step));
            const int lastRow = qMin(m_image.height(), qCeil((clip.bottom() - topLeft.y()) / step));
            for (int row = firstRow; row <= lastRow; ++row) {
                const qreal y = topLeft.y() + row * step;
                painter.drawLine(QPointF(topLeft.x(), y),
                                 QPointF(topLeft.x() + targetSize.width(), y));
            }
        }
    }

private:
    QImage m_image;
    QString m_text;
    QRectF m_imageRect;
    qreal m_scaleFactor = 1;
    bool m_smoothScaling = false;
    bool m_pixelGrid = true;
};

YuvViewer::YuvViewer()
    : m_reloadAction(new QAction(this)),
      m_prevFrameAction(new QAction(this)),
      m_nextFrameAction(new QAction(this)),
      m_zoomInAction(new QAction(this)),
      m_zoomOutAction(new QAction(this)),
      m_resetZoomAction(new QAction(this)),
      m_fitToWindowAction(new QAction(this)),
      m_smoothScalingAction(new QAction(this)),
      m_pixelGridAction(new QAction(this))
{
    // OpenCV 4.12's MinGW AVX2 semi-planar YUV converters can fault on some
    // Windows systems instead of reporting an exception. Select the portable
    // path before this plugin performs any OpenCV work.
    cv::setUseOptimized(false);

    setTranslationBaseName("yuvviewer"_L1);

    connect(this, &AbstractViewer::uiInitialized, this, &YuvViewer::setupYuvUi);

    m_reloadAction->setShortcut(QKeySequence(Qt::ControlModifier | Qt::Key_R));
    connect(m_reloadAction, &QAction::triggered, this, &YuvViewer::reload);

    m_prevFrameAction->setIcon(QIcon::fromTheme(QIcon::ThemeIcon::GoPrevious));
    m_prevFrameAction->setShortcut(QKeySequence(Qt::Key_PageUp));
    connect(m_prevFrameAction, &QAction::triggered, this, [this] {
        if (m_frameSpinBox)
            m_frameSpinBox->stepDown();
    });

    m_nextFrameAction->setIcon(QIcon::fromTheme(QIcon::ThemeIcon::GoNext));
    m_nextFrameAction->setShortcut(QKeySequence(Qt::Key_PageDown));
    connect(m_nextFrameAction, &QAction::triggered, this, [this] {
        if (m_frameSpinBox)
            m_frameSpinBox->stepUp();
    });

    m_zoomInAction->setIcon(QIcon::fromTheme(QIcon::ThemeIcon::ZoomIn));
    m_zoomInAction->setShortcut(QKeySequence::ZoomIn);
    connect(m_zoomInAction, &QAction::triggered, this, &YuvViewer::zoomIn);

    m_zoomOutAction->setIcon(QIcon::fromTheme(QIcon::ThemeIcon::ZoomOut));
    m_zoomOutAction->setShortcut(QKeySequence::ZoomOut);
    connect(m_zoomOutAction, &QAction::triggered, this, &YuvViewer::zoomOut);

    m_resetZoomAction->setIcon(QIcon::fromTheme(QIcon::ThemeIcon::ZoomFitBest));
    m_resetZoomAction->setShortcut(QKeySequence(Qt::ControlModifier | Qt::Key_0));
    connect(m_resetZoomAction, &QAction::triggered, this, &YuvViewer::resetZoom);

    m_fitToWindowAction->setShortcut(QKeySequence(Qt::ControlModifier | Qt::Key_9));
    connect(m_fitToWindowAction, &QAction::triggered, this, &YuvViewer::fitToWindow);

    // Nearest-neighbor is the default: raw image inspection cares about
    // exact pixel values, not pretty interpolation.
    m_smoothScalingAction->setCheckable(true);
    connect(m_smoothScalingAction, &QAction::toggled, this, [this](bool checked) {
        if (m_imageWidget)
            m_imageWidget->setSmoothScaling(checked);
    });

    m_pixelGridAction->setCheckable(true);
    m_pixelGridAction->setChecked(true);
    connect(m_pixelGridAction, &QAction::toggled, this, [this](bool checked) {
        if (m_imageWidget)
            m_imageWidget->setPixelGrid(checked);
    });

    m_prevFrameAction->setEnabled(false);
    m_nextFrameAction->setEnabled(false);
    m_zoomInAction->setEnabled(false);
    m_zoomOutAction->setEnabled(false);
    m_resetZoomAction->setEnabled(false);
    m_fitToWindowAction->setEnabled(false);
}

YuvViewer::~YuvViewer() = default;

void YuvViewer::init(QFile *file, QWidget *parent, QMainWindow *mainWindow)
{
    m_imageWidget = new YuvImageWidget(parent);
    m_imageWidget->setFrameShape(QFrame::Box);
    m_imageWidget->setMouseTracking(true);
    m_imageWidget->installEventFilter(this);

    AbstractViewer::init(file, m_imageWidget, mainWindow);

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

    m_frameLabel = new QLabel(toolBar);
    m_frameSpinBox = new QSpinBox(toolBar);
    m_frameSpinBox->setRange(1, 1);
    m_frameSpinBox->setKeyboardTracking(false);
    m_frameSpinBox->setEnabled(false);
    connect(m_frameSpinBox, &QSpinBox::valueChanged, this, &YuvViewer::reload);
    m_frameCountLabel = new QLabel(toolBar);

    connect(m_widthSpinBox, &QSpinBox::valueChanged, this, &YuvViewer::updateFormatMatches);
    connect(m_heightSpinBox, &QSpinBox::valueChanged, this, &YuvViewer::updateFormatMatches);

    m_planeLabel = new QLabel(toolBar);
    m_planeComboBox = new QComboBox(toolBar);
    updatePlaneCombo();
    connect(m_planeComboBox, &QComboBox::activated, this, &YuvViewer::onPlaneChanged);

    toolBar->addWidget(m_widthLabel);
    toolBar->addWidget(m_widthSpinBox);
    toolBar->addWidget(m_heightLabel);
    toolBar->addWidget(m_heightSpinBox);
    toolBar->addWidget(m_formatLabel);
    toolBar->addWidget(m_formatComboBox);
    toolBar->addWidget(m_planeLabel);
    toolBar->addWidget(m_planeComboBox);
    toolBar->addWidget(m_frameLabel);
    toolBar->addWidget(m_frameSpinBox);
    toolBar->addWidget(m_frameCountLabel);
    toolBar->addAction(m_prevFrameAction);
    toolBar->addAction(m_nextFrameAction);
    toolBar->addAction(m_reloadAction);
    toolBar->addSeparator();
    toolBar->addAction(m_zoomInAction);
    toolBar->addAction(m_zoomOutAction);
    toolBar->addAction(m_resetZoomAction);
    toolBar->addAction(m_fitToWindowAction);
    toolBar->addSeparator();
    toolBar->addAction(m_smoothScalingAction);
    toolBar->addAction(m_pixelGridAction);

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
    updateFormatMatches();
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
    return m_imageWidget && !m_imageWidget->image().isNull();
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
    stream << (m_frameSpinBox ? m_frameSpinBox->value() : 1);
    stream << (m_planeComboBox ? m_planeComboBox->currentIndex() : 0);
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

    // States saved before multi-frame support end after the format id.
    int frame = 1;
    if (!stream.atEnd())
        stream >> frame;
    if (m_frameSpinBox) {
        const QSignalBlocker blocker(m_frameSpinBox);
        m_frameSpinBox->setValue(qMax(1, frame));
    }

    // States saved before plane-view support end after the frame number.
    updatePlaneCombo();  // rebuild for the restored decoder
    int plane = 0;
    if (!stream.atEnd())
        stream >> plane;
    if (m_planeComboBox && plane > 0 && plane < m_planeComboBox->count()) {
        const QSignalBlocker blocker(m_planeComboBox);
        m_planeComboBox->setCurrentIndex(plane);
    } else {
        plane = 0;
    }

    if (!m_hasFileLayout && m_metadataError.isEmpty()
        && m_widthSpinBox && m_heightSpinBox) {
        m_widthSpinBox->setValue(width);
        m_heightSpinBox->setValue(height);
        reload();
    } else if (m_hasFileLayout && (frame > 1 || plane > 0)) {
        // The initial reload in setupYuvUi() ran before the state was
        // restored; reload again with the restored frame and plane.
        reload();
    }
    return true;
}

void YuvViewer::setupYuvUi()
{
    m_infoTable = addInfoTab(tr("Info"));

    if (m_hasFileLayout) {
        reload();
        return;
    }

    const QString prompt = tr("Enter the image width and height, then select Reload.");
    if (!m_metadataError.isEmpty()) {
        reportError(m_metadataError + QStringLiteral("\n") + prompt);
        return;
    }

    m_imageWidget->setText(prompt);
    statusMessage(prompt, tr("open"));
}

void YuvViewer::reload()
{
    clear();
    // A new reload supersedes a pending load; cancellation lets the worker
    // abort its read early instead of running to completion.
    cancelAsyncTasks();

    if (!m_file || !m_widthSpinBox || !m_heightSpinBox || !m_decoder) {
        reportError(tr("The YUV viewer is not fully initialized."));
        return;
    }

    const int width = m_widthSpinBox->value();
    const int height = m_heightSpinBox->value();
    RawImageLayout requestedLayout{width, height, m_decoder->defaultStride(width), height};
    if (m_hasFileLayout && width == m_fileWidth && height == m_fileHeight) {
        requestedLayout.stride = m_fileStride;
        requestedLayout.scanline = m_fileScanline;
    }

    // Layout validation is cheap and stays synchronous; only file reading
    // and pixel conversion move to a worker thread.
    const auto layout = m_decoder->validateLayout(requestedLayout);
    if (!layout) {
        reportError(layout.error());
        return;
    }

    const QString fileName = m_file->fileName();
    const RawImageLayout loadLayout = *layout;
    const RawImageDecoder *decoder = m_decoder;
    const QString formatName = decoder->displayName();

    // A file holding a whole number of frames enables frame navigation.
    // A size mismatch is not rejected here: readData() reports it with
    // full layout details.
    const qint64 frames = frameCount(*decoder, loadLayout, QFileInfo(fileName).size());
    updateFrameUi(frames);
    const qint64 frameIndex = frames > 0 ? qMin<qint64>(m_frameSpinBox->value() - 1, frames - 1) : 0;
    const int plane = currentPlane();

    m_imageWidget->setText(tr("Loading..."));

    startAsyncTaskWithProgress<LoadResult>(
        [fileName, loadLayout, decoder, frameIndex, plane](QPromise<LoadResult> &promise) {
            promise.setProgressRange(0, 100);
            auto data = decoder->readData(fileName, loadLayout, frameIndex,
                                          [&promise](qint64 done, qint64 total) {
                promise.setProgressValue(total > 0 ? int(done * 100 / total) : 0);
                return !promise.isCanceled();
            });
            if (!data) {
                promise.addResult(LoadResult(std::unexpected(data.error())));
                return;
            }
            auto image = renderImage(decoder, *data, loadLayout, plane);
            if (!image) {
                promise.addResult(LoadResult(std::unexpected(image.error())));
                return;
            }
            promise.addResult(LoadedFrame{std::move(*data), std::move(*image)});
        },
        [this](int value) {
            statusMessage(tr("Loading... %1%").arg(value), tr("open"), 0);
        },
        [this, fileName, loadLayout, formatName, decoder, frameIndex](LoadResult result) {
            if (!result) {
                reportError(result.error());
                return;
            }
            // Keep the raw samples: plane switching, the pixel probe and
            // the histogram reuse them without re-reading the file.
            m_rawData = std::move(result->data);
            m_layout = loadLayout;
            displayImage(result->image);
            updateInfoTab(fileName, loadLayout, decoder);
            const QString fileDescription = tr("\"%1\", %2x%3, %4 (stride=%5, scanline=%6)")
                                                .arg(QDir::toNativeSeparators(fileName))
                                                .arg(loadLayout.width)
                                                .arg(loadLayout.height)
                                                .arg(formatName)
                                                .arg(loadLayout.stride)
                                                .arg(loadLayout.scanline);
            if (m_frameCount > 1) {
                statusMessage(tr("Opened %1, frame %2/%3.")
                                  .arg(fileDescription)
                                  .arg(frameIndex + 1)
                                  .arg(m_frameCount));
            } else {
                statusMessage(tr("Opened %1.").arg(fileDescription));
            }
        });
}

void YuvViewer::updateFrameUi(qint64 frameCount)
{
    m_frameCount = qMax<qint64>(frameCount, 1);
    if (!m_frameSpinBox)
        return;

    const QSignalBlocker blocker(m_frameSpinBox);
    m_frameSpinBox->setMaximum(m_frameCount > INT_MAX ? INT_MAX : int(m_frameCount));
    if (m_frameSpinBox->value() > m_frameSpinBox->maximum())
        m_frameSpinBox->setValue(m_frameSpinBox->maximum());

    const bool navigable = m_frameCount > 1;
    m_frameSpinBox->setEnabled(navigable);
    m_prevFrameAction->setEnabled(navigable);
    m_nextFrameAction->setEnabled(navigable);
    m_frameCountLabel->setText(QStringLiteral("/ %1").arg(m_frameCount));
}

void YuvViewer::cleanup()
{
    // The base class deletes the page widget; drop the dangling pointer.
    m_infoTable = nullptr;
    AbstractViewer::cleanup();
}

void YuvViewer::busyChanged(bool busy)
{
    AbstractViewer::busyChanged(busy);
    if (m_reloadAction)
        m_reloadAction->setEnabled(!busy);
}

void YuvViewer::onFormatChanged()
{
    const int index = m_formatComboBox ? m_formatComboBox->currentIndex() : -1;
    if (index >= 0)
        m_decoder = RawImageDecoders::all().at(index);
    updatePlaneCombo();

    // Without a known layout the viewer still waits for width/height input.
    if (m_hasFileLayout || hasContent())
        reload();
}

void YuvViewer::updatePlaneCombo()
{
    if (!m_planeComboBox)
        return;

    const QSignalBlocker blocker(m_planeComboBox);
    m_planeComboBox->clear();
    m_planeComboBox->addItem(tr("Composite"));
    if (m_decoder)
        m_planeComboBox->addItems(m_decoder->planeNames());
    // Composite plus a single plane (e.g. Y8) offers nothing to switch.
    m_planeComboBox->setEnabled(m_planeComboBox->count() > 2);
}

int YuvViewer::currentPlane() const
{
    // Combo index 0 is the composite view; planes are 0-based after it.
    return m_planeComboBox ? m_planeComboBox->currentIndex() - 1 : -1;
}

// Highlights the formats whose tight frame size divides the file size
// evenly. This inverts the usual workflow: with unknown data, set the
// expected width/height and pick one of the matching (bold) formats.
void YuvViewer::updateFormatMatches()
{
    if (!m_formatComboBox || !m_file || !m_widthSpinBox || !m_heightSpinBox)
        return;

    const qint64 fileSize = QFileInfo(m_file->fileName()).size();
    const int width = m_widthSpinBox->value();
    const int height = m_heightSpinBox->value();

    const QList<const RawImageDecoder *> &decoders = RawImageDecoders::all();
    const QFont normalFont = m_formatComboBox->font();
    QFont boldFont = normalFont;
    boldFont.setBold(true);

    for (int i = 0; i < decoders.size(); ++i) {
        const RawImageDecoder *decoder = decoders.at(i);
        // The heuristic assumes a tightly packed layout; padded files
        // (stride/scanline tags) intentionally do not match.
        const RawImageLayout tight{width, height, decoder->defaultStride(width), height};

        qint64 frames = 0;
        if (decoder->validateLayout(tight)) {
            const qint64 frameSize = decoder->expectedByteSize(tight);
            if (frameSize > 0 && fileSize >= frameSize && (fileSize % frameSize) == 0)
                frames = fileSize / frameSize;
        }

        m_formatComboBox->setItemData(i, frames > 0 ? boldFont : normalFont, Qt::FontRole);
        m_formatComboBox->setItemData(i,
                                      frames > 0 ? tr("Matches the file size: %1 bytes per frame, %2 frames.")
                                                     .arg(decoder->expectedByteSize(tight))
                                                     .arg(frames)
                                                 : QString(),
                                      Qt::ToolTipRole);
    }
}

void YuvViewer::onPlaneChanged()
{
    if (!m_decoder || m_rawData.isEmpty())
        return;  // nothing loaded yet; the next reload applies the selection

    const RawImageDecoder *decoder = m_decoder;
    const QByteArray data = m_rawData;  // implicitly shared: cheap capture
    const RawImageLayout layout = m_layout;
    const int plane = currentPlane();

    startAsyncTask(
        [decoder, data, layout, plane]() {
            return renderImage(decoder, data, layout, plane);
        },
        [this](RawImageDecoder::ImageResult result) {
            if (!result) {
                reportError(result.error());
                return;
            }
            displayImage(*result);
        });
}

void YuvViewer::clear()
{
    if (m_imageWidget) {
        m_imageWidget->setImage({});
        m_imageWidget->setText({});
    }

    if (m_infoTable)
        m_infoTable->setRowCount(0);

    m_rawData.clear();
    m_layout = {};
    m_image = {};
    m_imageSize = {};
    m_maxScaleFactor = m_minScaleFactor = m_initialScaleFactor = m_scaleFactor = 1;
    m_zoomInAction->setEnabled(false);
    m_zoomOutAction->setEnabled(false);
    m_resetZoomAction->setEnabled(false);
    m_fitToWindowAction->setEnabled(false);
    disablePrinting();
}

void YuvViewer::displayImage(const QImage &image)
{
    m_imageWidget->setText({});
    m_image = image;

    const qreal devicePixelRatio = m_imageWidget->devicePixelRatioF();
    m_imageSize = QSizeF(image.size()) / devicePixelRatio;
    m_imageWidget->setImage(image);

    const QWidget *parent = m_imageWidget->parentWidget();
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

void YuvViewer::updateInfoTab(const QString &fileName, const RawImageLayout &layout,
                              const RawImageDecoder *decoder)
{
    if (!m_infoTable || !decoder)
        return;

    const QLocale locale;
    int row = 0;
    const auto addRow = [this, &row](const QString &name, const QString &value) {
        m_infoTable->insertRow(row);
        m_infoTable->setItem(row, 0, new QTableWidgetItem(name));
        m_infoTable->setItem(row, 1, new QTableWidgetItem(value));
        ++row;
    };

    m_infoTable->setRowCount(0);
    addRow(tr("File"), QDir::toNativeSeparators(fileName));
    addRow(tr("Format"), decoder->displayName());
    addRow(tr("Width"), tr("%1 px").arg(layout.width));
    addRow(tr("Height"), tr("%1 px").arg(layout.height));
    addRow(tr("Stride"), tr("%1 bytes").arg(layout.stride));
    addRow(tr("Scanline"), tr("%1 lines").arg(layout.scanline));
    addRow(tr("Y plane size"),
           locale.formattedDataSize(qint64(layout.stride) * layout.scanline));
    addRow(tr("Frame size"), locale.formattedDataSize(decoder->expectedByteSize(layout)));
    addRow(tr("File size"), locale.formattedDataSize(QFileInfo(fileName).size()));
    addRow(tr("Frames"), locale.toString(m_frameCount));
}

// Maps a position in widget coordinates to composite image coordinates.
// A plane view shows the plane at its native (possibly subsampled)
// resolution, so the displayed image is scaled back onto the layout.
// Returns (-1,-1) when the position is outside the image.
QPoint YuvViewer::compositePosition(QPoint widgetPos) const
{
    if (m_image.isNull() || m_layout.width <= 0 || m_scaleFactor <= 0)
        return {-1, -1};

    const QRectF target = m_imageWidget->imageRect();
    const qreal dpr = m_imageWidget->devicePixelRatioF();
    const int dx = qFloor((widgetPos.x() - target.x()) * dpr / m_scaleFactor);
    const int dy = qFloor((widgetPos.y() - target.y()) * dpr / m_scaleFactor);
    if (dx < 0 || dy < 0 || dx >= m_image.width() || dy >= m_image.height())
        return {-1, -1};

    return {dx * m_layout.width / m_image.width(),
            dy * m_layout.height / m_image.height()};
}

bool YuvViewer::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_imageWidget && m_decoder && !m_rawData.isEmpty()) {
        switch (event->type()) {
        case QEvent::MouseMove: {
            const auto *mouseEvent = static_cast<const QMouseEvent *>(event);
            const QPoint pos = compositePosition(mouseEvent->position().toPoint());
            if (pos.x() >= 0) {
                statusMessage(tr("(%1, %2)  %3")
                                  .arg(pos.x())
                                  .arg(pos.y())
                                  .arg(m_decoder->describePixel(m_rawData, m_layout,
                                                                pos.x(), pos.y())),
                              tr("probe"), 0);
            }
            return false;
        }
        case QEvent::Leave:
            if (statusBar())
                statusBar()->clearMessage();
            return false;
        default:
            break;
        }
    }
    return AbstractViewer::eventFilter(watched, event);
}

void YuvViewer::reportError(const QString &message)
{
    QString detailedMessage = message;
    if (m_file) {
        detailedMessage += tr("\nFile: %1")
                               .arg(QDir::toNativeSeparators(m_file->fileName()));
    }

    qWarning().noquote() << viewerName() + "/open:"_L1 << detailedMessage;
    if (m_imageWidget)
        m_imageWidget->setText(detailedMessage);
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
    m_imageWidget->setScaleFactor(scaleFactor);
    enableZoomActions();
}

void YuvViewer::enableZoomActions()
{
    m_resetZoomAction->setEnabled(!qFuzzyCompare(m_scaleFactor, m_initialScaleFactor));
    m_zoomInAction->setEnabled(m_scaleFactor < m_maxScaleFactor);
    m_zoomOutAction->setEnabled(m_scaleFactor > m_minScaleFactor);
    m_fitToWindowAction->setEnabled(!m_image.isNull());
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

void YuvViewer::fitToWindow()
{
    if (m_image.isNull() || m_imageSize.isEmpty())
        return;

    const QWidget *viewport = m_imageWidget->parentWidget();
    const QSizeF available = viewport ? QSizeF(viewport->size()) : QSizeF();
    if (available.width() <= 0 || available.height() <= 0)
        return;

    setScaleFactor(qMin(available.width() / m_imageSize.width(),
                        available.height() / m_imageSize.height()));
}

void YuvViewer::retranslate()
{
    if (toolBars().isEmpty())
        return;

    toolBars().constFirst()->setWindowTitle(tr("YUV Image"));
    m_widthLabel->setText(tr("Width:"));
    m_heightLabel->setText(tr("Height:"));
    m_formatLabel->setText(tr("Format:"));
    m_frameLabel->setText(tr("Frame:"));
    m_planeLabel->setText(tr("Plane:"));
    if (m_planeComboBox && m_planeComboBox->count() > 0)
        m_planeComboBox->setItemText(0, tr("Composite"));
    m_widthSpinBox->setSuffix(tr(" px"));
    m_heightSpinBox->setSuffix(tr(" px"));
    m_reloadAction->setText(tr("&Reload"));
    m_prevFrameAction->setText(tr("Previous Frame"));
    m_nextFrameAction->setText(tr("Next Frame"));
    m_zoomInAction->setText(tr("Zoom &In"));
    m_zoomOutAction->setText(tr("Zoom &Out"));
    m_resetZoomAction->setText(tr("Reset Zoom"));
    m_fitToWindowAction->setText(tr("&Fit to Window"));
    m_smoothScalingAction->setText(tr("&Smooth Scaling"));
    m_pixelGridAction->setText(tr("Pixel &Grid"));

    if (m_infoTable && m_uiAssets.tabs) {
        const int index = m_uiAssets.tabs->indexOf(m_infoTable);
        if (index >= 0)
            m_uiAssets.tabs->setTabText(index, tr("Info"));
    }
}
