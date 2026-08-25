// Copyright (C) 2026
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#ifndef RAWIMAGEDECODER_P_H
#define RAWIMAGEDECODER_P_H

#include "rawimagedecoder.h"

#include <opencv2/core.hpp>

#include <cstring>

// Primitives shared by the concrete decoders in yuvdecoders.cpp and
// rgbdecoders.cpp. A new format is normally a matter of picking the
// matching layout validator, one of the plane extractors and one of the
// pixel describers, so this is the first place to look before writing
// anything by hand.
//
// Not part of the public decoder API; only the decoder implementations
// and the registry include this header.
namespace RawImageDecoderHelpers {

using namespace Qt::StringLiterals;

// Shared layout validation for 4:2:0 formats (semi-planar and planar):
// 2x2 chroma subsampling requires even dimensions, stride and scanline.
inline RawImageDecoder::LayoutResult validateYuv420Layout(const RawImageDecoder &decoder,
                                                          const RawImageLayout &layout)
{
    // Qualified call: always the base implementation, no virtual dispatch.
    const RawImageDecoder::LayoutResult baseResult = decoder.RawImageDecoder::validateLayout(layout);
    if (!baseResult)
        return baseResult;

    if ((layout.width % 2) != 0 || (layout.height % 2) != 0) {
        return std::unexpected(RawImageDecoder::tr("%1 width and height must both be even. "
                                                   "Received %2x%3.")
                                   .arg(decoder.displayName())
                                   .arg(layout.width)
                                   .arg(layout.height));
    }
    if (layout.stride < layout.width || (layout.stride % 2) != 0) {
        return std::unexpected(RawImageDecoder::tr("%1 stride must be even and at least the width. "
                                                   "Received width %2, stride %3.")
                                   .arg(decoder.displayName())
                                   .arg(layout.width)
                                   .arg(layout.stride));
    }
    if (layout.scanline < layout.height || (layout.scanline % 2) != 0) {
        return std::unexpected(RawImageDecoder::tr("%1 scanline must be even and at least the height. "
                                                   "Received height %2, scanline %3.")
                                   .arg(decoder.displayName())
                                   .arg(layout.height)
                                   .arg(layout.scanline));
    }

    return layout;
}

// Shared layout validation for 4:2:2 formats (packed, planar and
// semi-planar): horizontal-only chroma subsampling requires an even
// width and an even stride, but imposes no constraint on the height.
inline RawImageDecoder::LayoutResult validateYuv422Layout(const RawImageDecoder &decoder,
                                                          const RawImageLayout &layout)
{
    const RawImageDecoder::LayoutResult baseResult = decoder.RawImageDecoder::validateLayout(layout);
    if (!baseResult)
        return baseResult;

    if ((layout.width % 2) != 0) {
        return std::unexpected(RawImageDecoder::tr("%1 width must be even. Received %2.")
                                   .arg(decoder.displayName())
                                   .arg(layout.width));
    }
    if (layout.stride < layout.width || (layout.stride % 2) != 0) {
        return std::unexpected(RawImageDecoder::tr("%1 stride must be even and at least the width. "
                                                   "Received width %2, stride %3.")
                                   .arg(decoder.displayName())
                                   .arg(layout.width)
                                   .arg(layout.stride));
    }
    if (layout.scanline < layout.height) {
        return std::unexpected(RawImageDecoder::tr("%1 scanline must be at least the height. "
                                                   "Received height %2, scanline %3.")
                                   .arg(decoder.displayName())
                                   .arg(layout.height)
                                   .arg(layout.scanline));
    }

    return layout;
}

// Shared layout validation for 4:4:4 formats (planar, semi-planar and
// packed): without chroma subsampling no dimension has to be even, so
// only the row and plane padding are checked. bytesPerPixel scales the
// stride requirement for packed layouts, whose single plane carries
// every component of a pixel.
inline RawImageDecoder::LayoutResult validateYuv444Layout(const RawImageDecoder &decoder,
                                                          const RawImageLayout &layout,
                                                          int bytesPerPixel = 1)
{
    const RawImageDecoder::LayoutResult baseResult = decoder.RawImageDecoder::validateLayout(layout);
    if (!baseResult)
        return baseResult;

    if (layout.stride < layout.width * bytesPerPixel) {
        if (bytesPerPixel == 1) {
            return std::unexpected(RawImageDecoder::tr("%1 stride must be at least the width. "
                                                       "Received width %2, stride %3.")
                                       .arg(decoder.displayName())
                                       .arg(layout.width)
                                       .arg(layout.stride));
        }
        return std::unexpected(RawImageDecoder::tr("%1 stride must be at least the width times %2 "
                                                   "bytes. Received width %3, stride %4.")
                                   .arg(decoder.displayName())
                                   .arg(bytesPerPixel)
                                   .arg(layout.width)
                                   .arg(layout.stride));
    }
    if (layout.scanline < layout.height) {
        return std::unexpected(RawImageDecoder::tr("%1 scanline must be at least the height. "
                                                   "Received height %2, scanline %3.")
                                   .arg(decoder.displayName())
                                   .arg(layout.height)
                                   .arg(layout.scanline));
    }

    return layout;
}

// Wraps a converted RGB/RGBA Mat into a detached QImage.
inline RawImageDecoder::ImageResult rgbaMatToImage(const cv::Mat &rgba,
                                                   const RawImageLayout &layout,
                                                   QImage::Format format = QImage::Format_RGBA8888)
{
    if (rgba.empty() || rgba.cols != layout.width || rgba.rows != layout.height) {
        return std::unexpected(RawImageDecoder::tr(
            "OpenCV returned an empty image or unexpected dimensions."));
    }

    const QImage wrappedImage(rgba.data, rgba.cols, rgba.rows,
                              static_cast<qsizetype>(rgba.step), format);
    QImage image = wrappedImage.copy();
    if (image.isNull())
        return std::unexpected(RawImageDecoder::tr("Could not allocate the converted QImage."));
    return image;
}

// Runs a YUV->RGBA conversion, mapping exceptions to error results.
template <typename Conversion>
RawImageDecoder::ImageResult runConversion(const RawImageDecoder &decoder, Conversion &&conversion)
{
    try {
        return conversion();
    } catch (const cv::Exception &exception) {
        return std::unexpected(RawImageDecoder::tr("OpenCV conversion failed: %1")
                                   .arg(QString::fromLocal8Bit(exception.what())));
    } catch (const std::exception &exception) {
        return std::unexpected(RawImageDecoder::tr("%1 conversion failed: %2")
                                   .arg(decoder.displayName(),
                                        QString::fromLocal8Bit(exception.what())));
    } catch (...) {
        return std::unexpected(RawImageDecoder::tr("%1 conversion failed with an unknown exception.")
                                   .arg(decoder.displayName()));
    }
}

// Wraps an 8-bit plane as a detached Format_Grayscale8 image.
inline RawImageDecoder::ImageResult grayscalePlane(const uchar *base, int width, int height,
                                                   qsizetype stride)
{
    const QImage wrapped(base, width, height, stride, QImage::Format_Grayscale8);
    QImage image = wrapped.copy();
    if (image.isNull())
        return std::unexpected(RawImageDecoder::tr("Could not allocate the plane image."));
    return image;
}

// Copies every step-th byte starting at offset into a tight grayscale
// image; used to deinterleave packed or semi-planar components.
inline RawImageDecoder::ImageResult stridedPlane(const uchar *base, int width, int height,
                                                 qint64 rowStride, int step, int offset)
{
    QImage image(width, height, QImage::Format_Grayscale8);
    if (image.isNull())
        return std::unexpected(RawImageDecoder::tr("Could not allocate the plane image."));
    for (int row = 0; row < height; ++row) {
        const uchar *src = base + qint64(row) * rowStride + offset;
        uchar *dst = image.scanLine(row);
        for (int col = 0; col < width; ++col)
            dst[col] = src[col * step];
    }
    return image;
}

// Extracts one channel of a bit-packed 16-bit RGB format (RGB565 and
// friends) and scales it to 8 bits.
inline RawImageDecoder::ImageResult rgb16Plane(const uchar *base, int width, int height,
                                               qint64 rowStride, quint16 mask, int shift, int bits)
{
    QImage image(width, height, QImage::Format_Grayscale8);
    if (image.isNull())
        return std::unexpected(RawImageDecoder::tr("Could not allocate the plane image."));
    const int maxValue = (1 << bits) - 1;
    for (int row = 0; row < height; ++row) {
        const uchar *src = base + qint64(row) * rowStride;
        uchar *dst = image.scanLine(row);
        for (int col = 0; col < width; ++col) {
            quint16 pixel;
            memcpy(&pixel, src + 2 * col, sizeof(pixel));
            const int value = (pixel & mask) >> shift;
            dst[col] = uchar((value * 255 + maxValue / 2) / maxValue);
        }
    }
    return image;
}

inline RawImageDecoder::ImageResult invalidPlane(int plane)
{
    return std::unexpected(RawImageDecoder::tr("Invalid plane index %1.").arg(plane));
}

// Reads one little-endian 16-bit sample.
inline quint16 readLe16(const uchar *p)
{
    quint16 value;
    memcpy(&value, p, sizeof(value));
    return value;
}

// Wraps a 16-bit plane as 8-bit grayscale, taking the most significant
// byte of each MSB-aligned sample.
inline RawImageDecoder::ImageResult grayscale16Plane(const uchar *base, int width, int height,
                                                     qsizetype strideBytes)
{
    QImage image(width, height, QImage::Format_Grayscale8);
    if (image.isNull())
        return std::unexpected(RawImageDecoder::tr("Could not allocate the plane image."));
    for (int row = 0; row < height; ++row) {
        const uchar *src = base + qint64(row) * strideBytes;
        uchar *dst = image.scanLine(row);
        for (int col = 0; col < width; ++col)
            dst[col] = uchar(readLe16(src + 2 * col) >> 8);
    }
    return image;
}

// 16-bit variant of stridedPlane(); step and offset are in samples.
inline RawImageDecoder::ImageResult strided16Plane(const uchar *base, int width, int height,
                                                   qint64 rowStrideBytes, int step, int offset)
{
    QImage image(width, height, QImage::Format_Grayscale8);
    if (image.isNull())
        return std::unexpected(RawImageDecoder::tr("Could not allocate the plane image."));
    for (int row = 0; row < height; ++row) {
        const uchar *src = base + qint64(row) * rowStrideBytes;
        uchar *dst = image.scanLine(row);
        for (int col = 0; col < width; ++col)
            dst[col] = uchar(readLe16(src + 2 * (col * step + offset)) >> 8);
    }
    return image;
}

inline QString describeYuv(int y, int u, int v)
{
    return RawImageDecoder::tr("Y=%1 U=%2 V=%3").arg(y).arg(u).arg(v);
}

inline QString describeRgb(int r, int g, int b)
{
    return RawImageDecoder::tr("R=%1 G=%2 B=%3").arg(r).arg(g).arg(b);
}

inline QString describeRgba(int r, int g, int b, int a)
{
    return RawImageDecoder::tr("R=%1 G=%2 B=%3 A=%4").arg(r).arg(g).arg(b).arg(a);
}

} // namespace RawImageDecoderHelpers

namespace RawImageDecoders {

// Built by the per-family translation units. The registry concatenates
// them in the order the format combo box presents; adding a format means
// adding a class to the matching file and a line to its factory.
QList<const RawImageDecoder *> createYuvDecoders();
QList<const RawImageDecoder *> createRgbDecoders();

} // namespace RawImageDecoders

#endif // RAWIMAGEDECODER_P_H
