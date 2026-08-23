// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#include "imageviewer.h"

#include <QFileDialog>
#include <QLabel>
#include <QMainWindow>
#include <QToolBar>

#include <QColorSpace>
#include <QIcon>
#include <QImageReader>
#include <QKeySequence>
#include <QPainter>
#include <QPixmap>
#include <QScreen>

#include <QDir>

#include <expected>

#ifdef DOCUMENTVIEWER_PRINTSUPPORT
#  include <QPrinter>
#endif

using namespace Qt::StringLiterals;

static QStringList imageFormats()
{
    QStringList result;
    const QByteArrayList &allFormats = QImageReader::supportedImageFormats();
    for (const auto &format : allFormats) {
        if (format != "pdf" && format != "tif" && format != "cur") // duplicate/non-existent
            result.append("image/"_L1 + QLatin1StringView(format));
    }
    return result;
}

namespace {

// Result of the worker-thread image load: the sRGB-converted image plus
// the description of the original color space for the status message.
struct LoadedImage
{
    QImage image;
    QString colorSpaceDescription;
};

using LoadResult = std::expected<LoadedImage, QString>;

}

static QString msgOpen(const QString &name, const LoadedImage &loaded)
{
    const QString description = !loaded.colorSpaceDescription.isEmpty()
    ? loaded.colorSpaceDescription : ImageViewer::tr("unknown");
    return ImageViewer::tr("Opened \"%1\", %2x%3, Depth: %4 (%5)")
                           .arg(QDir::toNativeSeparators(name))
                           .arg(loaded.image.width()).arg(loaded.image.height())
                           .arg(loaded.image.depth()).arg(description);
}

//! [init]
ImageViewer::ImageViewer()
    : m_zoomInAct(new QAction(this)),
      m_zoomOutAct(new QAction(this)),
      m_resetZoomAct(new QAction(this)),
      m_formats(imageFormats())
{
    connect(this, &AbstractViewer::uiInitialized, this, &ImageViewer::setupImageUi);
    QImageReader::setAllocationLimit(1024); // MB

    m_zoomInAct->setIcon(QIcon::fromTheme(QIcon::ThemeIcon::ZoomIn));
    m_zoomInAct->setShortcut(QKeySequence::ZoomIn);
    connect(m_zoomInAct, &QAction::triggered, this, &ImageViewer::zoomIn);

    m_zoomOutAct->setIcon(QIcon::fromTheme(QIcon::ThemeIcon::ZoomOut));
    m_zoomOutAct->setShortcut(QKeySequence::ZoomOut);
    connect(m_zoomOutAct, &QAction::triggered, this, &ImageViewer::zoomOut);

    m_resetZoomAct->setIcon(QIcon::fromTheme(QIcon::ThemeIcon::ZoomFitBest));
    m_resetZoomAct->setShortcut(QKeySequence(Qt::ControlModifier | Qt::Key_0));
    connect(m_resetZoomAct, &QAction::triggered, this, &ImageViewer::resetZoom);
}
//! [init]

ImageViewer::~ImageViewer() = default;

void ImageViewer::init(QFile *file, QWidget *parent, QMainWindow *mainWindow)
{
    m_imageLabel = new QLabel(parent);
    m_imageLabel->setFrameShape(QFrame::Box);
    m_imageLabel->setAlignment(Qt::AlignCenter);
    m_imageLabel->setScaledContents(true);

    AbstractViewer::init(file, m_imageLabel, mainWindow);
    setTranslationBaseName("imgviewer"_L1);

    QToolBar *toolBar = addToolBar();
    toolBar->addAction(m_zoomInAct);
    toolBar->addAction(m_zoomOutAct);
    toolBar->addAction(m_resetZoomAct);

    retranslate();
}

void ImageViewer::retranslate()
{
    if (toolBars().isEmpty())
        return;
    toolBars().at(0)->setWindowTitle(tr("Images"));
    m_zoomInAct->setText(tr("Zoom &In"));
    m_zoomOutAct->setText(tr("Zoom &Out"));
    m_resetZoomAct->setText(tr("Reset Zoom"));
}

QStringList ImageViewer::supportedMimeTypes() const
{
    return m_formats;
}

void ImageViewer::clear()
{
    m_imageLabel->setPixmap({});
    m_maxScaleFactor = m_minScaleFactor = m_initialScaleFactor = m_scaleFactor = 1;
}

void ImageViewer::setupImageUi()
{
    openFile();
}

//! [open]
void ImageViewer::openFile()
{
    const QString name = m_file->fileName();

    // QImageReader is not a QObject and is safe to use on a worker thread;
    // QPixmap conversion stays on the UI thread in the completion callback.
    startAsyncTask(
        [name]() -> LoadResult {
            QImageReader reader(name);
            QImage image = reader.read();
            if (image.isNull())
                return std::unexpected(reader.errorString());

            LoadedImage loaded;
            if (image.colorSpace().isValid()) {
                loaded.colorSpaceDescription = image.colorSpace().description();
                loaded.image = image.convertedToColorSpace(QColorSpace::SRgb);
            } else {
                loaded.image = std::move(image);
            }
            return loaded;
        },
        [this, name](LoadResult result) {
            if (!result) {
                statusMessage(tr("Cannot read file %1:\n%2.")
                              .arg(QDir::toNativeSeparators(name), result.error()),
                              tr("open"));
                disablePrinting();
                return;
            }

            clear();

            const auto devicePixelRatio = m_imageLabel->devicePixelRatioF();
            m_imageSize = QSizeF(result->image.size()) / devicePixelRatio;

            QPixmap pixmap = QPixmap::fromImage(result->image);
            pixmap.setDevicePixelRatio(devicePixelRatio);
            m_imageLabel->setPixmap(pixmap);

            const QSizeF targetSize = m_imageLabel->parentWidget()->size();
            if (m_imageSize.width() > targetSize.width()
                || m_imageSize.height() > targetSize.height()) {
                m_initialScaleFactor = qMin(targetSize.width() / m_imageSize.width(),
                                            targetSize.height() / m_imageSize.height());
            }
            m_maxScaleFactor = 3 * m_initialScaleFactor;
            m_minScaleFactor = m_initialScaleFactor / 3;
            doSetScaleFactor(m_initialScaleFactor);

            statusMessage(msgOpen(name, *result));
            maybeEnablePrinting();
        });
}
//! [open]

void ImageViewer::setScaleFactor(qreal scaleFactor)
{
    if (!qFuzzyCompare(m_scaleFactor, scaleFactor))
        doSetScaleFactor(scaleFactor);
}

void ImageViewer::doSetScaleFactor(qreal scaleFactor)
{
    m_scaleFactor = scaleFactor;
    const QSize labelSize = (m_imageSize * m_scaleFactor).toSize();
    m_imageLabel->setFixedSize(labelSize);
    enableZoomActions();
}

void ImageViewer::zoomIn()
{
    setScaleFactor(m_scaleFactor * 1.25);
}

void ImageViewer::zoomOut()
{
    setScaleFactor(m_scaleFactor * 0.8);
}

void ImageViewer::resetZoom()
{
    setScaleFactor(m_initialScaleFactor);
}

bool ImageViewer::hasContent() const
{
    return !m_imageLabel->pixmap().isNull();
}

void ImageViewer::enableZoomActions()
{
    m_resetZoomAct->setEnabled(!qFuzzyCompare(m_scaleFactor, m_initialScaleFactor));
    m_zoomInAct->setEnabled(m_scaleFactor < m_maxScaleFactor);
    m_zoomOutAct->setEnabled(m_scaleFactor > m_minScaleFactor);
}

#ifdef DOCUMENTVIEWER_PRINTSUPPORT
void ImageViewer::printDocument(QPrinter *printer) const
{
    if (!hasContent())
        return;

    QPainter painter(printer);
    QPixmap pixmap = m_imageLabel->pixmap();
    QRect rect = painter.viewport();
    QSize size = pixmap.size();
    size.scale(rect.size(), Qt::KeepAspectRatio);
    painter.setViewport(rect.x(), rect.y(), size.width(), size.height());
    painter.setWindow(pixmap.rect());
    painter.drawPixmap(0, 0, pixmap);
}
#endif // DOCUMENTVIEWER_PRINTSUPPORT
