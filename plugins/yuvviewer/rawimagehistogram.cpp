// Copyright (C) 2026
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#include "rawimagehistogram.h"

#include "rawimageframe.h"

#include <QColor>
#include <QCoreApplication>
#include <QFont>
#include <QLocale>
#include <QPainter>
#include <QPen>
#include <QRect>

#include <array>

using namespace Qt::StringLiterals;

namespace {

struct Tr
{
    Q_DECLARE_TR_FUNCTIONS(RawImageHistogram)
};

constexpr int binCount = 256;

// Chart geometry in device-independent pixels. One chart ("row") per
// component plane, stacked vertically.
namespace metrics {
constexpr int margin = 8;
constexpr int leftAxis = 64;        // room for the count labels
constexpr int bottomAxis = 36;      // room for the value ticks and label
constexpr int binWidth = 2;
constexpr int plotWidth = binCount * binWidth;
constexpr int plotHeight = 120;
constexpr int titleHeight = 20;
constexpr int rowHeight = titleHeight + plotHeight + bottomAxis + margin;
constexpr int imageWidth = leftAxis + plotWidth + margin;
} // namespace metrics

// One component plane reduced to a bin distribution.
struct Channel
{
    QString name;
    QColor color;
    std::array<quint32, binCount> bins{};
    quint32 maxCount = 0;
    double mean = 0;
};

QColor planeColor(const QString &planeName)
{
    if (planeName == "Y"_L1)
        return QColor(90, 90, 90);
    if (planeName == "U"_L1)
        return QColor(70, 70, 220);
    if (planeName == "V"_L1)
        return QColor(220, 70, 70);
    if (planeName == "R"_L1)
        return QColor(220, 40, 40);
    // Gr / Gb are the two greens of a Bayer cell.
    if (planeName == "G"_L1 || planeName == "Gr"_L1 || planeName == "Gb"_L1)
        return QColor(40, 170, 40);
    if (planeName == "B"_L1)
        return QColor(60, 60, 230);
    return QColor(150, 150, 150);  // A / X
}

// Axis labels have to fit into 40-odd pixels, so large counts are
// abbreviated rather than grouped.
QString formatCount(quint32 count)
{
    if (count >= 1000000)
        return QString::number(count / 1000000.0, 'f', 1) + u'M';
    if (count >= 10000)
        return QString::number(count / 1000.0, 'f', 1) + u'k';
    return QLocale().toString(count);
}

QList<Channel> collectChannels(const RawImageDecoder &decoder, const QByteArray &data,
                               const RawImageLayout &layout)
{
    QList<Channel> channels;
    const QStringList planes = decoder.planeNames();
    for (int i = 0; i < planes.size(); ++i) {
        const auto planeImage = decoder.extractPlane(data, layout, i);
        if (!planeImage)
            continue;

        Channel channel;
        channel.name = planes.at(i);
        channel.color = planeColor(channel.name);

        for (int row = 0; row < planeImage->height(); ++row) {
            const uchar *line = planeImage->constScanLine(row);
            for (int col = 0; col < planeImage->width(); ++col)
                ++channel.bins[line[col]];
        }

        quint64 sum = 0;
        for (int bin = 0; bin < binCount; ++bin) {
            channel.maxCount = qMax(channel.maxCount, channel.bins[bin]);
            sum += quint64(bin) * channel.bins[bin];
        }
        const qint64 pixelCount = qint64(planeImage->width()) * planeImage->height();
        channel.mean = pixelCount > 0 ? double(sum) / double(pixelCount) : 0;
        channels.append(channel);
    }
    return channels;
}

void drawChannel(QPainter &painter, const Channel &channel, int top,
                 const QFont &titleFont, const QFont &tickFont)
{
    const int plotLeft = metrics::leftAxis;
    const int plotTop = top + metrics::titleHeight;
    const int plotBottom = plotTop + metrics::plotHeight;
    const auto xForBin = [plotLeft](int bin) { return plotLeft + bin * metrics::binWidth; };
    const auto heightForCount = [&channel](quint32 count) {
        if (channel.maxCount == 0)
            return 0;
        return qRound(count * qreal(metrics::plotHeight) / channel.maxCount);
    };

    painter.setFont(titleFont);
    painter.setPen(Qt::black);
    painter.drawText(QRect(plotLeft, top, metrics::plotWidth, metrics::titleHeight),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     Tr::tr("%1  (mean %2)").arg(channel.name).arg(channel.mean, 0, 'f', 1));

    painter.fillRect(plotLeft, plotTop, metrics::plotWidth, metrics::plotHeight,
                     QColor(248, 248, 248));
    painter.setPen(QPen(channel.color, 2));
    for (int bin = 0; bin < binCount; ++bin) {
        const int x = xForBin(bin);
        painter.drawLine(x, plotBottom, x, plotBottom - heightForCount(channel.bins[bin]));
    }

    painter.setPen(QColor(80, 80, 80));
    painter.drawLine(plotLeft, plotBottom, plotLeft + metrics::plotWidth, plotBottom);
    painter.drawLine(plotLeft, plotTop, plotLeft, plotBottom);

    painter.setFont(tickFont);
    static constexpr int valueTicks[] = {0, 64, 128, 192, binCount - 1};
    for (int value : valueTicks) {
        const int x = xForBin(value);
        painter.drawLine(x, plotBottom, x, plotBottom + 4);
        painter.drawText(QRect(x - 18, plotBottom + 5, 36, 14), Qt::AlignHCenter | Qt::AlignTop,
                         QString::number(value));
    }
    painter.drawText(QRect(plotLeft, plotBottom + 18, metrics::plotWidth, 16),
                     Qt::AlignHCenter | Qt::AlignTop, Tr::tr("Value"));

    QList<quint32> countTicks{0};
    if (channel.maxCount > 1)
        countTicks.append(channel.maxCount / 2);
    if (channel.maxCount > 0)
        countTicks.append(channel.maxCount);
    for (quint32 count : std::as_const(countTicks)) {
        const int y = plotBottom - heightForCount(count);
        painter.drawLine(plotLeft - 4, y, plotLeft, y);
        painter.drawText(QRect(16, y - 8, plotLeft - 22, 16), Qt::AlignRight | Qt::AlignVCenter,
                         formatCount(count));
    }

    painter.save();
    painter.translate(10, (plotTop + plotBottom) / 2);
    painter.rotate(-90);
    painter.drawText(QRect(-metrics::plotHeight / 2, -8, metrics::plotHeight, 16), Qt::AlignCenter,
                     Tr::tr("Count"));
    painter.restore();
}

} // namespace

QImage RawImageHistogram::render(const RawImageDecoder &decoder, const QByteArray &data,
                                 const RawImageLayout &layout)
{
    // extractPlane() indexes by the layout, so a buffer that does not
    // match it would be read out of bounds. There is no way to report an
    // error here, and a missing chart is harmless, so bail out quietly.
    if (!RawImageFrame::validate(&decoder, data, layout))
        return {};

    const QList<Channel> channels = collectChannels(decoder, data, layout);
    if (channels.isEmpty())
        return {};

    QImage image(metrics::imageWidth, metrics::margin + channels.size() * metrics::rowHeight,
                 QImage::Format_RGB32);
    if (image.isNull())
        return {};
    image.fill(Qt::white);

    QPainter painter(&image);
    const QFont titleFont = painter.font();
    QFont tickFont = titleFont;
    tickFont.setPointSizeF(qMax(7.0, tickFont.pointSizeF() - 1.0));

    for (int c = 0; c < channels.size(); ++c)
        drawChannel(painter, channels.at(c), metrics::margin + c * metrics::rowHeight,
                    titleFont, tickFont);

    return image;
}
