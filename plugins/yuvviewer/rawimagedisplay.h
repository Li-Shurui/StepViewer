// Copyright (C) 2026
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#ifndef RAWIMAGEDISPLAY_H
#define RAWIMAGEDISPLAY_H

#include <QImage>
#include <QtGlobal>

// A display-only transform for a decoded frame.
//
// Sensor and ISP raw data is linear and often uses a small part of its
// range, which is faithful but close to unreadable: a 10-bit frame with a
// mean around a tenth of full scale, shown without gamma, is a dark green
// smear even when every sample in it is correct. These options make such a
// frame legible without pretending to be an ISP.
//
// Nothing here touches the sample buffer. The pixel probe and the
// histogram keep reading raw values, so the numbers a user reads off the
// image never depend on how it is being displayed.
struct RawImageDisplayOptions
{
    // Stretch the range so that a high percentile of the brightest channel
    // reaches full scale. Uses a percentile rather than the maximum so
    // that a few hot pixels cannot flatten the whole frame.
    bool autoLevel = false;

    // Equalize the channel means (gray-world). Raw data carries no white
    // balance, and the green filter passes the most light, so an untouched
    // frame reads as green. Content-dependent by nature, hence opt-in.
    bool grayWorldBalance = false;

    // Encoding gamma: 1 leaves the linear data alone, 2.2 approximates
    // what a display expects. Applied after the gains.
    qreal gamma = 1;

    bool isIdentity() const
    {
        return !autoLevel && !grayWorldBalance && qFuzzyCompare(gamma, qreal(1));
    }
};

namespace RawImageDisplay {

// Returns image transformed for display, or image itself when the options
// ask for nothing. Grayscale plane views are handled too, with the
// white balance skipped because a single channel has nothing to balance
// against.
QImage apply(const QImage &image, const RawImageDisplayOptions &options);

} // namespace RawImageDisplay

#endif // RAWIMAGEDISPLAY_H
