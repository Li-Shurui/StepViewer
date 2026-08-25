// Copyright (C) 2026
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#include "rawimageframe.h"

#include <QCoreApplication>

namespace {

struct Tr
{
    Q_DECLARE_TR_FUNCTIONS(RawImageFrame)
};

} // namespace

qint64 RawImageFrame::count(const RawImageDecoder &decoder, const RawImageLayout &layout,
                            qint64 fileSize)
{
    const qint64 frameSize = decoder.expectedByteSize(layout);
    if (frameSize <= 0 || fileSize < frameSize || (fileSize % frameSize) != 0)
        return 0;
    return fileSize / frameSize;
}

std::expected<void, QString> RawImageFrame::validate(const RawImageDecoder *decoder,
                                                     const QByteArray &data,
                                                     const RawImageLayout &layout)
{
    if (!decoder)
        return std::unexpected(Tr::tr("No decoder is available for the loaded image."));

    const qint64 expectedSize = decoder->expectedByteSize(layout);
    if (expectedSize <= 0)
        return std::unexpected(Tr::tr("The selected format produced an invalid frame size."));

    if (data.size() != expectedSize) {
        return std::unexpected(Tr::tr(
            "Loaded data size (%1 bytes) does not match the %2 frame size (%3 bytes). "
            "The format may have changed while the image was loading.")
                                   .arg(data.size())
                                   .arg(decoder->displayName())
                                   .arg(expectedSize));
    }
    return {};
}

RawImageDecoder::ImageResult RawImageFrame::render(const RawImageDecoder *decoder,
                                                   const QByteArray &data,
                                                   const RawImageLayout &layout, int plane)
{
    const auto validData = validate(decoder, data, layout);
    if (!validData)
        return std::unexpected(validData.error());

    if (plane == compositePlane)
        return decoder->convertToImage(data, layout);
    return decoder->extractPlane(data, layout, plane);
}
