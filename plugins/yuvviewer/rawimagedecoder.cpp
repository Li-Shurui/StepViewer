// Copyright (C) 2026
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#include "rawimagedecoder.h"

#include "rawimagedecoder_p.h"

#include <QFile>

#include <limits>
#include <new>

using namespace Qt::StringLiterals;

RawImageDecoder::LayoutResult RawImageDecoder::validateLayout(const RawImageLayout &layout) const
{
    if (layout.width < minimumDimension || layout.width > maximumDimension
        || layout.height < minimumDimension || layout.height > maximumDimension) {
        return std::unexpected(tr("Width and height must be between %1 and %2.")
                                   .arg(minimumDimension)
                                   .arg(maximumDimension));
    }
    if (layout.stride <= 0 || layout.scanline <= 0) {
        return std::unexpected(tr("Stride and scanline must both be positive."));
    }
    if (layout.stride > maximumStride || layout.scanline > maximumDimension) {
        return std::unexpected(tr("Stride must not exceed %1 and scanline must not exceed %2.")
                                   .arg(maximumStride)
                                   .arg(maximumDimension));
    }

    return layout;
}

int RawImageDecoder::defaultStride(int width) const
{
    return width;
}

RawImageDecoder::ImageResult RawImageDecoder::extractPlane(const QByteArray &,
                                                           const RawImageLayout &, int) const
{
    return std::unexpected(tr("%1 does not support separate plane viewing.")
                               .arg(displayName()));
}

QString RawImageDecoder::describePixel(const QByteArray &, const RawImageLayout &,
                                       int, int) const
{
    return {};
}

RawImageDecoder::DataResult RawImageDecoder::readData(const QString &fileName,
                                                      const RawImageLayout &layout,
                                                      qint64 frameIndex,
                                                      const ProgressCallback &progress) const
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        return std::unexpected(tr("Cannot open the file: %1")
                                   .arg(file.errorString()));
    }

    const qint64 frameSize = expectedByteSize(layout);
    const qint64 fileSize = file.size();
    if (frameSize <= 0) {
        return std::unexpected(tr("The calculated %1 frame size is invalid.")
                                   .arg(displayName()));
    }
    if (frameSize > std::numeric_limits<qsizetype>::max()) {
        return std::unexpected(tr("The %1 frame is too large to load into memory.")
                                   .arg(displayName()));
    }
    if (fileSize < frameSize || (fileSize % frameSize) != 0) {
        return std::unexpected(
            tr("File size does not match whole %1 frames. "
               "Frame size is %2 bytes, file size is %3 bytes "
               "(width=%4, height=%5, stride=%6, scanline=%7).")
                .arg(displayName())
                .arg(frameSize)
                .arg(fileSize)
                .arg(layout.width)
                .arg(layout.height)
                .arg(layout.stride)
                .arg(layout.scanline));
    }

    const qint64 frameCount = fileSize / frameSize;
    if (frameIndex < 0 || frameIndex >= frameCount) {
        return std::unexpected(tr("Frame %1 is out of range; the file contains %2 frames.")
                                   .arg(frameIndex + 1)
                                   .arg(frameCount));
    }

    if (!file.seek(frameIndex * frameSize)) {
        return std::unexpected(tr("Failed while reading the file: %1")
                                   .arg(file.errorString()));
    }

    constexpr qint64 chunkSize = 4 * 1024 * 1024;
    QByteArray data;
    try {
        data = QByteArray(static_cast<qsizetype>(frameSize), Qt::Uninitialized);
    } catch (const std::bad_alloc &) {
        return std::unexpected(tr("Not enough memory to allocate %1 bytes for the image frame.")
                                   .arg(frameSize));
    }
    if (data.size() != frameSize) {
        return std::unexpected(tr("Could not allocate %1 bytes for the image frame.")
                                   .arg(frameSize));
    }
    qint64 offset = 0;
    while (offset < frameSize) {
        const qint64 bytesRead = file.read(data.data() + offset,
                                           qMin(chunkSize, frameSize - offset));
        if (bytesRead < 0) {
            return std::unexpected(tr("Failed while reading the file: %1")
                                       .arg(file.errorString()));
        }
        if (bytesRead == 0)
            break;  // the file shrank mid-read; reported below
        offset += bytesRead;
        if (progress && !progress(offset, frameSize))
            return std::unexpected(tr("Loading canceled."));
    }

    if (offset != frameSize) {
        return std::unexpected(tr("The file read was incomplete. "
                                  "Expected %1 bytes, received %2 bytes.")
                                   .arg(frameSize)
                                   .arg(offset));
    }

    return data;
}

namespace RawImageDecoders {

const QList<const RawImageDecoder *> &all()
{
    // Never deleted; the registry lives as long as the plugin library.
    // The order is the order of the format combo box.
    static const QList<const RawImageDecoder *> decoders =
        createYuvDecoders() + createRgbDecoders() + createBayerDecoders();
    return decoders;
}

const RawImageDecoder *defaultDecoder()
{
    return all().constFirst();
}

const RawImageDecoder *findByExtension(const QString &extension)
{
    if (extension.isEmpty())
        return nullptr;

    for (const RawImageDecoder *decoder : all()) {
        const QStringList extensions = decoder->fileExtensions();
        for (const QString &candidate : extensions) {
            if (candidate.compare(extension, Qt::CaseInsensitive) == 0)
                return decoder;
        }
    }
    return nullptr;
}

const RawImageDecoder *findById(const QString &id)
{
    if (id.isEmpty())
        return nullptr;

    for (const RawImageDecoder *decoder : all()) {
        if (decoder->id() == id)
            return decoder;
    }
    return nullptr;
}

} // namespace RawImageDecoders
