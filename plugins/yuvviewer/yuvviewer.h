// Copyright (C) 2026
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#ifndef YUVVIEWER_H
#define YUVVIEWER_H

#include "viewerinterfaces.h"

#include "rawimagedecoder.h"

#include <QByteArray>
#include <QList>
#include <QPair>
#include <QSizeF>
#include <QString>

class QAction;
class QComboBox;
class QImage;
class QLabel;
class QSpinBox;
class QTableWidget;
class YuvImageWidget;

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
    void reload();
    void onFormatChanged();
    void onPlaneChanged();
    void zoomIn();
    void zoomOut();
    void resetZoom();
    void fitToWindow();
    void exportImage();

private:
    void retranslate() override;
    void busyChanged(bool busy) override;
    void clear();
    void displayImage(const QImage &image);
    void updateInfoTab(const QString &fileName, const RawImageLayout &layout,
                       const RawImageDecoder *decoder);
    void reportError(const QString &message);
    void setScaleFactor(qreal scaleFactor);
    void doSetScaleFactor(qreal scaleFactor);
    void enableZoomActions();
    void updateFrameUi(qint64 frameCount);
    void updatePlaneCombo();
    void updateFormatMatches();
    int currentPlane() const;
    QPoint compositePosition(QPoint widgetPos) const;

    YuvImageWidget *m_imageWidget = nullptr;
    QLabel *m_widthLabel = nullptr;
    QLabel *m_heightLabel = nullptr;
    QLabel *m_formatLabel = nullptr;
    QLabel *m_frameLabel = nullptr;
    QLabel *m_frameCountLabel = nullptr;
    QLabel *m_planeLabel = nullptr;
    QComboBox *m_formatComboBox = nullptr;
    QComboBox *m_planeComboBox = nullptr;
    QSpinBox *m_widthSpinBox = nullptr;
    QSpinBox *m_heightSpinBox = nullptr;
    QSpinBox *m_frameSpinBox = nullptr;
    QTableWidget *m_infoTable = nullptr;
    QLabel *m_histogramLabel = nullptr;
    QAction *m_reloadAction = nullptr;
    QAction *m_prevFrameAction = nullptr;
    QAction *m_nextFrameAction = nullptr;
    QAction *m_zoomInAction = nullptr;
    QAction *m_zoomOutAction = nullptr;
    QAction *m_resetZoomAction = nullptr;
    QAction *m_fitToWindowAction = nullptr;
    QAction *m_smoothScalingAction = nullptr;
    QAction *m_pixelGridAction = nullptr;
    QAction *m_exportAction = nullptr;
    const RawImageDecoder *m_decoder = nullptr;
    const RawImageDecoder *m_loadedDecoder = nullptr;
    bool m_hasFileLayout = false;
    int m_fileWidth = 0;
    int m_fileHeight = 0;
    int m_fileStride = 0;
    int m_fileScanline = 0;
    QList<QPair<QString, QString>> m_fileNameMetadata;
    QString m_metadataError;
    qint64 m_frameCount = 1;
    QByteArray m_rawData;
    RawImageLayout m_layout;
    QImage m_image;
    qreal m_scaleFactor = 1;
    qreal m_initialScaleFactor = 1;
    qreal m_minScaleFactor = 1;
    qreal m_maxScaleFactor = 1;
    QSizeF m_imageSize;
};

#endif // YUVVIEWER_H
