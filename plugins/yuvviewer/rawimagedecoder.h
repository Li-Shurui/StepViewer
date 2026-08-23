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

// Generic layout description for raw (uncompressed) image buffers.
// stride/scanline describe the first plane; planar formats may interpret
// them per plane in their validateLayout()/expectedByteSize() overrides.
struct RawImageLayout
{
    int width = 0;
    int height = 0;
    int stride = 0;
    int scanline = 0;
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

    RawImageDecoder() = default;
    virtual ~RawImageDecoder() = default;

    virtual QLatin1StringView id() const = 0;          // stable key, e.g. "nv12"
    virtual QString displayName() const = 0;           // UI name, e.g. "NV12"
    virtual QString mimeType() const = 0;
    virtual QStringList fileExtensions() const = 0;

    // Base implementation checks the dimension ranges only; formats with
    // alignment constraints (e.g. 4:2:0 chroma subsampling) override it.
    virtual LayoutResult validateLayout(const RawImageLayout &layout) const;
    virtual qint64 expectedByteSize(const RawImageLayout &layout) const = 0;
    virtual ImageResult convertToImage(const QByteArray &data,
                                       const RawImageLayout &layout) const = 0;

    // Shared file loading, size-checked against expectedByteSize().
    DataResult readData(const QString &fileName, const RawImageLayout &layout) const;
};

namespace RawImageDecoders {

const QList<const RawImageDecoder *> &all();
const RawImageDecoder *defaultDecoder();
const RawImageDecoder *findByExtension(const QString &extension);
const RawImageDecoder *findById(const QString &id);

} // namespace RawImageDecoders

#endif // RAWIMAGEDECODER_H
