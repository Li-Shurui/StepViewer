// Copyright (C) 2026
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#ifndef RAWIMAGEDECODER_P_H
#define RAWIMAGEDECODER_P_H

#include "rawimagedecoder.h"

#include <opencv2/core.hpp>

#include <cstring>

// Primitives shared by the concrete decoders in yuvdecoders.cpp,
// rgbdecoders.cpp and bayerdecoders.cpp. A new format is normally a
// matter of picking the matching layout validator, one of the plane
// layouts and one of the pixel describers, so this is the first place
// to look before writing anything by hand.
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

// Shared layout validation for Bayer mosaic formats: the color filter
// array repeats every two pixels in both directions, so a frame has to
// cover whole 2x2 cells. bytesPerPixel scales the stride requirement for
// the wider sample containers.
inline RawImageDecoder::LayoutResult validateBayerLayout(const RawImageDecoder &decoder,
                                                         const RawImageLayout &layout,
                                                         int bytesPerPixel)
{
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
    if (layout.stride < layout.width * bytesPerPixel) {
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

// Plane layouts for the three ways a sample is stored. Offsets and
// strides are in bytes.

// One byte per sample.
inline RawPlaneLayout bytePlane(qint64 offset, int width, int height, qint64 rowStride,
                                int sampleStride = 1)
{
    return {.offset = offset, .width = width, .height = height, .rowStride = rowStride,
            .sampleStride = sampleStride, .containerBytes = 1, .shift = 0, .bits = 8};
}

// One 16-bit container per sample, packed the way format says.
inline RawPlaneLayout wordPlane(qint64 offset, int width, int height, qint64 rowStride,
                                int sampleStride, const RawSampleFormat &format)
{
    return {.offset = offset, .width = width, .height = height, .rowStride = rowStride,
            .sampleStride = sampleStride, .containerBytes = 2,
            .shift = format.msbAligned ? format.padding() : 0, .bits = format.bits};
}

// One bit field of a 16-bit pixel, as in RGB565.
inline RawPlaneLayout bitFieldPlane(int width, int height, qint64 rowStride, int shift, int bits)
{
    return {.offset = 0, .width = width, .height = height, .rowStride = rowStride,
            .sampleStride = 2, .containerBytes = 2, .shift = shift, .bits = bits};
}

// Reads one little-endian 16-bit sample.
inline quint16 readLe16(const uchar *p)
{
    quint16 value;
    memcpy(&value, p, sizeof(value));
    return value;
}

// The sample as the format's own number, which is what the pixel probe
// reports: 0 .. 2^bits - 1, whichever end of the container it sits at.
inline int rawSampleValue(quint16 container, const RawSampleFormat &format)
{
    if (format.msbAligned)
        return container >> format.padding();
    return container & quint16((1u << format.bits) - 1u);
}

// The same sample expanded to the full 16-bit range, which is what the
// converters and the 8-bit display path want. MSB-aligned samples already
// are full range; right-aligned ones have to be shifted up, otherwise
// 10-bit data would only ever reach 1.5% of the range.
inline quint16 fullRangeSample(quint16 container, const RawSampleFormat &format)
{
    if (format.msbAligned)
        return container;
    return quint16(quint32(container & quint16((1u << format.bits) - 1u)) << format.padding());
}

// True when containers can be handed to OpenCV or QImage as they are,
// letting the converters skip a normalizing copy of the whole plane.
inline bool sampleNeedsNormalizing(const RawSampleFormat &format)
{
    return !format.msbAligned && format.padding() != 0;
}

inline QString describeYuv(int y, int u, int v)
{
    return RawImageDecoder::tr("Y=%1 U=%2 V=%3").arg(y).arg(u).arg(v);
}

inline QString describeYuva(int y, int u, int v, int a)
{
    return RawImageDecoder::tr("Y=%1 U=%2 V=%3 A=%4").arg(y).arg(u).arg(v).arg(a);
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
QList<const RawImageDecoder *> createBayerDecoders();

} // namespace RawImageDecoders

#endif // RAWIMAGEDECODER_P_H
