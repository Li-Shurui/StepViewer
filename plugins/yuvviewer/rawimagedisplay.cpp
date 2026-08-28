// Copyright (C) 2026
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#include "rawimagedisplay.h"

#include <QtMath>

#include <algorithm>
#include <array>
#include <vector>

namespace {

// Everything is reduced to one of three shapes before being touched:
// 8-bit grayscale for plane views, 8-bit RGBA, or 16-bit RGBA. That keeps
// one scanning routine instead of one per QImage format, and the extra
// alpha channel costs a cheap Qt conversion.
struct Shape
{
    int colorChannels = 3;    // channels the transform applies to
    int channelsPerPixel = 4; // including a trailing alpha it leaves alone
    int levels = 256;         // distinct values of one channel
};

// Percentile used by the auto level. Well below 100% so that hot pixels,
// which raw sensor frames always have a few of, cannot decide the gain.
constexpr double levelPercentile = 0.995;

// Gains outside these bounds mean the estimate was driven by noise or by a
// channel that is essentially empty, and applying them looks worse than
// doing nothing.
constexpr double minimumGain = 1.0 / 16;
constexpr double maximumGain = 256;
constexpr double maximumBalanceGain = 4;

struct Correction
{
    std::array<double, 3> gain = {1, 1, 1};
    double inverseGamma = 1;
};

double clampGain(double gain, double limit)
{
    return std::clamp(gain, 1.0 / limit, limit);
}

// Resolves the gains from per-channel histograms of the source values.
Correction resolveCorrection(const std::array<std::vector<quint64>, 3> &histograms,
                             const Shape &shape, const RawImageDisplayOptions &options)
{
    Correction correction;
    correction.inverseGamma = options.gamma > 0 ? 1.0 / options.gamma : 1.0;

    const int channels = shape.colorChannels;
    std::array<double, 3> mean = {0, 0, 0};
    quint64 total = 0;
    for (int c = 0; c < channels; ++c) {
        quint64 count = 0;
        quint64 sum = 0;
        for (int value = 0; value < shape.levels; ++value) {
            count += histograms[c][value];
            sum += histograms[c][value] * quint64(value);
        }
        mean[c] = count ? double(sum) / double(count) : 0;
        total = count;
    }
    if (total == 0)
        return correction;

    if (options.grayWorldBalance && channels == 3) {
        const double reference = (mean[0] + mean[1] + mean[2]) / 3;
        for (int c = 0; c < channels; ++c) {
            if (mean[c] > 0 && reference > 0)
                correction.gain[c] = clampGain(reference / mean[c], maximumBalanceGain);
        }
    }

    if (options.autoLevel) {
        // The level has to be decided after the balance, so the histogram
        // index is scaled by the gain that will be applied to it.
        double brightest = 0;
        for (int c = 0; c < channels; ++c) {
            const quint64 target = quint64(double(total) * levelPercentile);
            quint64 seen = 0;
            int percentileValue = shape.levels - 1;
            for (int value = 0; value < shape.levels; ++value) {
                seen += histograms[c][value];
                if (seen >= target) {
                    percentileValue = value;
                    break;
                }
            }
            brightest = std::max(brightest, percentileValue * correction.gain[c]);
        }
        if (brightest > 0) {
            const double level = clampGain((shape.levels - 1) / brightest, maximumGain);
            for (int c = 0; c < channels; ++c)
                correction.gain[c] = clampGain(correction.gain[c] * level, maximumGain);
        }
    }

    for (double &gain : correction.gain)
        gain = std::clamp(gain, minimumGain, maximumGain);
    return correction;
}

// Gain then gamma, folded into one table per channel so the pixel loop
// stays a lookup.
std::array<std::vector<int>, 3> buildLookups(const Correction &correction, const Shape &shape)
{
    const double maximum = shape.levels - 1;
    std::array<std::vector<int>, 3> lookups;
    for (int c = 0; c < shape.colorChannels; ++c) {
        lookups[c].resize(size_t(shape.levels));
        for (int value = 0; value < shape.levels; ++value) {
            double scaled = std::clamp(value * correction.gain[c] / maximum, 0.0, 1.0);
            if (!qFuzzyCompare(correction.inverseGamma, 1.0))
                scaled = std::pow(scaled, correction.inverseGamma);
            lookups[c][size_t(value)] = int(std::lround(scaled * maximum));
        }
    }
    return lookups;
}

template <typename Channel>
void gather(const QImage &image, const Shape &shape,
            std::array<std::vector<quint64>, 3> &histograms)
{
    for (int c = 0; c < shape.colorChannels; ++c)
        histograms[c].assign(size_t(shape.levels), 0);

    // A frame can be tens of megapixels and the gains only need to be
    // approximately right, so the statistics are sampled on a grid.
    const int step = std::max(1, int(std::sqrt(double(image.width()) * image.height() / 250000.0)));
    for (int y = 0; y < image.height(); y += step) {
        const auto *row = reinterpret_cast<const Channel *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); x += step) {
            const Channel *pixel = row + qsizetype(x) * shape.channelsPerPixel;
            for (int c = 0; c < shape.colorChannels; ++c)
                ++histograms[c][pixel[c]];
        }
    }
}

template <typename Channel>
void transform(QImage &image, const Shape &shape,
               const std::array<std::vector<int>, 3> &lookups)
{
    for (int y = 0; y < image.height(); ++y) {
        auto *row = reinterpret_cast<Channel *>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            Channel *pixel = row + qsizetype(x) * shape.channelsPerPixel;
            for (int c = 0; c < shape.colorChannels; ++c)
                pixel[c] = Channel(lookups[c][pixel[c]]);
        }
    }
}

template <typename Channel>
QImage run(QImage working, const Shape &shape, const RawImageDisplayOptions &options)
{
    std::array<std::vector<quint64>, 3> histograms;
    gather<Channel>(working, shape, histograms);
    const Correction correction = resolveCorrection(histograms, shape, options);
    transform<Channel>(working, shape, buildLookups(correction, shape));
    return working;
}

} // namespace

QImage RawImageDisplay::apply(const QImage &image, const RawImageDisplayOptions &options)
{
    if (image.isNull() || options.isIdentity())
        return image;

    // A plane view is a single channel, so there is nothing to balance
    // against; the gain and the gamma still apply.
    if (image.format() == QImage::Format_Grayscale8) {
        const Shape shape{1, 1, 256};
        return run<quint8>(image, shape, options);
    }

    // 16-bit sources keep their depth; everything else is cheaper to work
    // on at 8 bits, which is all it carries anyway.
    const bool deep = image.format() == QImage::Format_RGBA64
        || image.format() == QImage::Format_RGBX64
        || image.format() == QImage::Format_RGBA64_Premultiplied
        || image.format() == QImage::Format_Grayscale16;
    if (deep) {
        const Shape shape{3, 4, 65536};
        return run<quint16>(image.convertToFormat(QImage::Format_RGBA64), shape, options);
    }

    const Shape shape{3, 4, 256};
    return run<quint8>(image.convertToFormat(QImage::Format_RGBA8888), shape, options);
}
