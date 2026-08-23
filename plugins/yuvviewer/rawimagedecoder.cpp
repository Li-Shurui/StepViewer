// Copyright (C) 2026
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#include "rawimagedecoder.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <QFile>

#include <cstring>

using namespace Qt::StringLiterals;

RawImageDecoder::LayoutResult RawImageDecoder::validateLayout(const RawImageLayout &layout) const
{
    if (layout.width < minimumDimension || layout.width > maximumDimension
        || layout.height < minimumDimension || layout.height > maximumDimension) {
        return std::unexpected(tr("Width and height must be between %1 and %2.")
                                   .arg(minimumDimension)
                                   .arg(maximumDimension));
    }
    if (layout.stride > maximumDimension || layout.scanline > maximumDimension) {
        return std::unexpected(tr("Stride and scanline must not exceed %1.")
                                   .arg(maximumDimension));
    }

    return layout;
}

int RawImageDecoder::defaultStride(int width) const
{
    return width;
}

RawImageDecoder::DataResult RawImageDecoder::readData(const QString &fileName,
                                                      const RawImageLayout &layout,
                                                      const ProgressCallback &progress) const
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        return std::unexpected(tr("Cannot open the file: %1")
                                   .arg(file.errorString()));
    }

    const qint64 expectedSize = expectedByteSize(layout);
    const qint64 actualSize = file.size();
    if (actualSize != expectedSize) {
        return std::unexpected(
            tr("File size does not match the %1 layout. "
               "Expected %2 bytes, found %3 bytes "
               "(width=%4, height=%5, stride=%6, scanline=%7).")
                .arg(displayName())
                .arg(expectedSize)
                .arg(actualSize)
                .arg(layout.width)
                .arg(layout.height)
                .arg(layout.stride)
                .arg(layout.scanline));
    }

    constexpr qint64 chunkSize = 4 * 1024 * 1024;
    QByteArray data(expectedSize, Qt::Uninitialized);
    qint64 offset = 0;
    while (offset < expectedSize) {
        const qint64 bytesRead = file.read(data.data() + offset,
                                           qMin(chunkSize, expectedSize - offset));
        if (bytesRead < 0) {
            return std::unexpected(tr("Failed while reading the file: %1")
                                       .arg(file.errorString()));
        }
        if (bytesRead == 0)
            break;  // the file shrank mid-read; reported below
        offset += bytesRead;
        if (progress && !progress(offset, expectedSize))
            return std::unexpected(tr("Loading canceled."));
    }

    if (offset != expectedSize) {
        return std::unexpected(tr("The file read was incomplete. "
                                  "Expected %1 bytes, received %2 bytes.")
                                   .arg(expectedSize)
                                   .arg(offset));
    }

    return data;
}

namespace {

// Shared layout validation for 4:2:0 formats (semi-planar and planar):
// 2x2 chroma subsampling requires even dimensions, stride and scanline.
RawImageDecoder::LayoutResult validateYuv420Layout(const RawImageDecoder &decoder,
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

// Wraps a converted RGBA Mat into a detached QImage.
RawImageDecoder::ImageResult rgbaMatToImage(const cv::Mat &rgba, const RawImageLayout &layout)
{
    if (rgba.empty() || rgba.cols != layout.width || rgba.rows != layout.height) {
        return std::unexpected(RawImageDecoder::tr(
            "OpenCV returned an empty image or unexpected dimensions."));
    }

    const QImage wrappedImage(rgba.data, rgba.cols, rgba.rows,
                              static_cast<qsizetype>(rgba.step),
                              QImage::Format_RGBA8888);
    QImage image = wrappedImage.copy();
    if (image.isNull())
        return std::unexpected(RawImageDecoder::tr("Could not allocate the converted QImage."));
    return image;
}

// Runs a YUV->RGBA conversion, mapping exceptions to error results.
template <typename Conversion>
RawImageDecoder::ImageResult runConversion(const RawImageDecoder &decoder,
                                           Conversion &&conversion)
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

protected:
    // cv::COLOR_YUV2RGBA_NV12 / cv::COLOR_YUV2RGBA_NV21 / ...
    virtual int conversionCode() const = 0;
};

class Nv12Decoder final : public SemiPlanarYuv420Decoder
{
public:
    QLatin1StringView id() const override { return "nv12"_L1; }
    QString displayName() const override { return QStringLiteral("NV12"); }
    QString mimeType() const override { return "video/x-raw-nv12"_L1; }
    QStringList fileExtensions() const override { return {"nv12"_L1, "NV12"_L1}; }

protected:
    int conversionCode() const override { return cv::COLOR_YUV2RGBA_NV12; }
};

class Nv21Decoder final : public SemiPlanarYuv420Decoder
{
public:
    QLatin1StringView id() const override { return "nv21"_L1; }
    QString displayName() const override { return QStringLiteral("NV21"); }
    QString mimeType() const override { return "video/x-raw-nv21"_L1; }
    QStringList fileExtensions() const override { return {"nv21"_L1, "NV21"_L1}; }

protected:
    int conversionCode() const override { return cv::COLOR_YUV2RGBA_NV21; }
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

protected:
    // cv::COLOR_YUV2RGBA_I420 / cv::COLOR_YUV2RGBA_YV12 / ...
    virtual int conversionCode() const = 0;
};

class I420Decoder final : public PlanarYuv420Decoder
{
public:
    QLatin1StringView id() const override { return "i420"_L1; }
    QString displayName() const override { return QStringLiteral("I420"); }
    QString mimeType() const override { return "video/x-raw-i420"_L1; }
    QStringList fileExtensions() const override { return {"i420"_L1, "I420"_L1}; }

protected:
    int conversionCode() const override { return cv::COLOR_YUV2RGBA_I420; }
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
};

// Shared implementation for packed (interleaved) YUV 4:2:2 formats:
// a single plane of two-pixel macropixels, four bytes each. Only the
// component order inside the macropixel (YUYV, UYVY, YVYU) differs.
class PackedYuv422Decoder : public RawImageDecoder
{
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
        return qint64(layout.stride) * qint64(layout.scanline) * 2;
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

protected:
    // cv::COLOR_YUV2RGBA_YUY2 / cv::COLOR_YUV2RGBA_UYVY / ...
    virtual int conversionCode() const = 0;
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
        const LayoutResult baseResult = RawImageDecoder::validateLayout(layout);
        if (!baseResult)
            return baseResult;

        if ((layout.width % 2) != 0) {
            return std::unexpected(tr("%1 width must be even. Received %2.")
                                       .arg(displayName())
                                       .arg(layout.width));
        }
        if (layout.stride < layout.width || (layout.stride % 2) != 0) {
            return std::unexpected(tr("%1 stride must be even and at least the width. "
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
    QStringList fileExtensions() const override { return {"i422"_L1, "I422"_L1}; }

protected:
    bool chromaOrderIsUV() const override { return true; }
};

// Single-plane 8-bit grayscale: Y samples only, no chroma, so no
// subsampling alignment constraints apply.
class Y8Decoder final : public RawImageDecoder
{
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
};

} // namespace

namespace RawImageDecoders {

const QList<const RawImageDecoder *> &all()
{
    // Never deleted; the registry lives as long as the plugin library.
    static const QList<const RawImageDecoder *> decoders = {
        new Nv12Decoder,
        new Nv21Decoder,
        new I420Decoder,
        new Yv12Decoder,
        new Yuy2Decoder,
        new UyvyDecoder,
        new YvyuDecoder,
        new I422Decoder,
        new Y8Decoder,
    };
    return decoders;
}

const RawImageDecoder *defaultDecoder()
{
    return all().constFirst();
}

const RawImageDecoder *findByExtension(const QString &extension)
{
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
    for (const RawImageDecoder *decoder : all()) {
        if (decoder->id() == id)
            return decoder;
    }
    return nullptr;
}

} // namespace RawImageDecoders
