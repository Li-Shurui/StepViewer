// Copyright (C) 2026
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#ifndef RAWIMAGEHISTOGRAM_H
#define RAWIMAGEHISTOGRAM_H

#include "rawimagedecoder.h"

#include <QByteArray>
#include <QImage>

namespace RawImageHistogram {

// Reduces every component plane of the frame to a 256-bin distribution
// and renders the distributions as a stack of labelled charts, one per
// plane. Returns a null image when the format exposes no planes or when
// data does not hold one frame of layout.
//
// Safe to call from a worker thread: painting onto a QImage does not
// require the GUI thread.
QImage render(const RawImageDecoder &decoder, const QByteArray &data,
              const RawImageLayout &layout);

} // namespace RawImageHistogram

#endif // RAWIMAGEHISTOGRAM_H
