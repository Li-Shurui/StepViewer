// Copyright (C) 2026
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#include "rawimagefilename.h"

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

RawImageFileName::LayoutResult RawImageFileName::layout(const QString &fileName,
                                                        const RawImageDecoder &decoder)
{
    const QString baseName = QFileInfo(fileName).completeBaseName();
    static const QRegularExpression taggedPattern(
        QStringLiteral(R"((?:^|_)w\[(\d+)\]_h\[(\d+)\](?:_stride\[(\d+)\])?(?:_scanline\[(\d+)\])?)"),
        QRegularExpression::CaseInsensitiveOption);

    const QRegularExpressionMatch taggedMatch = taggedPattern.match(baseName);
    if (taggedMatch.hasMatch()) {
        const auto width = capturedInteger(taggedMatch, 1, Tr::tr("width"));
        if (!width)
            return std::unexpected(width.error());
        const auto height = capturedInteger(taggedMatch, 2, Tr::tr("height"));
        if (!height)
            return std::unexpected(height.error());

        // Untagged stride/scanline mean a tightly packed frame.
        const auto stride = taggedMatch.captured(3).isEmpty()
            ? std::expected<int, QString>(decoder.defaultStride(*width))
            : capturedInteger(taggedMatch, 3, Tr::tr("stride"));
        if (!stride)
            return std::unexpected(stride.error());
        const auto scanline = taggedMatch.captured(4).isEmpty()
            ? std::expected<int, QString>(*height)
            : capturedInteger(taggedMatch, 4, Tr::tr("scanline"));
        if (!scanline)
            return std::unexpected(scanline.error());

        const auto validated = decoder.validateLayout({*width, *height, *stride, *scanline});
        if (!validated)
            return std::unexpected(validated.error());
        return std::optional<RawImageLayout>(*validated);
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
        return std::optional<RawImageLayout>();

    const auto width = capturedInteger(lastMatch, 1, Tr::tr("width"));
    if (!width)
        return std::unexpected(width.error());
    const auto height = capturedInteger(lastMatch, 2, Tr::tr("height"));
    if (!height)
        return std::unexpected(height.error());

    const auto validated = decoder.validateLayout(
        {*width, *height, decoder.defaultStride(*width), *height});
    if (!validated)
        return std::unexpected(validated.error());
    return std::optional<RawImageLayout>(*validated);
}
