// Copyright (C) 2026
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#include "rawimagefilename.h"

#include "rawimagedecoder.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QRegularExpression>

using namespace Qt::StringLiterals;

namespace {

struct Tr
{
    Q_DECLARE_TR_FUNCTIONS(RawImageFileName)
};

// Normalized key names. Kept as constants because both the parser and
// the display-name lookup have to agree on them.
constexpr QLatin1StringView pipelineKey = "pipeline"_L1;
constexpr QLatin1StringView outputKey = "output"_L1;
constexpr QLatin1StringView widthKey = "width"_L1;
constexpr QLatin1StringView heightKey = "height"_L1;
constexpr QLatin1StringView strideKey = "stride"_L1;
constexpr QLatin1StringView scanlineKey = "scanline"_L1;

std::expected<int, QString> capturedInteger(const QRegularExpressionMatch &match, int index,
                                            const QString &fieldName)
{
    bool ok = false;
    const int value = match.captured(index).toInt(&ok);
    if (!ok) {
        return std::unexpected(Tr::tr("Invalid %1 value \"%2\" in the file name.")
                                   .arg(fieldName, match.captured(index)));
    }
    return value;
}

bool dimensionInRange(int value)
{
    return value >= RawImageDecoder::minimumDimension
        && value <= RawImageDecoder::maximumDimension;
}

} // namespace

RawImageFileName::Metadata RawImageFileName::metadata(const QString &fileName)
{
    const QString baseName = QFileInfo(fileName).completeBaseName();
    static const QRegularExpression pipelinePattern(
        QStringLiteral(R"((?:^|_)p\[([^\]]+)\])"),
        QRegularExpression::CaseInsensitiveOption);
    if (!pipelinePattern.match(baseName).hasMatch())
        return {};

    static const QRegularExpression tagPattern(
        QStringLiteral(R"((?:^|_)([A-Za-z][A-Za-z0-9]*)\[([^\]]*)\])"));

    Metadata metadata;
    auto matches = tagPattern.globalMatch(baseName);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        QString key = match.captured(1);
        const QString value = match.captured(2);

        if (key.compare("p"_L1, Qt::CaseInsensitive) == 0) {
            key = pipelineKey;
        } else if (key.compare("port"_L1, Qt::CaseInsensitive) == 0
                   && baseName.left(match.capturedStart(1))
                          .endsWith("[out]_"_L1, Qt::CaseInsensitive)) {
            key = outputKey;
        } else if (key.compare("w"_L1, Qt::CaseInsensitive) == 0) {
            key = widthKey;
        } else if (key.compare("h"_L1, Qt::CaseInsensitive) == 0) {
            key = heightKey;
        }

        metadata.append(qMakePair(key, value));
    }
    return metadata;
}

QString RawImageFileName::displayName(const QString &key)
{
    if (key == pipelineKey)
        return Tr::tr("Pipeline");
    if (key == outputKey)
        return Tr::tr("Output");
    return key;
}

bool RawImageFileName::isLayoutKey(const QString &key)
{
    return key.compare(widthKey, Qt::CaseInsensitive) == 0
        || key.compare(heightKey, Qt::CaseInsensitive) == 0
        || key.compare(strideKey, Qt::CaseInsensitive) == 0
        || key.compare(scanlineKey, Qt::CaseInsensitive) == 0;
}

RawImageFileName::LayoutResult RawImageFileName::layout(const QString &fileName)
{
    const QString baseName = QFileInfo(fileName).completeBaseName();
    static const QRegularExpression taggedPattern(
        QStringLiteral(R"((?:^|_)w\[(\d+)\]_h\[(\d+)\](?:_stride\[(\d+)\])?(?:_scanline\[(\d+)\])?)"),
        QRegularExpression::CaseInsensitiveOption);

    const auto finish = [](int width, int height, std::optional<int> stride,
                           std::optional<int> scanline) -> LayoutResult {
        if (!dimensionInRange(width)) {
            return std::unexpected(Tr::tr("Invalid %1 value \"%2\" in the file name.")
                                       .arg(Tr::tr("width"), QString::number(width)));
        }
        if (!dimensionInRange(height)) {
            return std::unexpected(Tr::tr("Invalid %1 value \"%2\" in the file name.")
                                       .arg(Tr::tr("height"), QString::number(height)));
        }
        if (stride && (*stride <= 0 || *stride > RawImageDecoder::maximumStride)) {
            return std::unexpected(Tr::tr("Invalid %1 value \"%2\" in the file name.")
                                       .arg(Tr::tr("stride"), QString::number(*stride)));
        }
        if (scanline && (*scanline <= 0 || *scanline > RawImageDecoder::maximumDimension)) {
            return std::unexpected(Tr::tr("Invalid %1 value \"%2\" in the file name.")
                                       .arg(Tr::tr("scanline"), QString::number(*scanline)));
        }
        return std::optional<NamedLayout>(NamedLayout{width, height, stride, scanline});
    };

    const QRegularExpressionMatch taggedMatch = taggedPattern.match(baseName);
    if (taggedMatch.hasMatch()) {
        const auto width = capturedInteger(taggedMatch, 1, Tr::tr("width"));
        if (!width)
            return std::unexpected(width.error());
        const auto height = capturedInteger(taggedMatch, 2, Tr::tr("height"));
        if (!height)
            return std::unexpected(height.error());

        // Absent tags are not defaults of the decoder used at open: that
        // would pin a .raw file's stride to NV12's one-byte rows.
        std::optional<int> stride;
        if (!taggedMatch.captured(3).isEmpty()) {
            const auto parsed = capturedInteger(taggedMatch, 3, Tr::tr("stride"));
            if (!parsed)
                return std::unexpected(parsed.error());
            stride = *parsed;
        }
        std::optional<int> scanline;
        if (!taggedMatch.captured(4).isEmpty()) {
            const auto parsed = capturedInteger(taggedMatch, 4, Tr::tr("scanline"));
            if (!parsed)
                return std::unexpected(parsed.error());
            scanline = *parsed;
        }
        return finish(*width, *height, stride, scanline);
    }

    if (baseName.contains("_w["_L1, Qt::CaseInsensitive)
        || baseName.contains("_h["_L1, Qt::CaseInsensitive)) {
        return std::unexpected(Tr::tr(
            "The file name contains incomplete image metadata. Expected "
            "\"_w[width]_h[height]_stride[stride]_scanline[scanline]\"."));
    }

    // Fall back to the last WxH group; trailing groups are more likely to
    // be the frame size than a leading date or version number.
    static const QRegularExpression dimensionsPattern(QStringLiteral(R"((\d+)[xX](\d+))"));
    auto matches = dimensionsPattern.globalMatch(baseName);
    QRegularExpressionMatch lastMatch;
    while (matches.hasNext())
        lastMatch = matches.next();

    if (!lastMatch.hasMatch())
        return std::optional<NamedLayout>();

    const auto width = capturedInteger(lastMatch, 1, Tr::tr("width"));
    if (!width)
        return std::unexpected(width.error());
    const auto height = capturedInteger(lastMatch, 2, Tr::tr("height"));
    if (!height)
        return std::unexpected(height.error());

    return finish(*width, *height, std::nullopt, std::nullopt);
}
