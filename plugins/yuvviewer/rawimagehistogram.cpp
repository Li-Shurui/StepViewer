// Copyright (C) 2026
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#include "rawimagehistogram.h"

#include "rawimageframe.h"

#include <vector>

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
        channel.mean = channel.total > 0 ? double(sum) / double(channel.total) : 0;
        channels.append(channel);
    }
    return channels;
}
