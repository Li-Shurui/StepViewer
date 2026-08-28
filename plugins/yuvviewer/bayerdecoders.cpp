// Copyright (C) 2026
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

// Decoders for Bayer mosaic formats: a single plane in which every pixel
// carries just one of R, G or B, following a 2x2 color filter array that
// repeats across the frame. The family base holds the layout rules, the
// demosaicing and the per-phase plane access; one thin subclass per CFA
// phase only says where each color sits.
//
// These are sensor dumps rather than display formats, and the conversion
// here is demosaicing only: no black level, white balance, color matrix
// or gamma is applied, so a frame looks flatter and greener than what the
// camera's own ISP would have produced from the same data.

#include "rawimagedecoder_p.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>

using namespace Qt::StringLiterals;
using namespace RawImageDecoderHelpers;

namespace {

// The four colors of a Bayer cell, in planeNames() order. The greens are
// told apart by the row they share: Gr sits on a red row, Gb on a blue
// one. They are not interchangeable, since a sensor's two greens usually
// differ slightly and are corrected separately.
enum CfaColor { CfaR, CfaGr, CfaGb, CfaB };

// Shared implementation for 16-bit Bayer formats: one plane of
// little-endian 16-bit samples, two bytes per pixel, with the CFA phase
// as the only difference between them.
class Bayer16Decoder : public RawImageDecoder
{
    Q_DECLARE_TR_FUNCTIONS(Bayer16Decoder)

public:
    LayoutResult validateLayout(const RawImageLayout &layout) const override
    {
        return validateBayerLayout(*this, layout, 2);
    }

    qint64 expectedByteSize(const RawImageLayout &layout) const override
    {
        // stride is in bytes and already covers the 16-bit samples.
        return qint64(layout.stride) * qint64(layout.scanline);
    }

    int defaultStride(int width) const override { return width * 2; }

    ImageResult convertToImage(const QByteArray &data,
                               const RawImageLayout &layout) const override
    {
        return runConversion(*this, [&]() -> ImageResult {
            auto *pixels = reinterpret_cast<uchar *>(const_cast<char *>(data.constData()));
            cv::Mat mosaic(layout.height, layout.width, CV_16UC1, pixels,
                           static_cast<size_t>(layout.stride));

            // Demosaicing keeps the 16-bit depth, so the whole sample
            // range reaches the display instead of being truncated to the
            // top eight bits the way the 16-bit YUV formats are.
            cv::Mat rgb;
            cv::cvtColor(mosaic, rgb, conversionCode());
            cv::Mat rgba;
            cv::cvtColor(rgb, rgba, cv::COLOR_RGB2RGBA);
            return rgbaMatToImage(rgba, layout, QImage::Format_RGBA64);
        });
    }

    QStringList planeNames() const override
    {
        return {QStringLiteral("R"), QStringLiteral("Gr"),
                QStringLiteral("Gb"), QStringLiteral("B")};
    }

    // Each color owns one position of the 2x2 cell, so a plane is the
    // frame sampled every second column of every second row: a quarter of
    // the pixels, at half the width and half the height. This is the raw
    // mosaic, with no interpolated samples in it.
    ImageResult extractPlane(const QByteArray &data, const RawImageLayout &layout,
                             int plane) const override
    {
        const std::array<int, 4> pattern = cfaPattern();
        const auto position = std::find(pattern.cbegin(), pattern.cend(), plane);
        if (position == pattern.cend())
            return invalidPlane(plane);

        const auto index = position - pattern.cbegin();
        const int columnOffset = int(index % 2);
        const int rowOffset = int(index / 2);
        const auto *pixels = reinterpret_cast<const uchar *>(data.constData());
        return strided16Plane(pixels + qint64(rowOffset) * layout.stride,
                              layout.width / 2, layout.height / 2,
                              qint64(layout.stride) * 2, 2, columnOffset);
    }

    // A mosaic pixel carries one color and nothing else, so the probe
    // reports which color that is rather than an RGB triplet.
    QString describePixel(const QByteArray &data, const RawImageLayout &layout,
                          int x, int y) const override
    {
        const auto *pixels = reinterpret_cast<const uchar *>(data.constData());
        const int sample = readLe16(pixels + qint64(y) * layout.stride + 2 * x);
        const int color = cfaPattern()[(y % 2) * 2 + (x % 2)];
        return tr("%1=%2").arg(planeNames().at(color)).arg(sample);
    }

protected:
    // Colors of the four cell positions in row-major order:
    // (0,0), (1,0), (0,1), (1,1).
    virtual std::array<int, 4> cfaPattern() const = 0;

    // cv::COLOR_BayerBG2RGB and friends. OpenCV names a code after the
    // second row's second and third columns instead of after the top-left
    // cell, so the letters do not match the format name: RGGB is BayerBG,
    // GRBG is BayerGB, GBRG is BayerGR and BGGR is BayerRG.
    virtual int conversionCode() const = 0;
};

class BayerRggb16Decoder final : public Bayer16Decoder
{
public:
    QLatin1StringView id() const override { return "bayer_rggb16"_L1; }
    QString displayName() const override { return QStringLiteral("Bayer RGGB16"); }
    QString mimeType() const override { return "video/x-raw-bayer-rggb16"_L1; }
    QStringList fileExtensions() const override { return {"rggb16"_L1, "RGGB16"_L1}; }

protected:
    std::array<int, 4> cfaPattern() const override { return {CfaR, CfaGr, CfaGb, CfaB}; }
    int conversionCode() const override { return cv::COLOR_BayerBG2RGB; }
};

} // namespace

QList<const RawImageDecoder *> RawImageDecoders::createBayerDecoders()
{
    return {
        new BayerRggb16Decoder,
    };
}
