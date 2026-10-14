// Copyright (C) 2026
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#include "rawimagehistogram.h"

#include "rawimageframe.h"

#include <vector>

namespace {

// The value of the sample at a 0-based rank in sorted order.
int valueAtRank(const quint32 *counts, int levels, qint64 rank)
{
    qint64 seen = 0;
    for (int value = 0; value < levels; ++value) {
        seen += counts[value];
        if (seen > rank)
            return value;
    }
    return levels - 1;
}

} // namespace

QList<RawImageHistogram::Channel> RawImageHistogram::compute(const RawImageDecoder &decoder,
                                                             const QByteArray &data,
                                                             const RawImageLayout &layout)
{
    // planeLayout() describes a frame of layout, so a buffer that does not
    // match it would be read out of bounds. There is no way to report an
    // error here, and a missing chart is harmless, so bail out quietly.
    if (!RawImageFrame::validate(&decoder, data, layout))
        return {};

    const auto *frame = reinterpret_cast<const uchar *>(data.constData());
    const QStringList planes = decoder.planeNames();
    QList<Channel> channels;
    for (int i = 0; i < planes.size(); ++i) {
        const std::optional<RawPlaneLayout> plane = decoder.planeLayout(layout, i);
        if (!plane || !plane->fitsIn(data.size()))
            continue;

        Channel channel;
        channel.name = planes.at(i);
        channel.bits = plane->bits;
        channel.counts.fill(0, plane->levels());

        // readRow() masks every sample to bits, so it always indexes a bin.
        quint32 *counts = channel.counts.data();
        std::vector<quint16> values(size_t(plane->width));
        for (int row = 0; row < plane->height; ++row) {
            plane->readRow(frame, row, values.data());
            for (const quint16 value : values)
                ++counts[value];
        }

        quint64 sum = 0;
        channel.minimum = -1;
        for (int value = 0; value < channel.levels(); ++value) {
            if (counts[value] == 0)
                continue;
            if (channel.minimum < 0)
                channel.minimum = value;
            channel.maximum = value;
            channel.total += counts[value];
            sum += quint64(value) * counts[value];
        }
        channel.minimum = qMax(channel.minimum, 0);
        if (channel.total > 0) {
            channel.mean = double(sum) / double(channel.total);

            double squares = 0;
            for (int value = channel.minimum; value <= channel.maximum; ++value) {
                const double deviation = value - channel.mean;
                squares += deviation * deviation * counts[value];
            }
            channel.variance = squares / double(channel.total);

            const int lowerMiddle = valueAtRank(counts, channel.levels(), (channel.total - 1) / 2);
            const int upperMiddle = valueAtRank(counts, channel.levels(), channel.total / 2);
            channel.median = (lowerMiddle + upperMiddle) / 2.0;
        }
        channels.append(channel);
    }
    return channels;
}
