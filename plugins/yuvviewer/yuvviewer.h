// Copyright (C) 2026
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#ifndef YUVVIEWER_H
#define YUVVIEWER_H

#include "viewerinterfaces.h"

#include "rawimagedecoder.h"
#include "rawimagefilename.h"

#include <QByteArray>
#include <QImage>
#include <QPointer>
#include <QSizeF>
#include <QString>

#include <optional>

class QAction;
class QLabel;
class QPoint;
class QTableWidget;
class YuvControls;
class YuvImageWidget;

// Viewer for headerless image files. Because such a file says nothing
// about its own content, the viewer combines three sources of truth:
// the file name (see RawImageFileName), the saved session state and the
// user's choice in the toolbar (see YuvControls). Decoding itself is
// delegated to a RawImageDecoder.
//
// Widget members are QPointer because AbstractViewer::cleanup() destroys
// the toolbar and the overview pages while this object stays alive to be
// reused for the next file.
class YuvViewer : public ViewerInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.Examples.DocumentViewer.ViewerInterface/1.0"
                      FILE "yuvviewer.json")
    Q_INTERFACES(ViewerInterface)

public:
    Q_DISABLE_COPY_MOVE(YuvViewer)

    YuvViewer();
    ~YuvViewer() override;

    void init(QFile *file, QWidget *parent, QMainWindow *mainWindow) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    QString viewerName() const override { return QLatin1StringView(staticMetaObject.className()); }
    QStringList supportedMimeTypes() const override;
    QStringList supportedExtensions() const override;
    bool hasContent() const override;
    QByteArray saveState() const override;
    bool restoreState(QByteArray &state) override;
    bool supportsOverview() const override { return true; }
    bool supportsExtensionlessFiles() const override { return true; }
    // The format combo box and the size fields let the user describe any
    // file, so the viewer decides about the content instead of the factory.
    bool acceptsAnyFile() const override { return true; }
    void cleanup() override;

private slots:
    void setupYuvUi();
    void onFormatSelected();
    void onPlaneSelected();
    void onDisplayOptionsSelected();
    void zoomIn();
    void zoomOut();
    void resetZoom();
    void fitToWindow();
    void exportImage();

private:
    void retranslate() override;
    void busyChanged(bool busy) override;

    void createActions();
    void setupToolBar();

    // Collapses the loads that opening a file triggers (the initial one
    // and the one after restoreState()) into a single read.
    void requestReload();
    void reload();
    void clear();
    void releaseFrame();

    void displayImage(const QImage &image);
    void showDecoded(const QImage &decoded);
    void applyDisplayAndShow();
    void updateInfoTab(const RawImageLayout &layout, const RawImageDecoder *decoder);
    void reportError(const QString &message);

    void setScaleFactor(qreal scaleFactor);
    void applyScaleFactor(qreal scaleFactor);
    void updateZoomActions();

    // Maps a widget position to composite image coordinates, or (-1,-1)
    // when it lies outside the image.
    QPoint compositePosition(QPoint widgetPos) const;
    void probePixel(QPoint widgetPos);

    QPointer<YuvImageWidget> m_imageWidget;
    QPointer<YuvControls> m_controls;
    QPointer<QTableWidget> m_infoTable;
    QPointer<QLabel> m_histogramLabel;

    // Owned by this object and therefore valid across cleanup().
    QAction *m_reloadAction = nullptr;
    QAction *m_zoomInAction = nullptr;
    QAction *m_zoomOutAction = nullptr;
    QAction *m_resetZoomAction = nullptr;
    QAction *m_fitToWindowAction = nullptr;
    QAction *m_smoothScalingAction = nullptr;
    QAction *m_pixelGridAction = nullptr;
    QAction *m_exportAction = nullptr;

    // The format the user selected, mirrored out of the toolbar so that
    // it survives cleanup() for saveState().
    const RawImageDecoder *m_decoder = nullptr;
    // The format m_rawData was actually read with. Plane switching and
    // the pixel probe must never interpret the buffer with anything else.
    const RawImageDecoder *m_loadedDecoder = nullptr;

    // Dimensions (and optional row/plane padding) taken from the file name.
    // stride/scanline are present only when the name wrote them; switching
    // format then uses the new decoder's tight row instead of the old one.
    std::optional<RawImageFileName::NamedLayout> m_fileLayout;
    RawImageFileName::Metadata m_fileNameMetadata;
    QString m_metadataError;

    qint64 m_frameCount = 1;
    QByteArray m_rawData;
    RawImageLayout m_layout;
    // Decoded pixels before the display transform. The probe and histogram
    // never read this; they go back to m_rawData. The screen and the export
    // go through applyDisplayAndShow() into m_image.
    QImage m_decodedImage;
    QImage m_image;
    int m_displayGeneration = 0;
    bool m_reloadPending = false;

    qreal m_scaleFactor = 1;
    qreal m_initialScaleFactor = 1;
    qreal m_minScaleFactor = 1;
    qreal m_maxScaleFactor = 1;
    QSizeF m_imageSize;
};

#endif // YUVVIEWER_H
