// Copyright (C) 2026
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

// Decoders for single-plane formats: packed RGB at 8 and 16 bits per
// channel, the bit-packed 16-bit variants (RGB565 and friends), and
// grayscale Y8 as the degenerate one-channel case.

#include "rawimagedecoder_p.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <array>
#include <cstring>
#include <limits>
#include <new>

using namespace Qt::StringLiterals;
using namespace RawImageDecoderHelpers;

namespace {

// Shared implementation for packed RGB formats: a single plane of
// fixed-size pixels. Byte-ordered layouts map directly onto a QImage
// format (no conversion); the rest go through an OpenCV conversion of
// the CV_8UC(n) view (e.g. BGRA, which QImage cannot wrap directly).
class PackedRgbDecoder : public RawImageDecoder
{
    Q_DECLARE_TR_FUNCTIONS(PackedRgbDecoder)

public:
    LayoutResult validateLayout(const RawImageLayout &layout) const override
    {
        const LayoutResult baseResult = RawImageDecoder::validateLayout(layout);
        if (!baseResult)
            return baseResult;

        if (layout.stride < layout.width * bytesPerPixel()) {
            return std::unexpected(tr("%1 stride must be at least the width times %2 bytes. "
                                      "Received width %3, stride %4.")
                                       .arg(displayName())
                                       .arg(bytesPerPixel())
                                       .arg(layout.width)
                                       .arg(layout.stride));
        }
        if (layout.scanline < layout.height) {
            return std::unexpected(tr("%1 scanline must be at least the height. "
                                      "Received height %2, scanline %3.")
                                       .arg(displayName())
                                       .arg(layout.height)
                                       .arg(layout.scanline));
        }

        return layout;
    }

    qint64 expectedByteSize(const RawImageLayout &layout) const override
    {
        // stride is in bytes and already includes bytesPerPixel().
        return qint64(layout.stride) * qint64(layout.scanline);
    }

    int defaultStride(int width) const override { return width * bytesPerPixel(); }

    ImageResult convertToImage(const QByteArray &data,
                               const RawImageLayout &layout) const override
    {
        if (imageFormat() != QImage::Format_Invalid) {
            const auto *pixels = reinterpret_cast<const uchar *>(data.constData());
            const QImage wrappedImage(pixels, layout.width, layout.height,
                                      static_cast<qsizetype>(layout.stride), imageFormat());
            QImage image = wrappedImage.copy();
            if (image.isNull())
                return std::unexpected(tr("Could not allocate the converted QImage."));
            return image;
        }

        return runConversion(*this, [&]() -> ImageResult {
            auto *pixels = reinterpret_cast<uchar *>(const_cast<char *>(data.constData()));
            cv::Mat src(layout.height, layout.width, CV_MAKETYPE(CV_8U, bytesPerPixel()),
                        pixels, static_cast<size_t>(layout.stride));
            cv::Mat converted;
            cv::cvtColor(src, converted, conversionCode());
            return rgbaMatToImage(converted, layout, convertedFormat());
        });
    }

    QStringList planeNames() const override
    {
        QStringList names{QStringLiteral("R"), QStringLiteral("G"), QStringLiteral("B")};
        if (bytesPerPixel() == 4)
            names << QString(fourthChannelName());
        return names;
    }

    ImageResult extractPlane(const QByteArray &data, const RawImageLayout &layout,
                             int plane) const override
    {
        if (plane < 0 || plane >= planeNames().size())
            return invalidPlane(plane);

        const auto *pixels = reinterpret_cast<const uchar *>(data.constData());
        const int byteOffset = channelByteOffset(plane);
        if (byteOffset >= 0) {
            return stridedPlane(pixels, layout.width, layout.height, layout.stride,
                                bytesPerPixel(), byteOffset);
        }

        quint16 mask = 0;
        int shift = 0;
        int bits = 0;
        if (channelBitLayout(plane, mask, shift, bits)) {
            return rgb16Plane(pixels, layout.width, layout.height, layout.stride,
                              mask, shift, bits);
        }
        return invalidPlane(plane);
    }

    QString describePixel(const QByteArray &data, const RawImageLayout &layout,
                          int x, int y) const override
    {
        const auto *pixels = reinterpret_cast<const uchar *>(data.constData());
        const uchar *row = pixels + qint64(y) * layout.stride;
        if (bytesPerPixel() != 2) {
            const uchar *pixel = row + x * bytesPerPixel();
            const int r = pixel[channelByteOffset(0)];
            const int g = pixel[channelByteOffset(1)];
            const int b = pixel[channelByteOffset(2)];
            if (bytesPerPixel() == 4)
                return describeRgba(r, g, b, pixel[channelByteOffset(3)]);
            return describeRgb(r, g, b);
        }

        quint16 pixel;
        memcpy(&pixel, row + 2 * x, sizeof(pixel));
        int channels[3] = {0, 0, 0};
        for (int channel = 0; channel < 3; ++channel) {
            quint16 mask = 0;
            int shift = 0;
            int bits = 0;
            if (channelBitLayout(channel, mask, shift, bits)) {
                const int value = (pixel & mask) >> shift;
                const int maxValue = (1 << bits) - 1;
                channels[channel] = (value * 255 + maxValue / 2) / maxValue;
            }
        }
        return describeRgb(channels[0], channels[1], channels[2]);
    }

protected:
    virtual int bytesPerPixel() const = 0;

    // Byte-ordered layouts map directly onto this QImage format.
    // Return Format_Invalid to use the OpenCV conversion path instead.
    virtual QImage::Format imageFormat() const = 0;

    // OpenCV path: conversion code applied to the CV_8UC(n) view and
    // the QImage format of the converted output.
    virtual int conversionCode() const { return -1; }
    virtual QImage::Format convertedFormat() const { return QImage::Format_RGBA8888; }

    // Name of the fourth channel in 4-byte formats: alpha or padding.
    virtual QLatin1StringView fourthChannelName() const { return "A"_L1; }

    // 8-bit formats: byte offset of channel c (0=R, 1=G, 2=B, 3=A/X)
    // within a pixel, or -1 if the channel is absent or bit-packed.
    virtual int channelByteOffset(int channel) const = 0;

    // 16-bit bit-packed formats (RGB565 and friends): mask, shift and
    // bit count of channel c. Returns false for byte-ordered formats.
    virtual bool channelBitLayout(int channel, quint16 &mask, int &shift, int &bits) const
    {
        Q_UNUSED(channel);
        Q_UNUSED(mask);
        Q_UNUSED(shift);
        Q_UNUSED(bits);
        return false;
    }
};

class Rgb888Decoder final : public PackedRgbDecoder
{
public:
    QLatin1StringView id() const override { return "rgb888"_L1; }
    QString displayName() const override { return QStringLiteral("RGB888"); }
    QString mimeType() const override { return "video/x-raw-rgb888"_L1; }
    QStringList fileExtensions() const override
    {
        return {"rgb888"_L1, "RGB888"_L1, "rgb"_L1, "RGB"_L1};
    }

protected:
    int bytesPerPixel() const override { return 3; }
    QImage::Format imageFormat() const override { return QImage::Format_RGB888; }
    int channelByteOffset(int channel) const override
    {
        constexpr int offsets[] = {0, 1, 2, -1};
        return offsets[channel];
    }
};

class Bgr888Decoder final : public PackedRgbDecoder
{
public:
    QLatin1StringView id() const override { return "bgr888"_L1; }
    QString displayName() const override { return QStringLiteral("BGR888"); }
    QString mimeType() const override { return "video/x-raw-bgr888"_L1; }
    QStringList fileExtensions() const override
    {
        return {"bgr888"_L1, "BGR888"_L1, "bgr"_L1, "BGR"_L1};
    }

protected:
    int bytesPerPixel() const override { return 3; }
    QImage::Format imageFormat() const override { return QImage::Format_BGR888; }
    int channelByteOffset(int channel) const override
    {
        constexpr int offsets[] = {2, 1, 0, -1};
        return offsets[channel];
    }
};

class Rgba8888Decoder final : public PackedRgbDecoder
{
public:
    QLatin1StringView id() const override { return "rgba8888"_L1; }
    QString displayName() const override { return QStringLiteral("RGBA8888"); }
    QString mimeType() const override { return "video/x-raw-rgba8888"_L1; }
    QStringList fileExtensions() const override
    {
        return {"rgba8888"_L1, "RGBA8888"_L1, "rgba"_L1, "RGBA"_L1};
    }

protected:
    int bytesPerPixel() const override { return 4; }
    QImage::Format imageFormat() const override { return QImage::Format_RGBA8888; }
    int channelByteOffset(int channel) const override
    {
        constexpr int offsets[] = {0, 1, 2, 3};
        return offsets[channel];
    }
};

class Rgbx8888Decoder final : public PackedRgbDecoder
{
public:
    QLatin1StringView id() const override { return "rgbx8888"_L1; }
    QString displayName() const override { return QStringLiteral("RGBX8888"); }
    QString mimeType() const override { return "video/x-raw-rgbx8888"_L1; }
    QStringList fileExtensions() const override
    {
        return {"rgbx8888"_L1, "RGBX8888"_L1, "rgbx"_L1, "RGBX"_L1};
    }

protected:
    int bytesPerPixel() const override { return 4; }
    QImage::Format imageFormat() const override { return QImage::Format_RGBX8888; }
    QLatin1StringView fourthChannelName() const override { return "X"_L1; }
    int channelByteOffset(int channel) const override
    {
        constexpr int offsets[] = {0, 1, 2, 3};
        return offsets[channel];
    }
};

class Bgra8888Decoder final : public PackedRgbDecoder
{
public:
    QLatin1StringView id() const override { return "bgra8888"_L1; }
    QString displayName() const override { return QStringLiteral("BGRA8888"); }
    QString mimeType() const override { return "video/x-raw-bgra8888"_L1; }
    QStringList fileExtensions() const override
    {
        return {"bgra8888"_L1, "BGRA8888"_L1, "bgra"_L1, "BGRA"_L1};
    }

protected:
    int bytesPerPixel() const override { return 4; }
    // QImage has no byte-ordered BGRA format; swap channels via OpenCV.
    QImage::Format imageFormat() const override { return QImage::Format_Invalid; }
    int conversionCode() const override { return cv::COLOR_BGRA2RGBA; }
    int channelByteOffset(int channel) const override
    {
        constexpr int offsets[] = {2, 1, 0, 3};
        return offsets[channel];
    }
};

class Bgrx8888Decoder final : public PackedRgbDecoder
{
public:
    QLatin1StringView id() const override { return "bgrx8888"_L1; }
    QString displayName() const override { return QStringLiteral("BGRX8888"); }
    QString mimeType() const override { return "video/x-raw-bgrx8888"_L1; }
    QStringList fileExtensions() const override
    {
        return {"bgrx8888"_L1, "BGRX8888"_L1, "bgrx"_L1, "BGRX"_L1};
    }

protected:
    int bytesPerPixel() const override { return 4; }
    // QImage has no byte-ordered BGRX format; convert via OpenCV,
    // dropping the padding byte instead of leaking it into the alpha.
    QImage::Format imageFormat() const override { return QImage::Format_Invalid; }
    int conversionCode() const override { return cv::COLOR_BGRA2RGB; }
    QImage::Format convertedFormat() const override { return QImage::Format_RGB888; }
    QLatin1StringView fourthChannelName() const override { return "X"_L1; }
    int channelByteOffset(int channel) const override
    {
        constexpr int offsets[] = {2, 1, 0, 3};
        return offsets[channel];
    }
};

class Rgb565Decoder final : public PackedRgbDecoder
{
public:
    QLatin1StringView id() const override { return "rgb565"_L1; }
    QString displayName() const override { return QStringLiteral("RGB565"); }
    QString mimeType() const override { return "video/x-raw-rgb565"_L1; }
    QStringList fileExtensions() const override { return {"rgb565"_L1, "RGB565"_L1}; }

protected:
    // Little-endian 16-bit R:G:B 5:6:5 samples match QImage::Format_RGB16.
    int bytesPerPixel() const override { return 2; }
    QImage::Format imageFormat() const override { return QImage::Format_RGB16; }
    int channelByteOffset(int) const override { return -1; }
    bool channelBitLayout(int channel, quint16 &mask, int &shift, int &bits) const override
    {
        struct ChannelBits { quint16 mask; int shift; int bits; };
        constexpr ChannelBits channels[] = {{0xF800, 11, 5}, {0x07E0, 5, 6}, {0x001F, 0, 5}};
        if (channel < 0 || channel > 2)
            return false;
        mask = channels[channel].mask;
        shift = channels[channel].shift;
        bits = channels[channel].bits;
        return true;
    }
};

class Bgr565Decoder final : public PackedRgbDecoder
{
public:
    QLatin1StringView id() const override { return "bgr565"_L1; }
    QString displayName() const override { return QStringLiteral("BGR565"); }
    QString mimeType() const override { return "video/x-raw-bgr565"_L1; }
    QStringList fileExtensions() const override { return {"bgr565"_L1, "BGR565"_L1}; }

protected:
    int bytesPerPixel() const override { return 2; }
    // QImage has no BGR565 format; unpack via OpenCV.
    QImage::Format imageFormat() const override { return QImage::Format_Invalid; }
    int conversionCode() const override { return cv::COLOR_BGR5652RGBA; }
    int channelByteOffset(int) const override { return -1; }
    bool channelBitLayout(int channel, quint16 &mask, int &shift, int &bits) const override
    {
        struct ChannelBits { quint16 mask; int shift; int bits; };
        constexpr ChannelBits channels[] = {{0x001F, 0, 5}, {0x07E0, 5, 6}, {0xF800, 11, 5}};
        if (channel < 0 || channel > 2)
            return false;
        mask = channels[channel].mask;
        shift = channels[channel].shift;
        bits = channels[channel].bits;
        return true;
    }
};

class Rgb555Decoder final : public PackedRgbDecoder
{
public:
    QLatin1StringView id() const override { return "rgb555"_L1; }
    QString displayName() const override { return QStringLiteral("RGB555"); }
    QString mimeType() const override { return "video/x-raw-rgb555"_L1; }
    QStringList fileExtensions() const override { return {"rgb555"_L1, "RGB555"_L1}; }

protected:
    // Little-endian 16-bit R:G:B 5:5:5 samples match QImage::Format_RGB555.
    int bytesPerPixel() const override { return 2; }
    QImage::Format imageFormat() const override { return QImage::Format_RGB555; }
    int channelByteOffset(int) const override { return -1; }
    bool channelBitLayout(int channel, quint16 &mask, int &shift, int &bits) const override
    {
        struct ChannelBits { quint16 mask; int shift; int bits; };
        constexpr ChannelBits channels[] = {{0x7C00, 10, 5}, {0x03E0, 5, 5}, {0x001F, 0, 5}};
        if (channel < 0 || channel > 2)
            return false;
        mask = channels[channel].mask;
        shift = channels[channel].shift;
        bits = channels[channel].bits;
        return true;
    }
};

class Bgr555Decoder final : public PackedRgbDecoder
{
public:
    QLatin1StringView id() const override { return "bgr555"_L1; }
    QString displayName() const override { return QStringLiteral("BGR555"); }
    QString mimeType() const override { return "video/x-raw-bgr555"_L1; }
    QStringList fileExtensions() const override { return {"bgr555"_L1, "BGR555"_L1}; }

protected:
    int bytesPerPixel() const override { return 2; }
    // QImage has no BGR555 format; unpack via OpenCV.
    QImage::Format imageFormat() const override { return QImage::Format_Invalid; }
    int conversionCode() const override { return cv::COLOR_BGR5552RGBA; }
    int channelByteOffset(int) const override { return -1; }
    bool channelBitLayout(int channel, quint16 &mask, int &shift, int &bits) const override
    {
        struct ChannelBits { quint16 mask; int shift; int bits; };
        constexpr ChannelBits channels[] = {{0x001F, 0, 5}, {0x03E0, 5, 5}, {0x7C00, 10, 5}};
        if (channel < 0 || channel > 2)
            return false;
        mask = channels[channel].mask;
        shift = channels[channel].shift;
        bits = channels[channel].bits;
        return true;
    }
};

// Shared implementation for packed 16-bit-per-channel RGB formats
// (RGB48/RGBA64): a single plane of little-endian 16-bit samples.
// Unlike PackedRgbDecoder, channels are whole 16-bit samples, so plane
// extraction and the pixel probe work on sample offsets.
class PackedRgb16Decoder : public RawImageDecoder
{
    Q_DECLARE_TR_FUNCTIONS(PackedRgb16Decoder)

public:
    LayoutResult validateLayout(const RawImageLayout &layout) const override
    {
        const LayoutResult baseResult = RawImageDecoder::validateLayout(layout);
        if (!baseResult)
            return baseResult;

        if (layout.stride < layout.width * bytesPerPixel()) {
            return std::unexpected(tr("%1 stride must be at least the width times %2 bytes. "
                                      "Received width %3, stride %4.")
                                       .arg(displayName())
                                       .arg(bytesPerPixel())
                                       .arg(layout.width)
                                       .arg(layout.stride));
        }
        if (layout.scanline < layout.height) {
            return std::unexpected(tr("%1 scanline must be at least the height. "
                                      "Received height %2, scanline %3.")
                                       .arg(displayName())
                                       .arg(layout.height)
                                       .arg(layout.scanline));
        }

        return layout;
    }

    qint64 expectedByteSize(const RawImageLayout &layout) const override
    {
        return qint64(layout.stride) * qint64(layout.scanline);
    }

    int defaultStride(int width) const override { return width * bytesPerPixel(); }

    std::optional<RawSampleFormat> defaultSampleFormat() const override
    {
        return RawSampleFormat{};
    }

    QStringList planeNames() const override
    {
        QStringList names{QStringLiteral("R"), QStringLiteral("G"), QStringLiteral("B")};
        if (bytesPerPixel() == 8)
            names << QStringLiteral("A");
        return names;
    }

    ImageResult extractPlane(const QByteArray &data, const RawImageLayout &layout,
                             int plane) const override
    {
        if (plane < 0 || plane >= planeNames().size())
            return invalidPlane(plane);
        const auto *pixels = reinterpret_cast<const uchar *>(data.constData());
        return strided16Plane(pixels, layout.width, layout.height, layout.stride,
                              bytesPerPixel() / 2, plane, layout.sample);
    }

    QString describePixel(const QByteArray &data, const RawImageLayout &layout,
                          int x, int y) const override
    {
        const auto *pixels = reinterpret_cast<const uchar *>(data.constData());
        const uchar *pixel = pixels + qint64(y) * layout.stride + x * bytesPerPixel();
        const int r = rawSampleValue(readLe16(pixel), layout.sample);
        const int g = rawSampleValue(readLe16(pixel + 2), layout.sample);
        const int b = rawSampleValue(readLe16(pixel + 4), layout.sample);
        if (bytesPerPixel() == 8)
            return describeRgba(r, g, b, rawSampleValue(readLe16(pixel + 6), layout.sample));
        return describeRgb(r, g, b);
    }

protected:
    // 6 for RGB48, 8 for RGBA64.
    virtual int bytesPerPixel() const = 0;

    // Samples spanning the full 16-bit range, which is what QImage and the
    // OpenCV conversions assume. Right-aligned samples are scaled up into
    // scratch; samples that already span the range are used in place, so
    // the common case copies nothing. The result is valid while scratch is.
    const uchar *fullRangeRows(const QByteArray &data, const RawImageLayout &layout,
                               QByteArray &scratch, int &rowBytes) const
    {
        const auto *pixels = reinterpret_cast<const uchar *>(data.constData());
        if (!sampleNeedsNormalizing(layout.sample)) {
            rowBytes = layout.stride;
            return pixels;
        }

        const int samplesPerRow = layout.width * bytesPerPixel() / 2;
        rowBytes = samplesPerRow * 2;
        scratch.resize(qsizetype(rowBytes) * layout.height);
        auto *destination = reinterpret_cast<quint16 *>(scratch.data());
        for (int row = 0; row < layout.height; ++row) {
            const uchar *source = pixels + qint64(row) * layout.stride;
            quint16 *destinationRow = destination + qint64(row) * samplesPerRow;
            for (int i = 0; i < samplesPerRow; ++i)
                destinationRow[i] = fullRangeSample(readLe16(source + 2 * i), layout.sample);
        }
        return reinterpret_cast<const uchar *>(scratch.constData());
    }
};

class Rgb48Decoder final : public PackedRgb16Decoder
{
public:
    QLatin1StringView id() const override { return "rgb48"_L1; }
    QString displayName() const override { return QStringLiteral("RGB48"); }
    QString mimeType() const override { return "video/x-raw-rgb48"_L1; }
    QStringList fileExtensions() const override { return {"rgb48"_L1, "RGB48"_L1}; }

    ImageResult convertToImage(const QByteArray &data,
                               const RawImageLayout &layout) const override
    {
        return runConversion(*this, [&]() -> ImageResult {
            QByteArray scratch;
            int rowBytes = 0;
            const uchar *pixels = fullRangeRows(data, layout, scratch, rowBytes);
            cv::Mat rgb(layout.height, layout.width, CV_16UC3, const_cast<uchar *>(pixels),
                        static_cast<size_t>(rowBytes));
            // OpenCV preserves the 16-bit depth through the conversion.
            cv::Mat rgba;
            cv::cvtColor(rgb, rgba, cv::COLOR_RGB2RGBA);
            return rgbaMatToImage(rgba, layout, QImage::Format_RGBA64);
        });
    }

protected:
    int bytesPerPixel() const override { return 6; }
};

class Rgba64Decoder final : public PackedRgb16Decoder
{
public:
    QLatin1StringView id() const override { return "rgba64"_L1; }
    QString displayName() const override { return QStringLiteral("RGBA64"); }
    QString mimeType() const override { return "video/x-raw-rgba64"_L1; }
    QStringList fileExtensions() const override { return {"rgba64"_L1, "RGBA64"_L1}; }

    ImageResult convertToImage(const QByteArray &data,
                               const RawImageLayout &layout) const override
    {
        QByteArray scratch;
        int rowBytes = 0;
        const uchar *pixels = fullRangeRows(data, layout, scratch, rowBytes);
        // Little-endian 16-bit R,G,B,A samples match QImage::Format_RGBA64.
        const QImage wrappedImage(pixels, layout.width, layout.height,
                                  static_cast<qsizetype>(rowBytes),
                                  QImage::Format_RGBA64);
        QImage image = wrappedImage.copy();
        if (image.isNull())
            return std::unexpected(tr("Could not allocate the converted QImage."));
        return image;
    }

protected:
    int bytesPerPixel() const override { return 8; }
};

// Single-plane 8-bit grayscale: Y samples only, no chroma, so no
// subsampling alignment constraints apply.
class Y8Decoder final : public RawImageDecoder
{
    Q_DECLARE_TR_FUNCTIONS(Y8Decoder)

public:
    QLatin1StringView id() const override { return "y8"_L1; }
    QString displayName() const override { return QStringLiteral("Y8"); }
    QString mimeType() const override { return "video/x-raw-y8"_L1; }
    QStringList fileExtensions() const override { return {"y8"_L1, "Y8"_L1}; }

    LayoutResult validateLayout(const RawImageLayout &layout) const override
    {
        const LayoutResult baseResult = RawImageDecoder::validateLayout(layout);
        if (!baseResult)
            return baseResult;

        if (layout.stride < layout.width) {
            return std::unexpected(tr("%1 stride must be at least the width. "
                                      "Received width %2, stride %3.")
                                       .arg(displayName())
                                       .arg(layout.width)
                                       .arg(layout.stride));
        }
        if (layout.scanline < layout.height) {
            return std::unexpected(tr("%1 scanline must be at least the height. "
                                      "Received height %2, scanline %3.")
                                       .arg(displayName())
                                       .arg(layout.height)
                                       .arg(layout.scanline));
        }

        return layout;
    }

    qint64 expectedByteSize(const RawImageLayout &layout) const override
    {
        return qint64(layout.stride) * qint64(layout.scanline);
    }

    ImageResult convertToImage(const QByteArray &data,
                               const RawImageLayout &layout) const override
    {
        const auto *pixels = reinterpret_cast<const uchar *>(data.constData());
        const QImage wrappedImage(pixels, layout.width, layout.height,
                                  static_cast<qsizetype>(layout.stride),
                                  QImage::Format_Grayscale8);
        QImage image = wrappedImage.copy();
        if (image.isNull())
            return std::unexpected(tr("Could not allocate the converted QImage."));
        return image;
    }

    QStringList planeNames() const override { return {QStringLiteral("Y")}; }

    ImageResult extractPlane(const QByteArray &data, const RawImageLayout &layout,
                             int plane) const override
    {
        if (plane != 0)
            return invalidPlane(plane);
        const auto *pixels = reinterpret_cast<const uchar *>(data.constData());
        return grayscalePlane(pixels, layout.width, layout.height, layout.stride);
    }

    QString describePixel(const QByteArray &data, const RawImageLayout &layout,
                          int x, int y) const override
    {
        const auto *pixels = reinterpret_cast<const uchar *>(data.constData());
        return tr("Y=%1").arg(pixels[qint64(y) * layout.stride + x]);
    }
};

} // namespace

QList<const RawImageDecoder *> RawImageDecoders::createRgbDecoders()
{
    return {
        new Rgb888Decoder,
        new Bgr888Decoder,
        new Rgba8888Decoder,
        new Rgbx8888Decoder,
        new Bgra8888Decoder,
        new Bgrx8888Decoder,
        new Rgb565Decoder,
        new Bgr565Decoder,
        new Rgb555Decoder,
        new Bgr555Decoder,
        new Rgb48Decoder,
        new Rgba64Decoder,
        new Y8Decoder,
    };
}
