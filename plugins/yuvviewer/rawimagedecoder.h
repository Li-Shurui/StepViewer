// Copyright (C) 2026
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#ifndef RAWIMAGEDECODER_H
#define RAWIMAGEDECODER_H

#include <QByteArray>
#include <QCoreApplication>
#include <QImage>
#include <QList>
#include <QString>
#include <QStringList>

#include <expected>
#include <functional>
#include <optional>

// Where the significant bits of a sample sit inside its 16-bit container.
//
// Formats with 8-bit samples have no container and ignore this. The 16-bit
// container formats need it because the same file can hold, say, 10-bit
// data either at the top of every 16-bit word (P010's convention, and what
// ISP outputs tend to use) or at the bottom (what sensor dumps tend to
// use), and reading one as the other is not a subtle error: right-aligned
// 10-bit data read as full-range 16-bit spans 1.5% of the range and shows
// up as a black frame.
struct RawSampleFormat
{
    int bits = 16;           // significant bits per sample
    bool msbAligned = true;  // at the top of the container, else the bottom

    // Container bits that carry no value; also the shift between the two
    // alignments.
    int padding() const { return 16 - bits; }
};

// Generic layout description for raw (uncompressed) image buffers.
// stride/scanline describe the first plane; planar formats may interpret
// them per plane in their validateLayout()/expectedByteSize() overrides.
struct RawImageLayout
{
    int width = 0;
    int height = 0;
    int stride = 0;
    int scanline = 0;

    // Only formats reporting a defaultSampleFormat() read this.
    RawSampleFormat sample;
};

// Abstract decoder for one raw pixel format (NV12, NV21, I420, RGB888, ...).
// Implementations are stateless singletons owned by the RawImageDecoders
// registry; add a new format by subclassing and registering it there.
class RawImageDecoder
{
    Q_DECLARE_TR_FUNCTIONS(RawImageDecoder)
    Q_DISABLE_COPY_MOVE(RawImageDecoder)

public:
    using LayoutResult = std::expected<RawImageLayout, QString>;
    using DataResult = std::expected<QByteArray, QString>;
    using ImageResult = std::expected<QImage, QString>;

    static constexpr int minimumDimension = 2;
    static constexpr int maximumDimension = 32768;
    // Stride is measured in bytes and may carry several bytes per pixel
    // (up to 8 for RGBA64).
    static constexpr int maximumStride = maximumDimension * 8;

    RawImageDecoder() = default;
    virtual ~RawImageDecoder() = default;

    virtual QLatin1StringView id() const = 0;          // stable key, e.g. "nv12"
    virtual QString displayName() const = 0;           // UI name, e.g. "NV12"
    virtual QString mimeType() const = 0;
    virtual QStringList fileExtensions() const = 0;

    // Base implementation checks the dimension ranges only; formats with
    // alignment constraints (e.g. 4:2:0 chroma subsampling) override it.
    virtual LayoutResult validateLayout(const RawImageLayout &layout) const;

    // Stride assumed when the file name does not specify one: the tight
    // row size of the first plane. Packed formats override this because
    // their rows carry more than one byte per pixel.
    virtual int defaultStride(int width) const;

    // Formats whose samples sit in 16-bit containers report the packing
    // they are conventionally written with. The UI preselects it and lets
    // the user override it in RawImageLayout::sample, because the file
    // itself does not say. Formats with 8-bit samples return nullopt and
    // ignore RawImageLayout::sample entirely.
    virtual std::optional<RawSampleFormat> defaultSampleFormat() const { return std::nullopt; }

    virtual qint64 expectedByteSize(const RawImageLayout &layout) const = 0;
    virtual ImageResult convertToImage(const QByteArray &data,
                                       const RawImageLayout &layout) const = 0;

    // Component plane access for separate plane viewing. planeNames()
    // lists the logical planes (e.g. {"Y","U","V"} or {"R","G","B","A"});
    // an empty list means plane viewing is not supported. extractPlane()
    // returns plane i as a Format_Grayscale8 image at its native
    // (possibly subsampled) resolution.
    virtual QStringList planeNames() const { return {}; }
    virtual ImageResult extractPlane(const QByteArray &data, const RawImageLayout &layout,
                                     int plane) const;

    // Pixel probe: describes the raw sample values of the pixel at (x,y)
    // in composite coordinates, e.g. "Y=128 U=90 V=200". The base
    // implementation returns an empty string (probe unavailable).
    virtual QString describePixel(const QByteArray &data, const RawImageLayout &layout,
                                  int x, int y) const;

    // Shared file loading, size-checked against expectedByteSize().
    // A file whose size is a multiple of the frame size is treated as a
    // multi-frame sequence; frameIndex selects which frame to read.
    // The progress variant reports bytesRead/totalBytes after each chunk;
    // the callback returns false to abort the read (cooperative cancel).
    using ProgressCallback = std::function<bool(qint64 bytesRead, qint64 totalBytes)>;
    DataResult readData(const QString &fileName, const RawImageLayout &layout) const
    {
        return readData(fileName, layout, 0, {});
    }
    DataResult readData(const QString &fileName, const RawImageLayout &layout,
                        const ProgressCallback &progress) const
    {
        return readData(fileName, layout, 0, progress);
    }
    DataResult readData(const QString &fileName, const RawImageLayout &layout,
                        qint64 frameIndex, const ProgressCallback &progress) const;
};

namespace RawImageDecoders {

const QList<const RawImageDecoder *> &all();
const RawImageDecoder *defaultDecoder();
const RawImageDecoder *findByExtension(const QString &extension);
const RawImageDecoder *findById(const QString &id);

} // namespace RawImageDecoders

#endif // RAWIMAGEDECODER_H
