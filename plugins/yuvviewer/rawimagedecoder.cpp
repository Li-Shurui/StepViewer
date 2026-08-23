// Copyright (C) 2026
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#include "rawimagedecoder.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <QFile>

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

RawImageDecoder::DataResult RawImageDecoder::readData(const QString &fileName,
                                                      const RawImageLayout &layout) const
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

    QByteArray data = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        return std::unexpected(tr("Failed while reading the file: %1")
                                   .arg(file.errorString()));
    }
    if (data.size() != expectedSize) {
        return std::unexpected(tr("The file read was incomplete. "
                                  "Expected %1 bytes, received %2 bytes.")
                                   .arg(expectedSize)
                                   .arg(data.size()));
    }

    return data;
}

namespace {

// Shared implementation for two-plane (semi-planar) YUV 4:2:0 formats:
// a full-resolution Y plane followed by an interleaved 2x2-subsampled
// chroma plane. Only the chroma order (UV vs VU) differs between them.
class SemiPlanarYuv420Decoder : public RawImageDecoder
{
public:
    LayoutResult validateLayout(const RawImageLayout &layout) const override
    {
        const LayoutResult baseResult = RawImageDecoder::validateLayout(layout);
        if (!baseResult)
            return baseResult;

        if ((layout.width % 2) != 0 || (layout.height % 2) != 0) {
            return std::unexpected(tr("%1 width and height must both be even. "
                                      "Received %2x%3.")
                                       .arg(displayName())
                                       .arg(layout.width)
                                       .arg(layout.height));
        }
        if (layout.stride < layout.width || (layout.stride % 2) != 0) {
            return std::unexpected(tr("%1 stride must be even and at least the width. "
                                      "Received width %2, stride %3.")
                                       .arg(displayName())
                                       .arg(layout.width)
                                       .arg(layout.stride));
        }
        if (layout.scanline < layout.height || (layout.scanline % 2) != 0) {
            return std::unexpected(tr("%1 scanline must be even and at least the height. "
                                      "Received height %2, scanline %3.")
                                       .arg(displayName())
                                       .arg(layout.height)
                                       .arg(layout.scanline));
        }

        return layout;
    }

    qint64 expectedByteSize(const RawImageLayout &layout) const override
    {
        return qint64(layout.stride) * qint64(layout.scanline) * 3 / 2;
    }

    ImageResult convertToImage(const QByteArray &data,
                               const RawImageLayout &layout) const override
    {
        try {
            auto *pixels = reinterpret_cast<uchar *>(const_cast<char *>(data.constData()));
            const qint64 yPlaneBytes = qint64(layout.stride) * qint64(layout.scanline);

            cv::Mat yPlane(layout.height, layout.width, CV_8UC1, pixels,
                           static_cast<size_t>(layout.stride));
            cv::Mat uvPlane(layout.height / 2, layout.width / 2, CV_8UC2,
                            pixels + yPlaneBytes, static_cast<size_t>(layout.stride));
            cv::Mat rgba;
            cv::cvtColorTwoPlane(yPlane, uvPlane, rgba, conversionCode());

            if (rgba.empty() || rgba.cols != layout.width || rgba.rows != layout.height) {
                return std::unexpected(tr(
                    "OpenCV returned an empty image or unexpected dimensions."));
            }

            const QImage wrappedImage(rgba.data, rgba.cols, rgba.rows,
                                      static_cast<qsizetype>(rgba.step),
                                      QImage::Format_RGBA8888);
            QImage image = wrappedImage.copy();
            if (image.isNull())
                return std::unexpected(tr("Could not allocate the converted QImage."));
            return image;
        } catch (const cv::Exception &exception) {
            return std::unexpected(tr("OpenCV conversion failed: %1")
                                       .arg(QString::fromLocal8Bit(exception.what())));
        } catch (const std::exception &exception) {
            return std::unexpected(tr("%1 conversion failed: %2")
                                       .arg(displayName(),
                                            QString::fromLocal8Bit(exception.what())));
        } catch (...) {
            return std::unexpected(tr("%1 conversion failed with an unknown exception.")
                                       .arg(displayName()));
        }
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

} // namespace

namespace RawImageDecoders {

const QList<const RawImageDecoder *> &all()
{
    // Never deleted; the registry lives as long as the plugin library.
    static const QList<const RawImageDecoder *> decoders = {
        new Nv12Decoder,
        new Nv21Decoder,
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
