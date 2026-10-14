// Copyright (C) 2026
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#include "yuvhistogramwidget.h"

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QHelpEvent>
#include <QLocale>
#include <QPaintEvent>
#include <QPainter>
#include <QPalette>
#include <QRect>
#include <QSizePolicy>
#include <QToolTip>

#include <algorithm>
#include <optional>

using namespace Qt::StringLiterals;

namespace {

using Channel = RawImageHistogram::Channel;

constexpr int margin = 8;
constexpr int tickLength = 4;
constexpr int plotHeight = 120;
constexpr int preferredPlotWidth = 512;
constexpr int minimumPlotWidth = 128;

// The text-dependent part of the geometry, derived from the widget font
// so that a larger font gets room instead of being clipped.
struct Metrics
{
    QFont titleFont;
    QFont nameFont;
    QFont tickFont;
    int titleHeight = 0;
    int leftAxis = 0;    // from the widget's left edge to the plot
    int bottomAxis = 0;  // from the plot's bottom edge to the row's margin
    int rowHeight = 0;
};

// Axis labels have to fit into 40-odd pixels, so large counts are
// abbreviated rather than grouped.
QString formatCount(quint64 count)
{
    if (count >= 1000000)
        return QString::number(count / 1000000.0, 'f', 1) + u'M';
    if (count >= 10000)
        return QString::number(count / 1000.0, 'f', 1) + u'k';
    return QLocale().toString(count);
}

// Width of the widest count label any bin of these channels can produce.
// No bin holds more than its plane's total, but a smaller count can still
// make a longer label ("9,999" against "10.0k", "1000.0k" against
// "1.5M"), so the longest label of every notation up to the total counts.
int countLabelWidth(const QList<Channel> &channels, const QFontMetrics &metrics)
{
    int width = metrics.horizontalAdvance(formatCount(0));
    for (const Channel &channel : channels) {
        const quint64 total = quint64(channel.total);
        for (const quint64 count : {total, qMin<quint64>(total, 9999), qMin<quint64>(total, 999999)})
            width = qMax(width, metrics.horizontalAdvance(formatCount(count)));
    }
    return width;
}

Metrics metricsFor(const QFont &font, const QList<Channel> &channels)
{
    Metrics metrics;
    metrics.titleFont = font;
    metrics.nameFont = font;
    metrics.nameFont.setBold(true);
    metrics.tickFont = font;
    if (font.pointSizeF() > 0)
        metrics.tickFont.setPointSizeF(qMax(7.0, font.pointSizeF() - 1.0));

    const QFontMetrics title(metrics.titleFont);
    const QFontMetrics tick(metrics.tickFont);
    metrics.titleHeight = title.height() + 4;
    // The rotated axis label, then the count labels.
    metrics.leftAxis = margin + tick.height() + 6 + countLabelWidth(channels, tick) + 3
                       + tickLength + 1;
    metrics.bottomAxis = tickLength + 1 + tick.height() + 2 + tick.height();
    metrics.rowHeight = metrics.titleHeight + plotHeight + metrics.bottomAxis + margin;
    return metrics;
}

QRect plotRect(const Metrics &metrics, int widgetWidth, int channel)
{
    const int top = margin + channel * metrics.rowHeight + metrics.titleHeight;
    return {metrics.leftAxis, top, qMax(1, widgetWidth - metrics.leftAxis - margin), plotHeight};
}

// Values per bin: the smallest power of two that keeps every bin at least
// a device pixel wide, so a high-DPI screen gets the finer histogram it
// can show. The level count is a power of two as well, so the bins tile
// the range exactly.
int valuesPerBin(int levels, const QRect &plot, qreal devicePixelRatio)
{
    const int pixels = qMax(1, int(plot.width() * devicePixelRatio));
    int group = 1;
    while (levels / group > pixels && group < levels)
        group *= 2;
    return group;
}

// In logical coordinates, which are fractional once a bin is narrower
// than a logical pixel.
qreal binEdge(const QRect &plot, int bin, int bins)
{
    return plot.left() + qreal(bin) * plot.width() / bins;
}

QList<quint64> groupCounts(const Channel &channel, int group)
{
    QList<quint64> bins(channel.levels() / group, 0);
    for (int value = 0; value < channel.levels(); ++value)
        bins[value / group] += channel.counts.at(value);
    return bins;
}

QColor mix(const QColor &from, const QColor &to, qreal amount)
{
    return QColor::fromRgbF(from.redF() + (to.redF() - from.redF()) * amount,
                            from.greenF() + (to.greenF() - from.greenF()) * amount,
                            from.blueF() + (to.blueF() - from.blueF()) * amount);
}

// Only Window and WindowText are reliable under the style sheet theme, so
// every other shade is mixed from those two.
struct Colors
{
    QColor text;
    QColor axis;
    QColor plotBackground;
    QColor neutral;
};

Colors colorsFor(const QPalette &palette)
{
    const QColor window = palette.color(QPalette::Window);
    const QColor text = palette.color(QPalette::WindowText);
    return {text, mix(window, text, 0.5), mix(window, text, 0.07), mix(window, text, 0.55)};
}

// Mid-luminance hues that read on both the light and the dark theme. Luma
// takes the text color, the neutral choice on any background.
QColor planeColor(const QString &planeName, const Colors &colors)
{
    if (planeName == "Y"_L1)
        return colors.text;
    if (planeName == "U"_L1 || planeName == "B"_L1)
        return QColor(66, 133, 244);
    if (planeName == "V"_L1 || planeName == "R"_L1)
        return QColor(234, 67, 53);
    // Gr / Gb are the two greens of a Bayer cell.
    if (planeName == "G"_L1 || planeName == "Gr"_L1 || planeName == "Gb"_L1)
        return QColor(52, 168, 83);
    return colors.neutral;  // A / X
}

void drawChannel(QPainter &painter, const Channel &channel, const QRect &plot,
                 int widgetWidth, qreal devicePixelRatio, const Metrics &metrics,
                 const Colors &colors)
{
    const QFontMetrics nameMetrics(metrics.nameFont);
    const QFontMetrics titleMetrics(metrics.titleFont);
    const QFontMetrics tickMetrics(metrics.tickFont);
    const int axisY = plot.top() + plot.height();

    QRect title(plot.left(), plot.top() - metrics.titleHeight, plot.width(), metrics.titleHeight);
    painter.setPen(colors.text);
    painter.setFont(metrics.nameFont);
    painter.drawText(title, Qt::AlignLeft | Qt::AlignVCenter, channel.name);
    title.setLeft(title.left() + nameMetrics.horizontalAdvance(channel.name)
                  + titleMetrics.horizontalAdvance(u"   "_s));
    const QString statistics = YuvHistogramWidget::tr("mean %1   min %2   max %3")
                                   .arg(channel.mean, 0, 'f', 1)
                                   .arg(channel.minimum)
                                   .arg(channel.maximum);
    painter.setFont(metrics.titleFont);
    painter.drawText(title, Qt::AlignLeft | Qt::AlignVCenter,
                     titleMetrics.elidedText(statistics, Qt::ElideRight, title.width()));

    painter.fillRect(plot, colors.plotBackground);
    const int group = valuesPerBin(channel.levels(), plot, devicePixelRatio);
    const QList<quint64> bins = groupCounts(channel, group);
    const quint64 peak = *std::max_element(bins.cbegin(), bins.cend());
    const auto heightFor = [peak](quint64 count) {
        if (count == 0 || peak == 0)
            return 0;
        // A bin that holds anything stays visible next to a tall peak.
        return qMax(1, int(qRound64(double(count) * plotHeight / double(peak))));
    };
    const QColor barColor = planeColor(channel.name, colors);
    for (int bin = 0; bin < bins.size(); ++bin) {
        const int barHeight = heightFor(bins.at(bin));
        if (barHeight == 0)
            continue;
        const qreal left = binEdge(plot, bin, bins.size());
        const qreal right = binEdge(plot, bin + 1, bins.size());
        painter.fillRect(QRectF(left, axisY - barHeight, right - left, barHeight), barColor);
    }

    painter.setPen(colors.axis);
    painter.drawLine(plot.left() - 1, axisY, plot.left() + plot.width(), axisY);
    painter.drawLine(plot.left() - 1, plot.top(), plot.left() - 1, axisY);

    // Ticks at the quarters of the range, thinned out when the labels
    // would run into each other.
    painter.setFont(metrics.tickFont);
    const int maximum = channel.levels() - 1;
    const int labelSpace = tickMetrics.horizontalAdvance(QString::number(maximum)) + 12;
    int divisions = 4;
    while (divisions > 1 && plot.width() / divisions < labelSpace)
        divisions /= 2;
    for (int i = 0; i <= divisions; ++i) {
        const int value = i == divisions ? maximum : channel.levels() / divisions * i;
        const int x = plot.left() + int((value + 0.5) * plot.width() / channel.levels());
        painter.setPen(colors.axis);
        painter.drawLine(x, axisY, x, axisY + tickLength);

        const QString label = QString::number(value);
        const int labelWidth = tickMetrics.horizontalAdvance(label);
        const int labelLeft = std::clamp(x - labelWidth / 2, 0, qMax(0, widgetWidth - labelWidth));
        painter.setPen(colors.text);
        painter.drawText(QRect(labelLeft, axisY + tickLength + 1, labelWidth, tickMetrics.height()),
                         Qt::AlignLeft | Qt::AlignTop, label);
    }
    painter.drawText(QRect(plot.left(), axisY + tickLength + 1 + tickMetrics.height() + 2,
                           plot.width(), tickMetrics.height()),
                     Qt::AlignHCenter | Qt::AlignTop,
                     YuvHistogramWidget::tr("Value (%1 bit)").arg(channel.bits));

    QList<quint64> countTicks{0};
    if (peak > 1)
        countTicks.append(peak / 2);
    if (peak > 0)
        countTicks.append(peak);
    for (const quint64 count : std::as_const(countTicks)) {
        const int y = axisY - heightFor(count);
        painter.setPen(colors.axis);
        painter.drawLine(plot.left() - 1 - tickLength, y, plot.left() - 1, y);
        painter.setPen(colors.text);
        painter.drawText(QRect(0, y - tickMetrics.height() / 2,
                               plot.left() - 1 - tickLength - 3, tickMetrics.height()),
                         Qt::AlignRight | Qt::AlignVCenter, formatCount(count));
    }

    painter.save();
    painter.translate(margin + tickMetrics.height() / 2, plot.top() + plot.height() / 2);
    painter.rotate(-90);
    painter.drawText(QRect(-plotHeight / 2, -tickMetrics.height() / 2, plotHeight,
                           tickMetrics.height()),
                     Qt::AlignCenter, YuvHistogramWidget::tr("Count"));
    painter.restore();
}

struct HoveredBin
{
    int channel = 0;
    int first = 0;
    int last = 0;
    quint64 count = 0;
    QRect rect;
};

std::optional<HoveredBin> binAt(const QList<Channel> &channels, const Metrics &metrics,
                                int widgetWidth, qreal devicePixelRatio, QPoint pos)
{
    for (int c = 0; c < channels.size(); ++c) {
        const QRect plot = plotRect(metrics, widgetWidth, c);
        if (!plot.contains(pos))
            continue;

        // Measured from the pixel's center, the same point the aliased bar
        // fill tests a pixel against.
        const Channel &channel = channels.at(c);
        const int group = valuesPerBin(channel.levels(), plot, devicePixelRatio);
        const int bins = channel.levels() / group;
        const qreal position = (pos.x() + 0.5 - plot.left()) * bins / plot.width();
        const int bin = std::clamp(int(position), 0, bins - 1);

        HoveredBin hovered;
        hovered.channel = c;
        hovered.first = bin * group;
        hovered.last = hovered.first + group - 1;
        for (int value = hovered.first; value <= hovered.last; ++value)
            hovered.count += channel.counts.at(value);
        const qreal left = binEdge(plot, bin, bins);
        hovered.rect = QRectF(left, plot.top(), binEdge(plot, bin + 1, bins) - left,
                              plot.height()).toAlignedRect();
        return hovered;
    }
    return std::nullopt;
}

QString describeBin(const Channel &channel, const HoveredBin &bin)
{
    const QLocale locale;
    const QString values = bin.first == bin.last
        ? YuvHistogramWidget::tr("%1 = %2").arg(channel.name).arg(bin.first)
        : YuvHistogramWidget::tr("%1 = %2 to %3").arg(channel.name).arg(bin.first).arg(bin.last);
    const double share = channel.total > 0 ? 100.0 * double(bin.count) / double(channel.total) : 0;
    return values + u'\n'
        + YuvHistogramWidget::tr("%1 samples (%2%)")
              .arg(locale.toString(bin.count), locale.toString(share, 'f', 2));
}

} // namespace

YuvHistogramWidget::YuvHistogramWidget(QWidget *parent) : QWidget(parent)
{
    // The enclosing QScrollArea has widgetResizable set: the charts follow
    // the viewport width down to minimumSizeHint(), where the scroll bar
    // takes over, and are always exactly as tall as their content.
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    updateSizeConstraints();
}

void YuvHistogramWidget::setChannels(QList<RawImageHistogram::Channel> channels)
{
    m_channels = std::move(channels);
    updateSizeConstraints();
    update();
}

void YuvHistogramWidget::clear()
{
    setChannels({});
}

QSize YuvHistogramWidget::sizeHint() const
{
    return {metricsFor(font(), m_channels).leftAxis + preferredPlotWidth + margin,
            contentHeight()};
}

QSize YuvHistogramWidget::minimumSizeHint() const
{
    return {metricsFor(font(), m_channels).leftAxis + minimumPlotWidth + margin,
            contentHeight()};
}

bool YuvHistogramWidget::event(QEvent *event)
{
    if (event->type() == QEvent::ToolTip) {
        const auto *help = static_cast<QHelpEvent *>(event);
        const std::optional<HoveredBin> bin = binAt(m_channels, metricsFor(font(), m_channels),
                                                    width(), devicePixelRatioF(), help->pos());
        if (!bin) {
            QToolTip::hideText();
            event->ignore();
            return true;
        }
        // Passing the bin's rectangle hides the tip as soon as the cursor
        // moves on to the next bin, which then gets a tip of its own.
        QToolTip::showText(help->globalPos(), describeBin(m_channels.at(bin->channel), *bin),
                           this, bin->rect);
        return true;
    }
    return QWidget::event(event);
}

void YuvHistogramWidget::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange)
        updateSizeConstraints();
    QWidget::changeEvent(event);
}

void YuvHistogramWidget::paintEvent(QPaintEvent *event)
{
    if (m_channels.isEmpty())
        return;

    const Metrics metrics = metricsFor(font(), m_channels);
    const Colors colors = colorsFor(palette());
    QPainter painter(this);
    for (int c = 0; c < m_channels.size(); ++c) {
        const QRect row(0, margin + c * metrics.rowHeight, width(), metrics.rowHeight);
        if (event->rect().intersects(row)) {
            drawChannel(painter, m_channels.at(c), plotRect(metrics, width(), c), width(),
                        devicePixelRatioF(), metrics, colors);
        }
    }
}

int YuvHistogramWidget::contentHeight() const
{
    if (m_channels.isEmpty())
        return 0;
    return margin + int(m_channels.size()) * metricsFor(font(), m_channels).rowHeight;
}

void YuvHistogramWidget::updateSizeConstraints()
{
    // Both depend on the data, and the enclosing scroll area only notices
    // a change through the resize these setters cause; updateGeometry()
    // alone would not reach it.
    setMinimumWidth(minimumSizeHint().width());
    setFixedHeight(contentHeight());
}
