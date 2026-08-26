// Copyright (C) 2026
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

// Decoders for the YUV format families: 4:2:0, 4:2:2 and 4:4:4 in
// planar, semi-planar and packed arrangements, at 8 and 16 bits per
// sample. Each family has a base class holding the layout rules and the
// conversion, and one thin subclass per concrete format that only picks
// the component order and the naming.

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

// Shared implementation for two-plane (semi-planar) YUV 4:2:0 formats:
// a full-resolution Y plane followed by an interleaved 2x2-subsampled
// chroma plane. Only the chroma order (UV vs VU) differs between them.
class SemiPlanarYuv420Decoder : public RawImageDecoder
{
public:
    LayoutResult validateLayout(const RawImageLayout &layout) const override
    {
        return validateYuv420Layout(*this, layout);
    }

    qint64 expectedByteSize(const RawImageLayout &layout) const override
    {
        return qint64(layout.stride) * qint64(layout.scanline) * 3 / 2;
    }

    ImageResult convertToImage(const QByteArray &data,
                               const RawImageLayout &layout) const override
    {
        return runConversion(*this, [&]() -> ImageResult {
            auto *pixels = reinterpret_cast<uchar *>(const_cast<char *>(data.constData()));
            const qint64 yPlaneBytes = qint64(layout.stride) * qint64(layout.scanline);

            cv::Mat yPlane(layout.height, layout.width, CV_8UC1, pixels,
                           static_cast<size_t>(layout.stride));
            cv::Mat uvPlane(layout.height / 2, layout.width / 2, CV_8UC2,
                            pixels + yPlaneBytes, static_cast<size_t>(layout.stride));
            cv::Mat rgba;
            cv::cvtColorTwoPlane(yPlane, uvPlane, rgba, conversionCode());
            return rgbaMatToImage(rgba, layout);
        });
    }

    QStringList planeNames() const override
    {
        return {QStringLiteral("Y"), QStringLiteral("U"), QStringLiteral("V")};
    }

    ImageResult extractPlane(const QByteArray &data, const RawImageLayout &layout,
                             int plane) const override
    {
        const auto *pixels = reinterpret_cast<const uchar *>(data.constData());
        const qint64 yPlaneBytes = qint64(layout.stride) * layout.scanline;
        switch (plane) {
        case 0:
            return grayscalePlane(pixels, layout.width, layout.height, layout.stride);
        case 1:
        case 2: {
            const int offset = ((plane == 1) == chromaOrderIsUV()) ? 0 : 1;
            return stridedPlane(pixels + yPlaneBytes, layout.width / 2, layout.height / 2,
                                layout.stride, 2, offset);
        }
        default:
            return invalidPlane(plane);
        }
    }

    QString describePixel(const QByteArray &data, const RawImageLayout &layout,
                          int x, int y) const override
    {
        const auto *pixels = reinterpret_cast<const uchar *>(data.constData());
        const qint64 yPlaneBytes = qint64(layout.stride) * layout.scanline;
        const int luma = pixels[qint64(y) * layout.stride + x];
        const uchar *chroma = pixels + yPlaneBytes + qint64(y / 2) * layout.stride
                              + (x / 2) * 2;
        const int u = chroma[chromaOrderIsUV() ? 0 : 1];
        const int v = chroma[chromaOrderIsUV() ? 1 : 0];
        return describeYuv(luma, u, v);
    }

protected:
    // cv::COLOR_YUV2RGBA_NV12 / cv::COLOR_YUV2RGBA_NV21 / ...
    virtual int conversionCode() const = 0;
    // NV12 interleaves U,V; NV21 interleaves V,U.
    virtual bool chromaOrderIsUV() const = 0;
};

class Nv12Decoder final : public SemiPlanarYuv420Decoder
{
public:
    QLatin1StringView id() const override { return "nv12"_L1; }
    QString displayName() const override { return QStringLiteral("NV12"); }
    QString mimeType() const override { return "video/x-raw-nv12"_L1; }
    QStringList fileExtensions() const override { return {"nv12"_L1, "NV12"_L1, "YUV420NV12"_L1}; }

protected:
    int conversionCode() const override { return cv::COLOR_YUV2RGBA_NV12; }
    bool chromaOrderIsUV() const override { return true; }
};

class Nv21Decoder final : public SemiPlanarYuv420Decoder
{
public:
    QLatin1StringView id() const override { return "nv21"_L1; }
    QString displayName() const override { return QStringLiteral("NV21"); }
    QString mimeType() const override { return "video/x-raw-nv21"_L1; }
    QStringList fileExtensions() const override {
        return {"nv21"_L1, "NV21"_L1, "YUV420NV21"_L1, "yuv420nv21"_L1
                "yuv420sp"_L1, "YUV420SP"_L1};
    }

protected:
    int conversionCode() const override { return cv::COLOR_YUV2RGBA_NV21; }
    bool chromaOrderIsUV() const override { return false; }
};

// Shared implementation for 16-bit semi-planar YUV 4:2:0 formats
// (P010/P016): like NV12, but each sample is stored MSB-aligned in a
// little-endian 16-bit container. Display conversion takes the top 8
// bits of every sample and reuses the 8-bit NV12 path; the pixel probe
// reports values shifted back to the native bit depth.
class SemiPlanarYuv420p16Decoder : public RawImageDecoder
{
    Q_DECLARE_TR_FUNCTIONS(SemiPlanarYuv420p16Decoder)

public:
    LayoutResult validateLayout(const RawImageLayout &layout) const override
    {
        const LayoutResult baseResult = validateYuv420Layout(*this, layout);
        if (!baseResult)
            return baseResult;

        if (layout.stride < layout.width * 2) {
            return std::unexpected(tr("%1 stride must be at least twice the width "
                                      "(16-bit samples). Received width %2, stride %3.")
                                       .arg(displayName())
                                       .arg(layout.width)
                                       .arg(layout.stride));
        }
        return layout;
    }

    qint64 expectedByteSize(const RawImageLayout &layout) const override
    {
        // stride is in bytes; the chroma plane covers half the lines.
        return qint64(layout.stride) * qint64(layout.scanline) * 3 / 2;
    }

    int defaultStride(int width) const override { return width * 2; }

    ImageResult convertToImage(const QByteArray &data,
                               const RawImageLayout &layout) const override
    {
        return runConversion(*this, [&]() -> ImageResult {
            const auto *pixels = reinterpret_cast<const uchar *>(data.constData());
            const qint64 yPlaneBytes = qint64(layout.stride) * layout.scanline;

            // Downconvert to tightly packed 8-bit semi-planar.
            QByteArray y8(qint64(layout.width) * layout.height, Qt::Uninitialized);
            QByteArray uv8(qint64(layout.width) * layout.height / 2, Qt::Uninitialized);
            auto *yDst = reinterpret_cast<uchar *>(y8.data());
            auto *uvDst = reinterpret_cast<uchar *>(uv8.data());
            for (int row = 0; row < layout.height; ++row) {
                const uchar *src = pixels + qint64(row) * layout.stride;
                uchar *dst = yDst + qint64(row) * layout.width;
                for (int col = 0; col < layout.width; ++col)
                    dst[col] = uchar(readLe16(src + 2 * col) >> 8);
            }
            const int chromaSamples = layout.width;  // interleaved U+V per row
            for (int row = 0; row < layout.height / 2; ++row) {
                const uchar *src = pixels + yPlaneBytes + qint64(row) * layout.stride;
                uchar *dst = uvDst + qint64(row) * chromaSamples;
                for (int col = 0; col < chromaSamples; ++col)
                    dst[col] = uchar(readLe16(src + 2 * col) >> 8);
            }

            cv::Mat yPlane(layout.height, layout.width, CV_8UC1, yDst,
                           static_cast<size_t>(layout.width));
            cv::Mat uvPlane(layout.height / 2, layout.width / 2, CV_8UC2, uvDst,
                            static_cast<size_t>(chromaSamples));
            cv::Mat rgba;
            cv::cvtColorTwoPlane(yPlane, uvPlane, rgba, cv::COLOR_YUV2RGBA_NV12);
            return rgbaMatToImage(rgba, layout);
        });
    }

    QStringList planeNames() const override
    {
        return {QStringLiteral("Y"), QStringLiteral("U"), QStringLiteral("V")};
    }

    ImageResult extractPlane(const QByteArray &data, const RawImageLayout &layout,
                             int plane) const override
    {
        const auto *pixels = reinterpret_cast<const uchar *>(data.constData());
        const qint64 yPlaneBytes = qint64(layout.stride) * layout.scanline;
        switch (plane) {
        case 0:
            return grayscale16Plane(pixels, layout.width, layout.height, layout.stride);
        case 1:
            return strided16Plane(pixels + yPlaneBytes, layout.width / 2, layout.height / 2,
                                  layout.stride, 2, 0);
        case 2:
            return strided16Plane(pixels + yPlaneBytes, layout.width / 2, layout.height / 2,
                                  layout.stride, 2, 1);
        default:
            return invalidPlane(plane);
        }
    }

    QString describePixel(const QByteArray &data, const RawImageLayout &layout,
                          int x, int y) const override
    {
        const auto *pixels = reinterpret_cast<const uchar *>(data.constData());
        const qint64 yPlaneBytes = qint64(layout.stride) * layout.scanline;
        const int shift = 16 - bitDepth();
        const int luma = readLe16(pixels + qint64(y) * layout.stride + 2 * x) >> shift;
        const uchar *chroma = pixels + yPlaneBytes + qint64(y / 2) * layout.stride
                              + (x / 2) * 4;
        const int u = readLe16(chroma) >> shift;
        const int v = readLe16(chroma + 2) >> shift;
        return describeYuv(luma, u, v);
    }

protected:
    // 10 for P010, 16 for P016. Both are MSB-aligned, so the top 8 bits
    // always carry the most significant part of the sample.
    virtual int bitDepth() const = 0;
};

class P010Decoder final : public SemiPlanarYuv420p16Decoder
{
public:
    QLatin1StringView id() const override { return "p010"_L1; }
    QString displayName() const override { return QStringLiteral("P010"); }
    QString mimeType() const override { return "video/x-raw-p010"_L1; }
    QStringList fileExtensions() const override { return {"p010"_L1, "P010"_L1}; }

protected:
    int bitDepth() const override { return 10; }
};

class P016Decoder final : public SemiPlanarYuv420p16Decoder
{
public:
    QLatin1StringView id() const override { return "p016"_L1; }
    QString displayName() const override { return QStringLiteral("P016"); }
    QString mimeType() const override { return "video/x-raw-p016"_L1; }
    QStringList fileExtensions() const override { return {"p016"_L1, "P016"_L1}; }

protected:
    int bitDepth() const override { return 16; }
};

// Shared implementation for three-plane (planar) YUV 4:2:0 formats:
// a full-resolution Y plane followed by the 2x2-subsampled U and V
// planes, each with half the stride and half the lines of the Y plane.
// Only the chroma plane order (U,V vs V,U) differs between them.
class PlanarYuv420Decoder : public RawImageDecoder
{
public:
    LayoutResult validateLayout(const RawImageLayout &layout) const override
    {
        return validateYuv420Layout(*this, layout);
    }

    qint64 expectedByteSize(const RawImageLayout &layout) const override
    {
        return qint64(layout.stride) * qint64(layout.scanline) * 3 / 2;
    }

    ImageResult convertToImage(const QByteArray &data,
                               const RawImageLayout &layout) const override
    {
        return runConversion(*this, [&]() -> ImageResult {
            const auto *pixels = reinterpret_cast<const uchar *>(data.constData());

            // OpenCV's three-plane conversion assumes one tightly packed
            // buffer, so padded planes are repacked into a tight copy.
            const uchar *src = pixels;
            QByteArray tightBuffer;
            if (layout.stride != layout.width || layout.scanline != layout.height) {
                const qint64 yPlaneBytes = qint64(layout.stride) * layout.scanline;
                const qint64 chromaPlaneBytes = qint64(layout.stride / 2)
                                              * (layout.scanline / 2);
                const uchar *planeSrc[3] = {pixels,
                                            pixels + yPlaneBytes,
                                            pixels + yPlaneBytes + chromaPlaneBytes};
                const int planeStride[3] = {layout.stride,
                                            layout.stride / 2,
                                            layout.stride / 2};
                const int planeRows[3] = {layout.height,
                                          layout.height / 2,
                                          layout.height / 2};
                const int planeBytes[3] = {layout.width,
                                           layout.width / 2,
                                           layout.width / 2};

                tightBuffer.resize(qint64(layout.width) * layout.height * 3 / 2);
                auto *dst = reinterpret_cast<uchar *>(tightBuffer.data());
                for (int plane = 0; plane < 3; ++plane) {
                    for (int row = 0; row < planeRows[plane]; ++row) {
                        memcpy(dst, planeSrc[plane] + qint64(row) * planeStride[plane],
                               size_t(planeBytes[plane]));
                        dst += planeBytes[plane];
                    }
                }
                src = reinterpret_cast<const uchar *>(tightBuffer.constData());
            }

            cv::Mat yuv(layout.height * 3 / 2, layout.width, CV_8UC1,
                        const_cast<uchar *>(src));
            cv::Mat rgba;
            cv::cvtColor(yuv, rgba, conversionCode());
            return rgbaMatToImage(rgba, layout);
        });
    }

    QStringList planeNames() const override
    {
        return {QStringLiteral("Y"), QStringLiteral("U"), QStringLiteral("V")};
    }

    ImageResult extractPlane(const QByteArray &data, const RawImageLayout &layout,
                             int plane) const override
    {
        const auto *pixels = reinterpret_cast<const uchar *>(data.constData());
        const qint64 yPlaneBytes = qint64(layout.stride) * layout.scanline;
        const qint64 chromaPlaneBytes = qint64(layout.stride / 2) * (layout.scanline / 2);
        switch (plane) {
        case 0:
            return grayscalePlane(pixels, layout.width, layout.height, layout.stride);
        case 1:
            return grayscalePlane(pixels + yPlaneBytes
                                      + (chromaOrderIsUV() ? 0 : chromaPlaneBytes),
                                  layout.width / 2, layout.height / 2, layout.stride / 2);
        case 2:
            return grayscalePlane(pixels + yPlaneBytes
                                      + (chromaOrderIsUV() ? chromaPlaneBytes : 0),
                                  layout.width / 2, layout.height / 2, layout.stride / 2);
        default:
            return invalidPlane(plane);
        }
    }

    QString describePixel(const QByteArray &data, const RawImageLayout &layout,
                          int x, int y) const override
    {
        const auto *pixels = reinterpret_cast<const uchar *>(data.constData());
        const qint64 yPlaneBytes = qint64(layout.stride) * layout.scanline;
        const qint64 chromaPlaneBytes = qint64(layout.stride / 2) * (layout.scanline / 2);
        const uchar *uPlane = pixels + yPlaneBytes
                              + (chromaOrderIsUV() ? 0 : chromaPlaneBytes);
        const uchar *vPlane = pixels + yPlaneBytes
                              + (chromaOrderIsUV() ? chromaPlaneBytes : 0);
        const int luma = pixels[qint64(y) * layout.stride + x];
        const int u = uPlane[qint64(y / 2) * (layout.stride / 2) + x / 2];
        const int v = vPlane[qint64(y / 2) * (layout.stride / 2) + x / 2];
        return describeYuv(luma, u, v);
    }

protected:
    // cv::COLOR_YUV2RGBA_I420 / cv::COLOR_YUV2RGBA_YV12 / ...
    virtual int conversionCode() const = 0;
    // I420 stores Y,U,V planes; YV12 stores Y,V,U.
    virtual bool chromaOrderIsUV() const = 0;
};

class I420Decoder final : public PlanarYuv420Decoder
{
public:
    QLatin1StringView id() const override { return "i420"_L1; }
    QString displayName() const override { return QStringLiteral("I420"); }
    QString mimeType() const override { return "video/x-raw-i420"_L1; }
    QStringList fileExtensions() const override {
        return {"i420"_L1, "I420"_L1, "yuv420p"_L1, "YUV420P"_L1,
                "yu12"_L1, "YU12"_L1, "IYUV"_L1, "iyuv"_L1};
    }

protected:
    int conversionCode() const override { return cv::COLOR_YUV2RGBA_I420; }
    bool chromaOrderIsUV() const override { return true; }
};

class Yv12Decoder final : public PlanarYuv420Decoder
{
public:
    QLatin1StringView id() const override { return "yv12"_L1; }
    QString displayName() const override { return QStringLiteral("YV12"); }
    QString mimeType() const override { return "video/x-raw-yv12"_L1; }
    QStringList fileExtensions() const override { return {"yv12"_L1, "YV12"_L1}; }

protected:
    int conversionCode() const override { return cv::COLOR_YUV2RGBA_YV12; }
    bool chromaOrderIsUV() const override { return false; }
};

// Shared implementation for 16-bit three-plane (planar) YUV 4:2:0
// formats (I010/I016): like I420, but each sample is stored MSB-aligned
// in a little-endian 16-bit container. Display conversion takes the top
// 8 bits of every sample and reuses the 8-bit I420 path; the pixel
// probe reports values shifted back to the native bit depth.
class PlanarYuv420p16Decoder : public RawImageDecoder
{
    Q_DECLARE_TR_FUNCTIONS(PlanarYuv420p16Decoder)

public:
    LayoutResult validateLayout(const RawImageLayout &layout) const override
    {
        const LayoutResult baseResult = validateYuv420Layout(*this, layout);
        if (!baseResult)
            return baseResult;

        if (layout.stride < layout.width * 2) {
            return std::unexpected(tr("%1 stride must be at least twice the width "
                                      "(16-bit samples). Received width %2, stride %3.")
                                       .arg(displayName())
                                       .arg(layout.width)
                                       .arg(layout.stride));
        }
        return layout;
    }

    qint64 expectedByteSize(const RawImageLayout &layout) const override
    {
        return qint64(layout.stride) * qint64(layout.scanline) * 3 / 2;
    }

    int defaultStride(int width) const override { return width * 2; }

    ImageResult convertToImage(const QByteArray &data,
                               const RawImageLayout &layout) const override
    {
        return runConversion(*this, [&]() -> ImageResult {
            const auto *pixels = reinterpret_cast<const uchar *>(data.constData());
            const qint64 yPlaneBytes = qint64(layout.stride) * layout.scanline;
            const qint64 chromaStride = layout.stride / 2;  // bytes per chroma row
            const qint64 chromaPlaneBytes = chromaStride * (layout.scanline / 2);

            // Downconvert to tightly packed 8-bit I420.
            QByteArray tight(qint64(layout.width) * layout.height * 3 / 2, Qt::Uninitialized);
            auto *dst = reinterpret_cast<uchar *>(tight.data());
            const uchar *planeSrc[3] = {pixels,
                                        pixels + yPlaneBytes,
                                        pixels + yPlaneBytes + chromaPlaneBytes};
            const int planeRows[3] = {layout.height, layout.height / 2, layout.height / 2};
            const int planeSamples[3] = {layout.width, layout.width / 2, layout.width / 2};
            const qint64 planeStride[3] = {layout.stride, chromaStride, chromaStride};
            for (int plane = 0; plane < 3; ++plane) {
                for (int row = 0; row < planeRows[plane]; ++row) {
                    const uchar *src = planeSrc[plane] + qint64(row) * planeStride[plane];
                    for (int col = 0; col < planeSamples[plane]; ++col)
                        *dst++ = uchar(readLe16(src + 2 * col) >> 8);
                }
            }

            cv::Mat yuv(layout.height * 3 / 2, layout.width, CV_8UC1,
                        reinterpret_cast<uchar *>(tight.data()));
            cv::Mat rgba;
            cv::cvtColor(yuv, rgba, cv::COLOR_YUV2RGBA_I420);
            return rgbaMatToImage(rgba, layout);
        });
    }

    QStringList planeNames() const override
    {
        return {QStringLiteral("Y"), QStringLiteral("U"), QStringLiteral("V")};
    }

    ImageResult extractPlane(const QByteArray &data, const RawImageLayout &layout,
                             int plane) const override
    {
        const auto *pixels = reinterpret_cast<const uchar *>(data.constData());
        const qint64 yPlaneBytes = qint64(layout.stride) * layout.scanline;
        const qint64 chromaPlaneBytes = qint64(layout.stride / 2) * (layout.scanline / 2);
        switch (plane) {
        case 0:
            return grayscale16Plane(pixels, layout.width, layout.height, layout.stride);
        case 1:
            return grayscale16Plane(pixels + yPlaneBytes, layout.width / 2,
                                    layout.height / 2, layout.stride / 2);
        case 2:
            return grayscale16Plane(pixels + yPlaneBytes + chromaPlaneBytes,
                                    layout.width / 2, layout.height / 2, layout.stride / 2);
        default:
            return invalidPlane(plane);
        }
    }

    QString describePixel(const QByteArray &data, const RawImageLayout &layout,
                          int x, int y) const override
    {
        const auto *pixels = reinterpret_cast<const uchar *>(data.constData());
        const qint64 yPlaneBytes = qint64(layout.stride) * layout.scanline;
        const qint64 chromaStride = layout.stride / 2;
        const qint64 chromaPlaneBytes = chromaStride * (layout.scanline / 2);
        const int shift = 16 - bitDepth();
        const int luma = readLe16(pixels + qint64(y) * layout.stride + 2 * x) >> shift;
        const int u = readLe16(pixels + yPlaneBytes + qint64(y / 2) * chromaStride
                               + 2 * (x / 2)) >> shift;
        const int v = readLe16(pixels + yPlaneBytes + chromaPlaneBytes
                               + qint64(y / 2) * chromaStride + 2 * (x / 2)) >> shift;
        return describeYuv(luma, u, v);
    }

protected:
    // 10 for I010, 16 for I016. Both are MSB-aligned.
    virtual int bitDepth() const = 0;
};

class I010Decoder final : public PlanarYuv420p16Decoder
{
public:
    QLatin1StringView id() const override { return "i010"_L1; }
    QString displayName() const override { return QStringLiteral("I010"); }
    QString mimeType() const override { return "video/x-raw-i010"_L1; }
    QStringList fileExtensions() const override { return {"i010"_L1, "I010"_L1}; }

protected:
    int bitDepth() const override { return 10; }
};

class I016Decoder final : public PlanarYuv420p16Decoder
{
public:
    QLatin1StringView id() const override { return "i016"_L1; }
    QString displayName() const override { return QStringLiteral("I016"); }
    QString mimeType() const override { return "video/x-raw-i016"_L1; }
    QStringList fileExtensions() const override { return {"i016"_L1, "I016"_L1}; }

protected:
    int bitDepth() const override { return 16; }
};

// Shared implementation for packed (interleaved) YUV 4:2:2 formats:
// a single plane of two-pixel macropixels, four bytes each. Only the
// component order inside the macropixel (YUYV, UYVY, YVYU, VYUY) differs.
class PackedYuv422Decoder : public RawImageDecoder
{
    Q_DECLARE_TR_FUNCTIONS(PackedYuv422Decoder)

public:
    LayoutResult validateLayout(const RawImageLayout &layout) const override
    {
        const LayoutResult baseResult = RawImageDecoder::validateLayout(layout);
        if (!baseResult)
            return baseResult;

        if ((layout.width % 2) != 0) {
            return std::unexpected(tr("%1 width must be even. Received %2.")
                                       .arg(displayName())
                                       .arg(layout.width));
        }
        if (layout.stride < layout.width * 2) {
            return std::unexpected(tr("%1 stride must be at least twice the width. "
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
        // stride is in bytes and already accounts for both pixels of a
        // macropixel: the frame is exactly stride * scanline bytes.
        return qint64(layout.stride) * qint64(layout.scanline);
    }

    int defaultStride(int width) const override { return width * 2; }

    ImageResult convertToImage(const QByteArray &data,
                               const RawImageLayout &layout) const override
    {
        return runConversion(*this, [&]() -> ImageResult {
            auto *pixels = reinterpret_cast<uchar *>(const_cast<char *>(data.constData()));
            cv::Mat yuv(layout.height, layout.width, CV_8UC2, pixels,
                        static_cast<size_t>(layout.stride));
            cv::Mat rgba;
            cv::cvtColor(yuv, rgba, conversionCode());
            return rgbaMatToImage(rgba, layout);
        });
    }

    QStringList planeNames() const override
    {
        return {QStringLiteral("Y"), QStringLiteral("U"), QStringLiteral("V")};
    }

    ImageResult extractPlane(const QByteArray &data, const RawImageLayout &layout,
                             int plane) const override
    {
        const auto *pixels = reinterpret_cast<const uchar *>(data.constData());
        const auto offsets = componentOffsets();
        switch (plane) {
        case 0:
            return stridedPlane(pixels, layout.width, layout.height, layout.stride,
                                2, offsets[0]);
        case 1:
            return stridedPlane(pixels, layout.width / 2, layout.height, layout.stride,
                                4, offsets[1]);
        case 2:
            return stridedPlane(pixels, layout.width / 2, layout.height, layout.stride,
                                4, offsets[2]);
        default:
            return invalidPlane(plane);
        }
    }

    QString describePixel(const QByteArray &data, const RawImageLayout &layout,
                          int x, int y) const override
    {
        const auto *pixels = reinterpret_cast<const uchar *>(data.constData());
        const auto offsets = componentOffsets();
        const uchar *macropixel = pixels + qint64(y) * layout.stride + (x / 2) * 4;
        // The macropixel carries both luma samples two bytes apart; U and V
        // are shared by its two pixels.
        const int luma = macropixel[offsets[0] + (x % 2) * 2];
        return describeYuv(luma, macropixel[offsets[1]], macropixel[offsets[2]]);
    }

protected:
    // cv::COLOR_YUV2RGBA_YUY2 / cv::COLOR_YUV2RGBA_UYVY / ...
    virtual int conversionCode() const = 0;
    // Byte offsets of Y, U and V inside the 4-byte macropixel.
    virtual std::array<int, 3> componentOffsets() const = 0;
};

class Yuy2Decoder final : public PackedYuv422Decoder
{
public:
    QLatin1StringView id() const override { return "yuy2"_L1; }
    QString displayName() const override { return QStringLiteral("YUY2"); }
    QString mimeType() const override { return "video/x-raw-yuy2"_L1; }
    QStringList fileExtensions() const override
    {
        return {"yuy2"_L1, "YUY2"_L1, "yuyv"_L1, "YUYV"_L1};
    }

protected:
    int conversionCode() const override { return cv::COLOR_YUV2RGBA_YUY2; }
    std::array<int, 3> componentOffsets() const override { return {0, 1, 3}; }
};

class UyvyDecoder final : public PackedYuv422Decoder
{
public:
    QLatin1StringView id() const override { return "uyvy"_L1; }
    QString displayName() const override { return QStringLiteral("UYVY"); }
    QString mimeType() const override { return "video/x-raw-uyvy"_L1; }
    QStringList fileExtensions() const override { return {"uyvy"_L1, "UYVY"_L1}; }

protected:
    int conversionCode() const override { return cv::COLOR_YUV2RGBA_UYVY; }
    std::array<int, 3> componentOffsets() const override { return {1, 0, 2}; }
};

class YvyuDecoder final : public PackedYuv422Decoder
{
public:
    QLatin1StringView id() const override { return "yvyu"_L1; }
    QString displayName() const override { return QStringLiteral("YVYU"); }
    QString mimeType() const override { return "video/x-raw-yvyu"_L1; }
    QStringList fileExtensions() const override { return {"yvyu"_L1, "YVYU"_L1}; }

protected:
    int conversionCode() const override { return cv::COLOR_YUV2RGBA_YVYU; }
    std::array<int, 3> componentOffsets() const override { return {0, 3, 1}; }
};

class VyuyDecoder final : public PackedYuv422Decoder
{
public:
    QLatin1StringView id() const override { return "vyuy"_L1; }
    QString displayName() const override { return QStringLiteral("VYUY"); }
    QString mimeType() const override { return "video/x-raw-vyuy"_L1; }
    QStringList fileExtensions() const override { return {"vyuy"_L1, "VYUY"_L1}; }

    ImageResult convertToImage(const QByteArray &data,
                               const RawImageLayout &layout) const override
    {
        // OpenCV comments COLOR_YUV2RGBA_VYUY out and does not implement
        // it. VYUY is UYVY with U and V swapped (V Y U Y -> U Y V Y).
        return runConversion(*this, [&]() -> ImageResult {
            QByteArray uyvy(qint64(layout.stride) * layout.height, Qt::Uninitialized);
            const auto *src = reinterpret_cast<const uchar *>(data.constData());
            auto *converted = reinterpret_cast<uchar *>(uyvy.data());
            for (int row = 0; row < layout.height; ++row) {
                const uchar *s = src + qint64(row) * layout.stride;
                uchar *d = converted + qint64(row) * layout.stride;
                memcpy(d, s, static_cast<size_t>(layout.stride));
                for (int col = 0; col < layout.width; col += 2) {
                    const uchar v = d[0];
                    d[0] = d[2];
                    d[2] = v;
                    d += 4;
                }
            }
            cv::Mat yuv(layout.height, layout.width, CV_8UC2, converted,
                        static_cast<size_t>(layout.stride));
            cv::Mat rgba;
            cv::cvtColor(yuv, rgba, cv::COLOR_YUV2RGBA_UYVY);
            return rgbaMatToImage(rgba, layout);
        });
    }

protected:
    // Unused: convertToImage() is overridden. Kept to satisfy the base.
    int conversionCode() const override { return cv::COLOR_YUV2RGBA_UYVY; }
    std::array<int, 3> componentOffsets() const override { return {1, 2, 0}; }
};

// Shared implementation for three-plane (planar) YUV 4:2:2 formats:
// a full-resolution Y plane followed by the horizontally subsampled
// U and V planes, each with half the stride and all lines of the Y
// plane. Only the chroma plane order (U,V vs V,U) differs between them.
class PlanarYuv422Decoder : public RawImageDecoder
{
public:
    LayoutResult validateLayout(const RawImageLayout &layout) const override
    {
        return validateYuv422Layout(*this, layout);
    }

    qint64 expectedByteSize(const RawImageLayout &layout) const override
    {
        return qint64(layout.stride) * qint64(layout.scanline) * 2;
    }

    ImageResult convertToImage(const QByteArray &data,
                               const RawImageLayout &layout) const override
    {
        return runConversion(*this, [&]() -> ImageResult {
            const auto *pixels = reinterpret_cast<const uchar *>(data.constData());
            const qint64 yPlaneBytes = qint64(layout.stride) * layout.scanline;
            const qint64 chromaPlaneBytes = qint64(layout.stride / 2) * layout.scanline;
            const uchar *uPlane = pixels + yPlaneBytes
                                  + (chromaOrderIsUV() ? 0 : chromaPlaneBytes);
            const uchar *vPlane = pixels + yPlaneBytes
                                  + (chromaOrderIsUV() ? chromaPlaneBytes : 0);

            // Repack into a YUY2 macropixel stream for OpenCV.
            QByteArray yuy2(qint64(layout.width) * layout.height * 2, Qt::Uninitialized);
            auto *dst = reinterpret_cast<uchar *>(yuy2.data());
            const int pairsPerRow = layout.width / 2;
            for (int row = 0; row < layout.height; ++row) {
                const uchar *yRow = pixels + qint64(row) * layout.stride;
                const uchar *uRow = uPlane + qint64(row) * (layout.stride / 2);
                const uchar *vRow = vPlane + qint64(row) * (layout.stride / 2);
                uchar *out = dst + qint64(row) * layout.width * 2;
                for (int pair = 0; pair < pairsPerRow; ++pair) {
                    out[4 * pair]     = yRow[2 * pair];
                    out[4 * pair + 1] = uRow[pair];
                    out[4 * pair + 2] = yRow[2 * pair + 1];
                    out[4 * pair + 3] = vRow[pair];
                }
            }

            cv::Mat yuv(layout.height, layout.width, CV_8UC2, yuy2.data(),
                        static_cast<size_t>(layout.width) * 2);
            cv::Mat rgba;
            cv::cvtColor(yuv, rgba, cv::COLOR_YUV2RGBA_YUY2);
            return rgbaMatToImage(rgba, layout);
        });
    }

    QStringList planeNames() const override
    {
        return {QStringLiteral("Y"), QStringLiteral("U"), QStringLiteral("V")};
    }

    ImageResult extractPlane(const QByteArray &data, const RawImageLayout &layout,
                             int plane) const override
    {
        const auto *pixels = reinterpret_cast<const uchar *>(data.constData());
        const qint64 yPlaneBytes = qint64(layout.stride) * layout.scanline;
        const qint64 chromaPlaneBytes = qint64(layout.stride / 2) * layout.scanline;
        switch (plane) {
        case 0:
            return grayscalePlane(pixels, layout.width, layout.height, layout.stride);
        case 1:
            return grayscalePlane(pixels + yPlaneBytes
                                      + (chromaOrderIsUV() ? 0 : chromaPlaneBytes),
                                  layout.width / 2, layout.height, layout.stride / 2);
        case 2:
            return grayscalePlane(pixels + yPlaneBytes
                                      + (chromaOrderIsUV() ? chromaPlaneBytes : 0),
                                  layout.width / 2, layout.height, layout.stride / 2);
        default:
            return invalidPlane(plane);
        }
    }

    QString describePixel(const QByteArray &data, const RawImageLayout &layout,
                          int x, int y) const override
    {
        const auto *pixels = reinterpret_cast<const uchar *>(data.constData());
        const qint64 yPlaneBytes = qint64(layout.stride) * layout.scanline;
        const qint64 chromaPlaneBytes = qint64(layout.stride / 2) * layout.scanline;
        const uchar *uPlane = pixels + yPlaneBytes
                              + (chromaOrderIsUV() ? 0 : chromaPlaneBytes);
        const uchar *vPlane = pixels + yPlaneBytes
                              + (chromaOrderIsUV() ? chromaPlaneBytes : 0);
        const int luma = pixels[qint64(y) * layout.stride + x];
        const int u = uPlane[qint64(y) * (layout.stride / 2) + x / 2];
        const int v = vPlane[qint64(y) * (layout.stride / 2) + x / 2];
        return describeYuv(luma, u, v);
    }

protected:
    // I422 stores Y,U,V planes; YV16 stores Y,V,U.
    virtual bool chromaOrderIsUV() const = 0;
};

class I422Decoder final : public PlanarYuv422Decoder
{
public:
    QLatin1StringView id() const override { return "i422"_L1; }
    QString displayName() const override { return QStringLiteral("I422"); }
    QString mimeType() const override { return "video/x-raw-i422"_L1; }
    QStringList fileExtensions() const override {
        return {"i422"_L1, "I422"_L1, "yuv422p"_L1, "YUV422P"_L1};
    }

protected:
    bool chromaOrderIsUV() const override { return true; }
};

class Yv16Decoder final : public PlanarYuv422Decoder
{
public:
    QLatin1StringView id() const override { return "yv16"_L1; }
    QString displayName() const override { return QStringLiteral("YV16"); }
    QString mimeType() const override { return "video/x-raw-yv16"_L1; }
    QStringList fileExtensions() const override { return {"yv16"_L1, "YV16"_L1}; }

protected:
    bool chromaOrderIsUV() const override { return false; }
};

// Shared implementation for two-plane (semi-planar) YUV 4:2:2 formats:
// a full-resolution Y plane followed by an interleaved chroma plane
// with half the horizontal resolution and all lines of the Y plane.
// Only the chroma byte order (UV vs VU) differs between them.
class SemiPlanarYuv422Decoder : public RawImageDecoder
{
public:
    LayoutResult validateLayout(const RawImageLayout &layout) const override
    {
        return validateYuv422Layout(*this, layout);
    }

    qint64 expectedByteSize(const RawImageLayout &layout) const override
    {
        return qint64(layout.stride) * qint64(layout.scanline) * 2;
    }

    ImageResult convertToImage(const QByteArray &data,
                               const RawImageLayout &layout) const override
    {
        return runConversion(*this, [&]() -> ImageResult {
            const auto *pixels = reinterpret_cast<const uchar *>(data.constData());
            const uchar *uvPlane = pixels + qint64(layout.stride) * layout.scanline;
            const int uOffset = chromaOrderIsUV() ? 0 : 1;

            // Repack into a YUY2 macropixel stream for OpenCV.
            QByteArray yuy2(qint64(layout.width) * layout.height * 2, Qt::Uninitialized);
            auto *dst = reinterpret_cast<uchar *>(yuy2.data());
            const int pairsPerRow = layout.width / 2;
            for (int row = 0; row < layout.height; ++row) {
                const uchar *yRow = pixels + qint64(row) * layout.stride;
                const uchar *uvRow = uvPlane + qint64(row) * layout.stride;
                uchar *out = dst + qint64(row) * layout.width * 2;
                for (int pair = 0; pair < pairsPerRow; ++pair) {
                    out[4 * pair]     = yRow[2 * pair];
                    out[4 * pair + 1] = uvRow[2 * pair + uOffset];
                    out[4 * pair + 2] = yRow[2 * pair + 1];
                    out[4 * pair + 3] = uvRow[2 * pair + (1 - uOffset)];
                }
            }

            cv::Mat yuv(layout.height, layout.width, CV_8UC2, yuy2.data(),
                        static_cast<size_t>(layout.width) * 2);
            cv::Mat rgba;
            cv::cvtColor(yuv, rgba, cv::COLOR_YUV2RGBA_YUY2);
            return rgbaMatToImage(rgba, layout);
        });
    }

    QStringList planeNames() const override
    {
        return {QStringLiteral("Y"), QStringLiteral("U"), QStringLiteral("V")};
    }

    ImageResult extractPlane(const QByteArray &data, const RawImageLayout &layout,
                             int plane) const override
    {
        const auto *pixels = reinterpret_cast<const uchar *>(data.constData());
        const qint64 yPlaneBytes = qint64(layout.stride) * layout.scanline;
        switch (plane) {
        case 0:
            return grayscalePlane(pixels, layout.width, layout.height, layout.stride);
        case 1:
        case 2: {
            const int offset = ((plane == 1) == chromaOrderIsUV()) ? 0 : 1;
            return stridedPlane(pixels + yPlaneBytes, layout.width / 2, layout.height,
                                layout.stride, 2, offset);
        }
        default:
            return invalidPlane(plane);
        }
    }

    QString describePixel(const QByteArray &data, const RawImageLayout &layout,
                          int x, int y) const override
    {
        const auto *pixels = reinterpret_cast<const uchar *>(data.constData());
        const qint64 yPlaneBytes = qint64(layout.stride) * layout.scanline;
        const int luma = pixels[qint64(y) * layout.stride + x];
        const uchar *chroma = pixels + yPlaneBytes + qint64(y) * layout.stride + (x / 2) * 2;
        const int u = chroma[chromaOrderIsUV() ? 0 : 1];
        const int v = chroma[chromaOrderIsUV() ? 1 : 0];
        return describeYuv(luma, u, v);
    }

protected:
    // NV16 interleaves U,V; NV61 interleaves V,U.
    virtual bool chromaOrderIsUV() const = 0;
};

class Nv16Decoder final : public SemiPlanarYuv422Decoder
{
public:
    QLatin1StringView id() const override { return "nv16"_L1; }
    QString displayName() const override { return QStringLiteral("NV16"); }
    QString mimeType() const override { return "video/x-raw-nv16"_L1; }
    QStringList fileExtensions() const override { return {"nv16"_L1, "NV16"_L1}; }

protected:
    bool chromaOrderIsUV() const override { return true; }
};

class Nv61Decoder final : public SemiPlanarYuv422Decoder
{
public:
    QLatin1StringView id() const override { return "nv61"_L1; }
    QString displayName() const override { return QStringLiteral("NV61"); }
    QString mimeType() const override { return "video/x-raw-nv61"_L1; }
    QStringList fileExtensions() const override { return {"nv61"_L1, "NV61"_L1}; }

protected:
    bool chromaOrderIsUV() const override { return false; }
};

// Shared implementation for three-plane (planar) YUV 4:4:4 formats:
// full-resolution Y, U and V planes with no subsampling. Only the
// chroma plane order (U,V vs V,U) differs between them.
class PlanarYuv444Decoder : public RawImageDecoder
{
public:
    LayoutResult validateLayout(const RawImageLayout &layout) const override
    {
        return validateYuv444Layout(*this, layout);
    }

    qint64 expectedByteSize(const RawImageLayout &layout) const override
    {
        return qint64(layout.stride) * qint64(layout.scanline) * 3;
    }

    ImageResult convertToImage(const QByteArray &data,
                               const RawImageLayout &layout) const override
    {
        return runConversion(*this, [&]() -> ImageResult {
            auto *pixels = reinterpret_cast<uchar *>(const_cast<char *>(data.constData()));
            const qint64 planeBytes = qint64(layout.stride) * layout.scanline;
            const size_t step = static_cast<size_t>(layout.stride);

            cv::Mat yPlane(layout.height, layout.width, CV_8UC1, pixels, step);
            cv::Mat uPlane(layout.height, layout.width, CV_8UC1,
                           pixels + (chromaOrderIsUV() ? planeBytes : 2 * planeBytes), step);
            cv::Mat vPlane(layout.height, layout.width, CV_8UC1,
                           pixels + (chromaOrderIsUV() ? 2 * planeBytes : planeBytes), step);

            const cv::Mat planes[3] = {yPlane, uPlane, vPlane};
            cv::Mat yuv;
            cv::merge(planes, 3, yuv);

            // OpenCV's three-channel YUV conversion has no RGBA variant.
            cv::Mat rgb;
            cv::cvtColor(yuv, rgb, cv::COLOR_YUV2RGB);
            return rgbaMatToImage(rgb, layout, QImage::Format_RGB888);
        });
    }

    QStringList planeNames() const override
    {
        return {QStringLiteral("Y"), QStringLiteral("U"), QStringLiteral("V")};
    }

    ImageResult extractPlane(const QByteArray &data, const RawImageLayout &layout,
                             int plane) const override
    {
        const auto *pixels = reinterpret_cast<const uchar *>(data.constData());
        const qint64 planeBytes = qint64(layout.stride) * layout.scanline;
        switch (plane) {
        case 0:
            return grayscalePlane(pixels, layout.width, layout.height, layout.stride);
        case 1:
            return grayscalePlane(pixels + (chromaOrderIsUV() ? planeBytes : 2 * planeBytes),
                                  layout.width, layout.height, layout.stride);
        case 2:
            return grayscalePlane(pixels + (chromaOrderIsUV() ? 2 * planeBytes : planeBytes),
                                  layout.width, layout.height, layout.stride);
        default:
            return invalidPlane(plane);
        }
    }

    QString describePixel(const QByteArray &data, const RawImageLayout &layout,
                          int x, int y) const override
    {
        const auto *pixels = reinterpret_cast<const uchar *>(data.constData());
        const qint64 planeBytes = qint64(layout.stride) * layout.scanline;
        const uchar *uPlane = pixels + (chromaOrderIsUV() ? planeBytes : 2 * planeBytes);
        const uchar *vPlane = pixels + (chromaOrderIsUV() ? 2 * planeBytes : planeBytes);
        const int luma = pixels[qint64(y) * layout.stride + x];
        const int u = uPlane[qint64(y) * layout.stride + x];
        const int v = vPlane[qint64(y) * layout.stride + x];
        return describeYuv(luma, u, v);
    }

protected:
    // I444 stores Y,U,V planes; YV24 stores Y,V,U.
    virtual bool chromaOrderIsUV() const = 0;
};

class I444Decoder final : public PlanarYuv444Decoder
{
public:
    QLatin1StringView id() const override { return "i444"_L1; }
    QString displayName() const override { return QStringLiteral("I444"); }
    QString mimeType() const override { return "video/x-raw-i444"_L1; }
    QStringList fileExtensions() const override { return {"i444"_L1, "I444"_L1, "yuv444p"_L1, "YUV444P"_L1}; }

protected:
    bool chromaOrderIsUV() const override { return true; }
};

class Yv24Decoder final : public PlanarYuv444Decoder
{
public:
    QLatin1StringView id() const override { return "yv24"_L1; }
    QString displayName() const override { return QStringLiteral("YV24"); }
    QString mimeType() const override { return "video/x-raw-yv24"_L1; }
    QStringList fileExtensions() const override { return {"yv24"_L1, "YV24"_L1}; }

protected:
    bool chromaOrderIsUV() const override { return false; }
};

// Shared implementation for two-plane (semi-planar) YUV 4:4:4 formats:
// a full-resolution Y plane followed by an interleaved chroma plane
// that is not subsampled at all, so it carries a U and a V sample per
// pixel and its rows are twice as wide as the Y rows. Only the chroma
// byte order (UV vs VU) differs between them.
class SemiPlanarYuv444Decoder : public RawImageDecoder
{
public:
    LayoutResult validateLayout(const RawImageLayout &layout) const override
    {
        return validateYuv444Layout(*this, layout);
    }

    qint64 expectedByteSize(const RawImageLayout &layout) const override
    {
        // The chroma plane is twice the size of the Y plane.
        return qint64(layout.stride) * qint64(layout.scanline) * 3;
    }

    ImageResult convertToImage(const QByteArray &data,
                               const RawImageLayout &layout) const override
    {
        return runConversion(*this, [&]() -> ImageResult {
            auto *pixels = reinterpret_cast<uchar *>(const_cast<char *>(data.constData()));
            const qint64 yPlaneBytes = qint64(layout.stride) * layout.scanline;

            cv::Mat yPlane(layout.height, layout.width, CV_8UC1, pixels,
                           static_cast<size_t>(layout.stride));
            cv::Mat uvPlane(layout.height, layout.width, CV_8UC2, pixels + yPlaneBytes,
                            static_cast<size_t>(layout.stride) * 2);

            // cvtColorTwoPlane() only knows the subsampled 4:2:0 layouts,
            // so the chroma is deinterleaved and merged into a plain
            // three-channel YUV image instead.
            cv::Mat chroma[2];
            cv::split(uvPlane, chroma);
            const cv::Mat planes[3] = {yPlane,
                                       chroma[chromaOrderIsUV() ? 0 : 1],
                                       chroma[chromaOrderIsUV() ? 1 : 0]};
            cv::Mat yuv;
            cv::merge(planes, 3, yuv);

            // OpenCV's three-channel YUV conversion has no RGBA variant.
            cv::Mat rgb;
            cv::cvtColor(yuv, rgb, cv::COLOR_YUV2RGB);
            return rgbaMatToImage(rgb, layout, QImage::Format_RGB888);
        });
    }

    QStringList planeNames() const override
    {
        return {QStringLiteral("Y"), QStringLiteral("U"), QStringLiteral("V")};
    }

    ImageResult extractPlane(const QByteArray &data, const RawImageLayout &layout,
                             int plane) const override
    {
        const auto *pixels = reinterpret_cast<const uchar *>(data.constData());
        const qint64 yPlaneBytes = qint64(layout.stride) * layout.scanline;
        switch (plane) {
        case 0:
            return grayscalePlane(pixels, layout.width, layout.height, layout.stride);
        case 1:
        case 2: {
            const int offset = ((plane == 1) == chromaOrderIsUV()) ? 0 : 1;
            return stridedPlane(pixels + yPlaneBytes, layout.width, layout.height,
                                qint64(layout.stride) * 2, 2, offset);
        }
        default:
            return invalidPlane(plane);
        }
    }

    QString describePixel(const QByteArray &data, const RawImageLayout &layout,
                          int x, int y) const override
    {
        const auto *pixels = reinterpret_cast<const uchar *>(data.constData());
        const qint64 yPlaneBytes = qint64(layout.stride) * layout.scanline;
        const int luma = pixels[qint64(y) * layout.stride + x];
        const uchar *chroma = pixels + yPlaneBytes + qint64(y) * layout.stride * 2 + x * 2;
        const int u = chroma[chromaOrderIsUV() ? 0 : 1];
        const int v = chroma[chromaOrderIsUV() ? 1 : 0];
        return describeYuv(luma, u, v);
    }

protected:
    // NV24 interleaves U,V; NV42 interleaves V,U.
    virtual bool chromaOrderIsUV() const = 0;
};

class Nv24Decoder final : public SemiPlanarYuv444Decoder
{
public:
    QLatin1StringView id() const override { return "nv24"_L1; }
    QString displayName() const override { return QStringLiteral("NV24"); }
    QString mimeType() const override { return "video/x-raw-nv24"_L1; }
    QStringList fileExtensions() const override { return {"nv24"_L1, "NV24"_L1}; }

protected:
    bool chromaOrderIsUV() const override { return true; }
};

class Nv42Decoder final : public SemiPlanarYuv444Decoder
{
public:
    QLatin1StringView id() const override { return "nv42"_L1; }
    QString displayName() const override { return QStringLiteral("NV42"); }
    QString mimeType() const override { return "video/x-raw-nv42"_L1; }
    QStringList fileExtensions() const override { return {"nv42"_L1, "NV42"_L1}; }

protected:
    bool chromaOrderIsUV() const override { return false; }
};

// Shared implementation for packed (interleaved) YUV 4:4:4 formats: a
// single plane whose pixels carry all of their own components, so there
// is neither subsampling nor a second plane and every pixel is
// independent. Only the component order, the pixel size and the presence
// of an alpha byte differ between them.
class PackedYuv444Decoder : public RawImageDecoder
{
public:
    LayoutResult validateLayout(const RawImageLayout &layout) const override
    {
        return validateYuv444Layout(*this, layout, bytesPerPixel());
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
        return runConversion(*this, [&]() -> ImageResult {
            auto *pixels = reinterpret_cast<uchar *>(const_cast<char *>(data.constData()));
            cv::Mat packed(layout.height, layout.width,
                           CV_MAKETYPE(CV_8U, bytesPerPixel()), pixels,
                           static_cast<size_t>(layout.stride));

            // OpenCV has no packed 4:4:4 conversion, so the components are
            // deinterleaved and reordered into a plain three-channel YUV
            // image. components[] is sized for the widest pixel here.
            cv::Mat components[4];
            cv::split(packed, components);
            const auto offsets = componentOffsets();
            const cv::Mat yuvPlanes[3] = {components[offsets[0]],
                                          components[offsets[1]],
                                          components[offsets[2]]};
            cv::Mat yuv;
            cv::merge(yuvPlanes, 3, yuv);

            // OpenCV's three-channel YUV conversion has no RGBA variant.
            cv::Mat rgb;
            cv::cvtColor(yuv, rgb, cv::COLOR_YUV2RGB);
            if (alphaOffset() < 0)
                return rgbaMatToImage(rgb, layout, QImage::Format_RGB888);

            // Alpha passes through untouched: it is not a color component,
            // so it is put back only after the color conversion.
            cv::Mat rgbPlanes[3];
            cv::split(rgb, rgbPlanes);
            const cv::Mat rgbaPlanes[4] = {rgbPlanes[0], rgbPlanes[1], rgbPlanes[2],
                                           components[alphaOffset()]};
            cv::Mat rgba;
            cv::merge(rgbaPlanes, 4, rgba);
            return rgbaMatToImage(rgba, layout);
        });
    }

    QStringList planeNames() const override
    {
        QStringList names{QStringLiteral("Y"), QStringLiteral("U"), QStringLiteral("V")};
        if (alphaOffset() >= 0)
            names << QStringLiteral("A");
        return names;
    }

    ImageResult extractPlane(const QByteArray &data, const RawImageLayout &layout,
                             int plane) const override
    {
        if (plane < 0 || plane >= planeNames().size())
            return invalidPlane(plane);

        const auto *pixels = reinterpret_cast<const uchar *>(data.constData());
        const int offset = plane == 3 ? alphaOffset() : componentOffsets()[plane];
        return stridedPlane(pixels, layout.width, layout.height, layout.stride,
                            bytesPerPixel(), offset);
    }

    QString describePixel(const QByteArray &data, const RawImageLayout &layout,
                          int x, int y) const override
    {
        const auto *pixels = reinterpret_cast<const uchar *>(data.constData());
        const auto offsets = componentOffsets();
        const uchar *pixel = pixels + qint64(y) * layout.stride + x * bytesPerPixel();
        const int luma = pixel[offsets[0]];
        const int u = pixel[offsets[1]];
        const int v = pixel[offsets[2]];
        if (alphaOffset() >= 0)
            return describeYuva(luma, u, v, pixel[alphaOffset()]);
        return describeYuv(luma, u, v);
    }

protected:
    // 3 for YUV444, 4 for AYUV. Must not exceed the size of components[]
    // in convertToImage().
    virtual int bytesPerPixel() const = 0;
    // Byte offsets of Y, U and V inside one pixel.
    virtual std::array<int, 3> componentOffsets() const = 0;
    // Byte offset of the alpha component, or -1 if the format has none.
    virtual int alphaOffset() const { return -1; }
};

class Yuv444Decoder final : public PackedYuv444Decoder
{
public:
    QLatin1StringView id() const override { return "yuv444"_L1; }
    QString displayName() const override { return QStringLiteral("YUV444"); }
    QString mimeType() const override { return "video/x-raw-yuv444"_L1; }
    QStringList fileExtensions() const override
    {
        // Not "yuv444p": that names the planar arrangement, which is I444.
        return {"yuv444"_L1, "YUV444"_L1, "iyu2"_L1, "IYU2"_L1, "v308"_L1, "V308"_L1};
    }

protected:
    int bytesPerPixel() const override { return 3; }
    std::array<int, 3> componentOffsets() const override { return {0, 1, 2}; }
};

class AyuvDecoder final : public PackedYuv444Decoder
{
public:
    QLatin1StringView id() const override { return "ayuv"_L1; }
    QString displayName() const override { return QStringLiteral("AYUV"); }
    QString mimeType() const override { return "video/x-raw-ayuv"_L1; }
    QStringList fileExtensions() const override
    {
        return {"ayuv"_L1, "AYUV"_L1, "vuya"_L1, "VUYA"_L1};
    }

protected:
    int bytesPerPixel() const override { return 4; }
    // The FourCC names the components of a little-endian 32-bit word from
    // the most significant byte down, so in memory they run V,U,Y,A --
    // which is why FFmpeg calls this same layout VUYA.
    std::array<int, 3> componentOffsets() const override { return {2, 1, 0}; }
    int alphaOffset() const override { return 3; }
};

} // namespace

QList<const RawImageDecoder *> RawImageDecoders::createYuvDecoders()
{
    return {
        new Nv12Decoder,
        new Nv21Decoder,
        new P010Decoder,
        new P016Decoder,
        new I420Decoder,
        new Yv12Decoder,
        new I010Decoder,
        new I016Decoder,
        new Yuy2Decoder,
        new UyvyDecoder,
        new YvyuDecoder,
        new VyuyDecoder,
        new I422Decoder,
        new Yv16Decoder,
        new Nv16Decoder,
        new Nv61Decoder,
        new I444Decoder,
        new Yv24Decoder,
        new Nv24Decoder,
        new Nv42Decoder,
        new Yuv444Decoder,
        new AyuvDecoder,
    };
}
