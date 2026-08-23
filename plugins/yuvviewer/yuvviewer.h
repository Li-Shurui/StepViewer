// Copyright (C) 2026
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#ifndef YUVVIEWER_H
#define YUVVIEWER_H

#include "viewerinterfaces.h"

#include <QSizeF>
#include <QString>

class QAction;
class QComboBox;
class QImage;
class QLabel;
class QSpinBox;
class RawImageDecoder;

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
    QString viewerName() const override { return QLatin1StringView(staticMetaObject.className()); }
    QStringList supportedMimeTypes() const override;
    QStringList supportedExtensions() const override;
    bool hasContent() const override;
    QByteArray saveState() const override;
    bool restoreState(QByteArray &state) override;
    bool supportsOverview() const override { return false; }

private slots:
    void setupYuvUi();
    void reload();
    void onFormatChanged();
    void zoomIn();
    void zoomOut();
    void resetZoom();

private:
    void retranslate() override;
    void clear();
    void displayImage(const QImage &image);
    void reportError(const QString &message);
    void setScaleFactor(qreal scaleFactor);
    void doSetScaleFactor(qreal scaleFactor);
    void enableZoomActions();

    QLabel *m_imageLabel = nullptr;
    QLabel *m_widthLabel = nullptr;
    QLabel *m_heightLabel = nullptr;
    QLabel *m_formatLabel = nullptr;
    QComboBox *m_formatComboBox = nullptr;
    QSpinBox *m_widthSpinBox = nullptr;
    QSpinBox *m_heightSpinBox = nullptr;
    QAction *m_reloadAction = nullptr;
    QAction *m_zoomInAction = nullptr;
    QAction *m_zoomOutAction = nullptr;
    QAction *m_resetZoomAction = nullptr;
    const RawImageDecoder *m_decoder = nullptr;
    bool m_hasFileLayout = false;
    int m_fileWidth = 0;
    int m_fileHeight = 0;
    int m_fileStride = 0;
    int m_fileScanline = 0;
    QString m_metadataError;
    qreal m_scaleFactor = 1;
    qreal m_initialScaleFactor = 1;
    qreal m_minScaleFactor = 1;
    qreal m_maxScaleFactor = 1;
    QSizeF m_imageSize;
};

#endif // YUVVIEWER_H
