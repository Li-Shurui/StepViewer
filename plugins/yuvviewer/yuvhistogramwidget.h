// Copyright (C) 2026
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#ifndef YUVHISTOGRAMWIDGET_H
#define YUVHISTOGRAMWIDGET_H

#include "rawimagehistogram.h"

#include <QList>
#include <QWidget>

// Histogram page of the YUV viewer: one chart per component plane,
// stacked vertically. The x axis spans the plane's whole native range
// (0..1023 for 10-bit samples, 0..65535 for 16-bit), and neighboring
// values are merged into as many bins as the plot is wide, so deep
// formats stay legible at any pane width. Hovering a bin shows the
// values it covers and their exact count.
//
// Painted from the palette on every update rather than rendered once
// into an image, so it follows the theme and the pane width.
class YuvHistogramWidget : public QWidget
{
    Q_OBJECT

public:
    explicit YuvHistogramWidget(QWidget *parent = nullptr);

    void setChannels(QList<RawImageHistogram::Channel> channels);
    void clear();

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    bool event(QEvent *event) override;
    void changeEvent(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    int contentHeight() const;
    void updateSizeConstraints();

    QList<RawImageHistogram::Channel> m_channels;
};

#endif // YUVHISTOGRAMWIDGET_H
