// Copyright (C) 2026
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#ifndef RAWIMAGEFRAME_H
#define RAWIMAGEFRAME_H

#include "rawimagedecoder.h"

#include <QByteArray>
#include <QString>

#include <expected>

// Frame-level operations shared by the loader, the plane switcher and the
// pixel probe. They all work on the triple (decoder, raw samples, layout)
// and exist mainly so that every consumer goes through the same
// consistency check before indexing into the buffer.
namespace RawImageFrame {

// Plane index denoting the converted RGB view rather than a single
// component plane.
constexpr int compositePlane = -1;

// Number of whole frames of the given layout in a file of fileSize
// bytes; 0 when the file size is not a positive multiple of the frame
// size.
qint64 count(const RawImageDecoder &decoder, const RawImageLayout &layout, qint64 fileSize);

// Verifies that data really holds one frame of layout as understood by
// decoder. Callers must succeed here before handing the buffer to
// convertToImage(), extractPlane() or describePixel(), all of which
// index by the layout and would read out of bounds on a mismatch.
std::expected<void, QString> validate(const RawImageDecoder *decoder, const QByteArray &data,
                                      const RawImageLayout &layout);

// Validates and then renders either the composite image or one plane.
RawImageDecoder::ImageResult render(const RawImageDecoder *decoder, const QByteArray &data,
                                   const RawImageLayout &layout, int plane);

} // namespace RawImageFrame

#endif // RAWIMAGEFRAME_H
