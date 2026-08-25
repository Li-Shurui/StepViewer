// Copyright (C) 2026
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#include "yuvviewer.h"

#include "rawimagedecoder.h"
#include "rawimagefilename.h"
#include "rawimageframe.h"
#include "rawimagehistogram.h"
#include "yuvcontrols.h"
#include "yuvimagewidget.h"

#include <opencv2/core.hpp>

#include <QAction>
#include <QDataStream>
#include <QDebug>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QIcon>
#include <QKeySequence>
#include <QLabel>
#include <QLocale>
#include <QMouseEvent>
#include <QPixmap>
#include <QStatusBar>
#include <QTableWidget>
#include <QTabWidget>
#include <QToolBar>

#include <exception>
#include <new>
#include <utility>

using namespace Qt::StringLiterals;

namespace {

// One loaded frame: the raw samples plus the rendered views of them.
struct LoadedFrame
{
    QByteArray data;
    QImage image;
    QImage histogram;
};
using LoadResult = std::expected<LoadedFrame, QString>;

constexpr qreal zoomInStep = 1.25;
constexpr qreal zoomOutStep = 0.8;
// How far the initial (fit) scale may be exceeded or undercut.
constexpr qreal zoomRange = 3;

} // namespace

YuvViewer::YuvViewer()
{
    // OpenCV 4.12's MinGW AVX2 semi-planar YUV converters can fault on some
    // Windows systems instead of reporting an exception. Select the portable
    // path before this plugin performs any OpenCV work.
    cv::setUseOptimized(false);

    setTranslationBaseName("yuvviewer"_L1);
    createActions();
    connect(this, &AbstractViewer::uiInitialized, this, &YuvViewer::setupYuvUi);
}

YuvViewer::~YuvViewer() = default;

void YuvViewer::createActions()
{
    const auto addAction = [this](QKeySequence shortcut) {
        auto *action = new QAction(this);
        action->setShortcut(shortcut);
        return action;
    };

    m_reloadAction = addAction(QKeySequence(Qt::ControlModifier | Qt::Key_R));
    connect(m_reloadAction, &QAction::triggered, this, &YuvViewer::requestReload);

    m_prevFrameAction = addAction(QKeySequence(Qt::Key_PageUp));
    m_prevFrameAction->setIcon(QIcon::fromTheme(QIcon::ThemeIcon::GoPrevious));
    connect(m_prevFrameAction, &QAction::triggered, this, [this] {
        if (m_controls)
            m_controls->stepFrame(-1);
    });

    m_nextFrameAction = addAction(QKeySequence(Qt::Key_PageDown));
    m_nextFrameAction->setIcon(QIcon::fromTheme(QIcon::ThemeIcon::GoNext));
    connect(m_nextFrameAction, &QAction::triggered, this, [this] {
        if (m_controls)
            m_controls->stepFrame(1);
    });

    m_zoomInAction = addAction(QKeySequence::ZoomIn);
    m_zoomInAction->setIcon(QIcon::fromTheme(QIcon::ThemeIcon::ZoomIn));
    connect(m_zoomInAction, &QAction::triggered, this, &YuvViewer::zoomIn);

    m_zoomOutAction = addAction(QKeySequence::ZoomOut);
    m_zoomOutAction->setIcon(QIcon::fromTheme(QIcon::ThemeIcon::ZoomOut));
    connect(m_zoomOutAction, &QAction::triggered, this, &YuvViewer::zoomOut);

    m_resetZoomAction = addAction(QKeySequence(Qt::ControlModifier | Qt::Key_0));
    m_resetZoomAction->setIcon(QIcon::fromTheme(QIcon::ThemeIcon::ZoomFitBest));
    connect(m_resetZoomAction, &QAction::triggered, this, &YuvViewer::resetZoom);

    m_fitToWindowAction = addAction(QKeySequence(Qt::ControlModifier | Qt::Key_9));
    connect(m_fitToWindowAction, &QAction::triggered, this, &YuvViewer::fitToWindow);

    // Nearest-neighbor is the default: raw image inspection cares about
    // exact pixel values, not pretty interpolation.
    m_smoothScalingAction = addAction({});
    m_smoothScalingAction->setCheckable(true);
    connect(m_smoothScalingAction, &QAction::toggled, this, [this](bool checked) {
        if (m_imageWidget)
            m_imageWidget->setSmoothScaling(checked);
    });

    m_pixelGridAction = addAction({});
    m_pixelGridAction->setCheckable(true);
    m_pixelGridAction->setChecked(true);
    connect(m_pixelGridAction, &QAction::toggled, this, [this](bool checked) {
        if (m_imageWidget)
            m_imageWidget->setPixelGrid(checked);
    });

    m_exportAction = addAction(QKeySequence(Qt::ControlModifier | Qt::Key_E));
    m_exportAction->setIcon(QIcon::fromTheme(QIcon::ThemeIcon::DocumentSaveAs));
    connect(m_exportAction, &QAction::triggered, this, &YuvViewer::exportImage);

    m_prevFrameAction->setEnabled(false);
    m_nextFrameAction->setEnabled(false);
    m_zoomInAction->setEnabled(false);
    m_zoomOutAction->setEnabled(false);
    m_resetZoomAction->setEnabled(false);
    m_fitToWindowAction->setEnabled(false);
    m_exportAction->setEnabled(false);
}

void YuvViewer::init(QFile *file, QWidget *parent, QMainWindow *mainWindow)
{
    m_imageWidget = new YuvImageWidget(parent);
    m_imageWidget->setFrameShape(QFrame::Box);
    m_imageWidget->setMouseTracking(true);
    m_imageWidget->installEventFilter(this);

    AbstractViewer::init(file, m_imageWidget, mainWindow);

    setupToolBar();

    // The file name is the only metadata a headerless file carries, so it
    // decides both the initial format and the initial frame size.
    const QString fileName = file->fileName();
    m_decoder = RawImageDecoders::findByExtension(QFileInfo(fileName).suffix());
    if (!m_decoder)
        m_decoder = RawImageDecoders::defaultDecoder();
    m_controls->setDecoder(m_decoder);
    m_controls->rebuildPlanes();
    m_controls->setFileSize(QFileInfo(fileName).size());

    m_fileLayout.reset();
    m_metadataError.clear();
    m_fileNameMetadata = RawImageFileName::metadata(fileName);

    const auto parsedLayout = RawImageFileName::layout(fileName, *m_decoder);
    if (!parsedLayout) {
        m_metadataError = parsedLayout.error();
    } else if (parsedLayout->has_value()) {
        m_fileLayout = *parsedLayout;
        m_controls->setImageSize(m_fileLayout->width, m_fileLayout->height);
    }

    clear();
    retranslate();
}

void YuvViewer::setupToolBar()
{
    QToolBar *toolBar = addToolBar();
    m_controls = new YuvControls(toolBar);
    connect(m_controls, &YuvControls::formatSelected, this, &YuvViewer::onFormatSelected);
    connect(m_controls, &YuvControls::planeSelected, this, &YuvViewer::onPlaneSelected);
    connect(m_controls, &YuvControls::frameSelected, this, &YuvViewer::requestReload);

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
    toolBar->addSeparator();
    toolBar->addAction(m_exportAction);
}

void YuvViewer::setupYuvUi()
{
    m_infoTable = addInfoTab(tr("Info"));

    auto *histogram = new QLabel;
    histogram->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_histogramLabel = histogram;
    addTabPage(histogram, tr("Histogram"));

    if (m_fileLayout) {
        requestReload();
        return;
    }

    const QString prompt = tr("Enter the image width and height, then select Reload.");
    if (!m_metadataError.isEmpty()) {
        reportError(m_metadataError + u'\n' + prompt);
        return;
    }

    if (m_imageWidget)
        m_imageWidget->setText(prompt);
    statusMessage(prompt, tr("open"));
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
    // Called after cleanup() the controls are gone and there is nothing
    // left to describe.
    if (!m_controls)
        return {};

    QByteArray state;
    QDataStream stream(&state, QIODevice::WriteOnly);
    stream << QString(viewerName());
    stream << m_controls->imageWidth();
    stream << m_controls->imageHeight();
    stream << (m_decoder ? QString(m_decoder->id()) : QString());
    stream << m_controls->frame();
    stream << m_controls->plane() + 1;
    return state;
}

bool YuvViewer::restoreState(QByteArray &state)
{
    if (!m_controls)
        return false;

    QDataStream stream(&state, QIODevice::ReadOnly);
    QString viewer;
    int width = 0;
    int height = 0;
    stream >> viewer >> width >> height;

    if (stream.status() != QDataStream::Ok || viewer != viewerName())
        return false;
    if (width < RawImageDecoder::minimumDimension || width > RawImageDecoder::maximumDimension
        || height < RawImageDecoder::minimumDimension
        || height > RawImageDecoder::maximumDimension) {
        return false;
    }

    // States saved before multi-format support end after the height field.
    QString formatId;
    if (!stream.atEnd())
        stream >> formatId;

    const RawImageDecoder *extensionDecoder = m_file
        ? RawImageDecoders::findByExtension(QFileInfo(m_file->fileName()).suffix())
        : nullptr;
    if (extensionDecoder) {
        // A specific extension such as .Y8 or .NV12 is authoritative. The
        // saved format belongs to the previously opened file and must not
        // reinterpret data that is already loading for this file.
        m_decoder = extensionDecoder;
    } else if (const RawImageDecoder *saved = RawImageDecoders::findById(formatId)) {
        m_decoder = saved;
    }
    m_controls->setDecoder(m_decoder);
    m_controls->rebuildPlanes();  // the plane list depends on the format

    // States saved before multi-frame support end after the format id.
    int frame = 1;
    if (!stream.atEnd())
        stream >> frame;
    m_controls->setFrame(frame);

    // States saved before plane-view support end after the frame number.
    int planeIndex = 0;
    if (!stream.atEnd())
        stream >> planeIndex;
    m_controls->setPlane(planeIndex - 1);

    // The file name wins over the saved size; without one, the size from
    // the previous session is the best guess available.
    if (!m_fileLayout && m_metadataError.isEmpty())
        m_controls->setImageSize(width, height);

    // setupYuvUi() may already have asked for a load; requestReload()
    // collapses both requests into one read with the restored settings.
    if (m_fileLayout || m_metadataError.isEmpty())
        requestReload();
    return true;
}

void YuvViewer::requestReload()
{
    if (m_reloadPending)
        return;

    // Opening a file runs setupYuvUi() and restoreState() back to back,
    // and both want to load. Deferring to the next event loop pass turns
    // that into a single read of the file.
    m_reloadPending = true;
    QMetaObject::invokeMethod(this, [this] {
        m_reloadPending = false;
        if (m_file && m_controls)
            reload();
    }, Qt::QueuedConnection);
}

void YuvViewer::reload()
{
    clear();
    // A new reload supersedes a pending load; cancellation lets the worker
    // abort its read early instead of running to completion.
    cancelAsyncTasks();

    if (!m_file || !m_controls || !m_decoder) {
        reportError(tr("The YUV viewer is not fully initialized."));
        return;
    }

    const int width = m_controls->imageWidth();
    const int height = m_controls->imageHeight();
    RawImageLayout requestedLayout{width, height, m_decoder->defaultStride(width), height};
    // Padding from the file name only applies while the user keeps the
    // frame size the name declared.
    if (m_fileLayout && width == m_fileLayout->width && height == m_fileLayout->height) {
        requestedLayout.stride = m_fileLayout->stride;
        requestedLayout.scanline = m_fileLayout->scanline;
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
    const qint64 fileSize = QFileInfo(fileName).size();
    const qint64 frames = RawImageFrame::count(*decoder, loadLayout, fileSize);
    m_frameCount = qMax<qint64>(frames, 1);
    m_controls->setFrameCount(m_frameCount);
    m_prevFrameAction->setEnabled(m_frameCount > 1);
    m_nextFrameAction->setEnabled(m_frameCount > 1);

    const qint64 frameIndex = frames > 0 ? qMin<qint64>(m_controls->frame() - 1, frames - 1) : 0;
    const int plane = m_controls->plane();

    if (m_imageWidget)
        m_imageWidget->setText(tr("Loading..."));

    startAsyncTaskWithProgress<LoadResult>(
        [fileName, loadLayout, decoder, frameIndex, plane](QPromise<LoadResult> &promise) {
            promise.setProgressRange(0, 100);
            try {
                auto data = decoder->readData(fileName, loadLayout, frameIndex,
                                              [&promise](qint64 done, qint64 total) {
                    promise.setProgressValue(total > 0 ? int(done * 100 / total) : 0);
                    return !promise.isCanceled();
                });
                if (!data) {
                    promise.addResult(LoadResult(std::unexpected(data.error())));
                    return;
                }
                auto image = RawImageFrame::render(decoder, *data, loadLayout, plane);
                if (!image) {
                    promise.addResult(LoadResult(std::unexpected(image.error())));
                    return;
                }
                QImage histogram = RawImageHistogram::render(*decoder, *data, loadLayout);
                promise.addResult(LoadedFrame{std::move(*data), std::move(*image),
                                              std::move(histogram)});
            } catch (const std::bad_alloc &) {
                promise.addResult(LoadResult(std::unexpected(
                    YuvViewer::tr("Not enough memory to load and render the image."))));
            } catch (const std::exception &exception) {
                promise.addResult(LoadResult(std::unexpected(
                    YuvViewer::tr("Unexpected error while loading the image: %1")
                        .arg(QString::fromLocal8Bit(exception.what())))));
            } catch (...) {
                promise.addResult(LoadResult(std::unexpected(
                    YuvViewer::tr("An unknown error occurred while loading the image."))));
            }
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
            m_loadedDecoder = decoder;
            m_rawData = std::move(result->data);
            m_layout = loadLayout;
            displayImage(result->image);
            if (m_histogramLabel)
                m_histogramLabel->setPixmap(QPixmap::fromImage(result->histogram));
            updateInfoTab(loadLayout, decoder);

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

void YuvViewer::cleanup()
{
    // A closed file must not keep its frame buffer alive: raw frames run
    // into tens of megabytes and the viewer object is reused, not deleted.
    releaseFrame();
    m_image = QImage();
    m_fileNameMetadata.clear();
    m_fileLayout.reset();
    m_metadataError.clear();
    // The QPointer members go null on their own once the base class
    // deletes the toolbar and the overview pages.
    AbstractViewer::cleanup();
}

void YuvViewer::releaseFrame()
{
    m_loadedDecoder = nullptr;
    m_rawData = QByteArray();
    m_layout = {};
}

void YuvViewer::busyChanged(bool busy)
{
    AbstractViewer::busyChanged(busy);
    m_reloadAction->setEnabled(!busy);
}

void YuvViewer::onFormatSelected()
{
    if (!m_controls)
        return;

    m_decoder = m_controls->decoder();
    m_controls->rebuildPlanes();

    // Without a known layout the viewer still waits for width/height input.
    if (m_fileLayout || hasContent())
        requestReload();
}

void YuvViewer::onPlaneSelected()
{
    if (!m_loadedDecoder || m_rawData.isEmpty() || !m_controls)
        return;  // nothing loaded yet; the next reload applies the selection

    if (m_loadedDecoder != m_decoder) {
        reportError(tr("The selected format no longer matches the loaded image. Reload the file."));
        return;
    }

    const RawImageDecoder *decoder = m_loadedDecoder;
    const QByteArray data = m_rawData;  // implicitly shared: cheap capture
    const RawImageLayout layout = m_layout;
    const int plane = m_controls->plane();

    startAsyncTask(
        [decoder, data, layout, plane] {
            return RawImageFrame::render(decoder, data, layout, plane);
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
    if (m_histogramLabel)
        m_histogramLabel->clear();

    releaseFrame();
    m_image = QImage();
    m_imageSize = {};
    m_maxScaleFactor = m_minScaleFactor = m_initialScaleFactor = m_scaleFactor = 1;
    updateZoomActions();
    disablePrinting();
}

void YuvViewer::displayImage(const QImage &image)
{
    if (!m_imageWidget)
        return;

    m_imageWidget->setText({});
    m_image = image;

    const qreal devicePixelRatio = m_imageWidget->devicePixelRatioF();
    m_imageSize = QSizeF(image.size()) / devicePixelRatio;
    m_imageWidget->setImage(image);

    // Scale an oversized image down to the viewport; anything that fits
    // is shown at 1:1. Switching planes keeps the factor already in use.
    const QWidget *parent = m_imageWidget->parentWidget();
    const QSizeF targetSize = parent ? QSizeF(parent->size()) : m_imageSize;
    if (targetSize.width() > 0 && targetSize.height() > 0
        && (m_imageSize.width() > targetSize.width()
            || m_imageSize.height() > targetSize.height())) {
        m_initialScaleFactor = qMin(targetSize.width() / m_imageSize.width(),
                                    targetSize.height() / m_imageSize.height());
    }

    m_maxScaleFactor = zoomRange * m_initialScaleFactor;
    m_minScaleFactor = m_initialScaleFactor / zoomRange;
    applyScaleFactor(m_initialScaleFactor);
}

void YuvViewer::updateInfoTab(const RawImageLayout &layout, const RawImageDecoder *decoder)
{
    if (!m_infoTable || !decoder || !m_file)
        return;

    const QLocale locale;
    int row = 0;
    const auto addRow = [this, &row](const QString &name, const QString &value) {
        m_infoTable->insertRow(row);
        m_infoTable->setItem(row, 0, new QTableWidgetItem(name));
        m_infoTable->setItem(row, 1, new QTableWidgetItem(value));
        ++row;
    };

    const QString fileName = m_file->fileName();
    m_infoTable->setRowCount(0);
    addRow(tr("File"), QDir::toNativeSeparators(fileName));
    for (const auto &[key, value] : std::as_const(m_fileNameMetadata)) {
        // Dimensions and padding are reported from the resolved layout
        // below, which may differ from what the name asked for.
        if (!RawImageFileName::isLayoutKey(key))
            addRow(RawImageFileName::displayName(key), value);
    }
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

// A plane view shows the plane at its native (possibly subsampled)
// resolution, so the displayed image is scaled back onto the layout.
QPoint YuvViewer::compositePosition(QPoint widgetPos) const
{
    if (!m_imageWidget || m_image.isNull() || m_layout.width <= 0 || m_scaleFactor <= 0)
        return {-1, -1};

    const QRectF target = m_imageWidget->imageRect();
    const qreal dpr = m_imageWidget->devicePixelRatioF();
    const int dx = qFloor((widgetPos.x() - target.x()) * dpr / m_scaleFactor);
    const int dy = qFloor((widgetPos.y() - target.y()) * dpr / m_scaleFactor);
    if (dx < 0 || dy < 0 || dx >= m_image.width() || dy >= m_image.height())
        return {-1, -1};

    return {dx * m_layout.width / m_image.width(), dy * m_layout.height / m_image.height()};
}

void YuvViewer::probePixel(QPoint widgetPos)
{
    const QPoint pos = compositePosition(widgetPos);
    if (pos.x() < 0)
        return;

    // describePixel() indexes by the layout, so a buffer that no longer
    // matches it would be read out of bounds.
    const auto validData = RawImageFrame::validate(m_loadedDecoder, m_rawData, m_layout);
    if (!validData) {
        reportError(validData.error());
        releaseFrame();
        return;
    }

    statusMessage(tr("(%1, %2)  %3")
                      .arg(pos.x())
                      .arg(pos.y())
                      .arg(m_loadedDecoder->describePixel(m_rawData, m_layout, pos.x(), pos.y())),
                  tr("probe"), 0);
}

bool YuvViewer::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_imageWidget && m_loadedDecoder && !m_rawData.isEmpty()) {
        switch (event->type()) {
        case QEvent::MouseMove:
            probePixel(static_cast<const QMouseEvent *>(event)->position().toPoint());
            return false;
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
    if (m_file)
        detailedMessage += tr("\nFile: %1").arg(QDir::toNativeSeparators(m_file->fileName()));

    qWarning().noquote() << viewerName() + "/open:"_L1 << detailedMessage;
    if (m_imageWidget)
        m_imageWidget->setText(detailedMessage);
    statusMessage(detailedMessage, tr("open"), 15000);
}

void YuvViewer::setScaleFactor(qreal scaleFactor)
{
    if (!qFuzzyCompare(m_scaleFactor, scaleFactor))
        applyScaleFactor(scaleFactor);
}

void YuvViewer::applyScaleFactor(qreal scaleFactor)
{
    m_scaleFactor = scaleFactor;
    if (m_imageWidget)
        m_imageWidget->setScaleFactor(scaleFactor);
    updateZoomActions();
}

void YuvViewer::updateZoomActions()
{
    const bool hasImage = !m_image.isNull();
    m_resetZoomAction->setEnabled(!qFuzzyCompare(m_scaleFactor, m_initialScaleFactor));
    m_zoomInAction->setEnabled(hasImage && m_scaleFactor < m_maxScaleFactor);
    m_zoomOutAction->setEnabled(hasImage && m_scaleFactor > m_minScaleFactor);
    m_fitToWindowAction->setEnabled(hasImage);
    m_exportAction->setEnabled(hasImage);
}

void YuvViewer::zoomIn()
{
    setScaleFactor(m_scaleFactor * zoomInStep);
}

void YuvViewer::zoomOut()
{
    setScaleFactor(m_scaleFactor * zoomOutStep);
}

void YuvViewer::resetZoom()
{
    setScaleFactor(m_initialScaleFactor);
}

void YuvViewer::fitToWindow()
{
    if (!m_imageWidget || m_image.isNull() || m_imageSize.isEmpty())
        return;

    const QWidget *viewport = m_imageWidget->parentWidget();
    const QSizeF available = viewport ? QSizeF(viewport->size()) : QSizeF();
    if (available.width() <= 0 || available.height() <= 0)
        return;

    setScaleFactor(qMin(available.width() / m_imageSize.width(),
                        available.height() / m_imageSize.height()));
}

void YuvViewer::exportImage()
{
    if (m_image.isNull() || !m_file)
        return;

    // Export exactly what is on screen: the composite rendering or the
    // selected component plane.
    QString suggestion = QFileInfo(m_file->fileName()).completeBaseName();
    if (m_controls) {
        if (m_frameCount > 1)
            suggestion += QStringLiteral("_frame%1").arg(m_controls->frame());
        const QString planeName = m_controls->selectedPlaneName();
        if (!planeName.isEmpty())
            suggestion += u'_' + planeName.toLower();
    }
    suggestion += QStringLiteral(".png");

    const QString filter = tr("PNG image (*.png);;BMP image (*.bmp)");
    QString selectedFilter;
    const QString fileName = QFileDialog::getSaveFileName(m_imageWidget, tr("Export Image"),
                                                          suggestion, filter, &selectedFilter);
    if (fileName.isEmpty())
        return;

    QString finalName = fileName;
    if (QFileInfo(finalName).suffix().isEmpty()) {
        finalName += selectedFilter.contains("bmp"_L1, Qt::CaseInsensitive)
            ? QStringLiteral(".bmp") : QStringLiteral(".png");
    }

    if (!m_image.save(finalName)) {
        reportError(tr("Failed to save the image to \"%1\".")
                        .arg(QDir::toNativeSeparators(finalName)));
        return;
    }
    statusMessage(tr("Exported \"%1\".").arg(QDir::toNativeSeparators(finalName)), tr("export"));
}

void YuvViewer::retranslate()
{
    m_reloadAction->setText(tr("&Reload"));
    m_prevFrameAction->setText(tr("Previous Frame"));
    m_nextFrameAction->setText(tr("Next Frame"));
    m_zoomInAction->setText(tr("Zoom &In"));
    m_zoomOutAction->setText(tr("Zoom &Out"));
    m_resetZoomAction->setText(tr("Reset Zoom"));
    m_fitToWindowAction->setText(tr("&Fit to Window"));
    m_smoothScalingAction->setText(tr("&Smooth Scaling"));
    m_pixelGridAction->setText(tr("Pixel &Grid"));
    m_exportAction->setText(tr("&Export..."));

    if (!toolBars().isEmpty())
        toolBars().constFirst()->setWindowTitle(tr("YUV Image"));
    if (m_controls)
        m_controls->retranslate();

    if (QTabWidget *tabs = m_uiAssets.tabs) {
        if (m_infoTable) {
            const int index = tabs->indexOf(m_infoTable);
            if (index >= 0)
                tabs->setTabText(index, tr("Info"));
        }
        if (m_histogramLabel) {
            const int index = tabs->indexOf(m_histogramLabel);
            if (index >= 0)
                tabs->setTabText(index, tr("Histogram"));
        }
    }
}
