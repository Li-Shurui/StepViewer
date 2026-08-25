// Copyright (C) 2026
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#ifndef YUVIMAGEWIDGET_H
#define YUVIMAGEWIDGET_H

#include <QFrame>
#include <QImage>
#include <QRectF>
#include <QString>

// Image display widget for the YUV viewer. Unlike a QLabel with
// scaledContents, painting is fully controlled here: nearest-neighbor
// vs. smooth scaling, a pixel grid at high zoom levels, and text
// prompts when no image is loaded. The widget reports the scaled image
// size as its size hint so the enclosing scroll area adds scroll bars.
class YuvImageWidget : public QFrame
{
    Q_OBJECT

public:
    // Below this many widget pixels per image pixel the grid lines would
    // cover more area than the pixels they separate.
    static constexpr qreal pixelGridThreshold = 4.0;

    explicit YuvImageWidget(QWidget *parent = nullptr);

    const QImage &image() const { return m_image; }

    // Where the image was last painted, in widget coordinates. Valid
    // after the first paint following setImage()/setScaleFactor().
    QRectF imageRect() const { return m_imageRect; }

    void setImage(const QImage &image);

    // Message shown centered while no image is available, e.g. a load
    // error or a prompt for the missing dimensions.
    void setText(const QString &text);

    void setScaleFactor(qreal scaleFactor);
    void setSmoothScaling(bool smooth);
    void setPixelGrid(bool grid);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void drawPixelGrid(QPainter &painter, const QRectF &clip, qreal step);

    QImage m_image;
    QString m_text;
    QRectF m_imageRect;
    qreal m_scaleFactor = 1;
    bool m_smoothScaling = false;
    bool m_pixelGrid = true;
};

#endif // YUVIMAGEWIDGET_H
