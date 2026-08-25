// Copyright (C) 2026
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#include "yuvimagewidget.h"

#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QtMath>

YuvImageWidget::YuvImageWidget(QWidget *parent) : QFrame(parent)
{
}

void YuvImageWidget::setImage(const QImage &image)
{
    m_image = image;
    updateGeometry();
    update();
}

void YuvImageWidget::setText(const QString &text)
{
    m_text = text;
    update();
}

void YuvImageWidget::setScaleFactor(qreal scaleFactor)
{
    if (qFuzzyCompare(m_scaleFactor, scaleFactor))
        return;
    m_scaleFactor = scaleFactor;
    updateGeometry();
    update();
}

void YuvImageWidget::setSmoothScaling(bool smooth)
{
    m_smoothScaling = smooth;
    update();
}

void YuvImageWidget::setPixelGrid(bool grid)
{
    m_pixelGrid = grid;
    update();
}

QSize YuvImageWidget::sizeHint() const
{
    if (m_image.isNull())
        return QFrame::sizeHint();
    const QSizeF logical = QSizeF(m_image.size()) / devicePixelRatioF();
    return (logical * m_scaleFactor).toSize() + QSize(2 * frameWidth(), 2 * frameWidth());
}

void YuvImageWidget::paintEvent(QPaintEvent *event)
{
    QFrame::paintEvent(event);
    QPainter painter(this);
    const QRectF area = contentsRect();

    if (m_image.isNull()) {
        m_imageRect = QRectF();
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
    if (m_pixelGrid && step >= pixelGridThreshold)
        drawPixelGrid(painter, event->rect(), step);
}

void YuvImageWidget::drawPixelGrid(QPainter &painter, const QRectF &clip, qreal step)
{
    QPen pen(QColor(128, 128, 128, 90));
    pen.setCosmetic(true);
    painter.setPen(pen);

    const QPointF topLeft = m_imageRect.topLeft();
    const int firstCol = qMax(0, qFloor((clip.left() - topLeft.x()) / step));
    const int lastCol = qMin(m_image.width(), qCeil((clip.right() - topLeft.x()) / step));
    for (int col = firstCol; col <= lastCol; ++col) {
        const qreal x = topLeft.x() + col * step;
        painter.drawLine(QPointF(x, m_imageRect.top()), QPointF(x, m_imageRect.bottom()));
    }

    const int firstRow = qMax(0, qFloor((clip.top() - topLeft.y()) / step));
    const int lastRow = qMin(m_image.height(), qCeil((clip.bottom() - topLeft.y()) / step));
    for (int row = firstRow; row <= lastRow; ++row) {
        const qreal y = topLeft.y() + row * step;
        painter.drawLine(QPointF(m_imageRect.left(), y), QPointF(m_imageRect.right(), y));
    }
}
