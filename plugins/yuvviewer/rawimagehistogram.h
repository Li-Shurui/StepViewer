// Copyright (C) 2026
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#ifndef RAWIMAGEHISTOGRAM_H
#define RAWIMAGEHISTOGRAM_H

#include "rawimagedecoder.h"

#include <QByteArray>
#include <QList>
#include <QString>

#include <cmath>

namespace RawImageHistogram {

// The distribution of one component plane over every value its samples
// can take: 2^bits counts, so 256 for 8-bit data and 65536 for 16-bit.
// The summary statistics are exact, taken over every sample of the plane.
struct Channel
{
    QString name;
    int bits = 8;
    QList<quint32> counts;  // counts[v]: samples with value v
    qint64 total = 0;
    double mean = 0;
    double median = 0;      // midway between the two middle samples for an even total
    double variance = 0;    // population variance
    int minimum = 0;
    int maximum = 0;

    int levels() const { return int(counts.size()); }
    double standardDeviation() const { return std::sqrt(variance); }
};

// Counts every component plane of the frame at its native bit depth,
// straight from the sample buffer. Returns an empty list when the format
// exposes no planes or when data does not hold one frame of layout.
//
// Pure computation, safe to call from a worker thread.
QList<Channel> compute(const RawImageDecoder &decoder, const QByteArray &data,
                       const RawImageLayout &layout);

} // namespace RawImageHistogram

#endif // RAWIMAGEHISTOGRAM_H
