// Copyright (C) 2026
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#ifndef RAWIMAGEFILENAME_H
#define RAWIMAGEFILENAME_H

#include <QList>
#include <QPair>
#include <QString>

#include <expected>
#include <optional>

// Raw image files carry no header, so everything the viewer knows about
// a frame up front comes from the file name. Two conventions are
// recognized:
//
//   p[MfsrBlend0]_..._[out]_port[9]_w[1920]_h[1440]_stride[1920]_scanline[1440]
//   anything_1920x1080.yuv
//
// The first is a debug dump naming scheme where every field is a
// key[value] pair; the second is the common "WxH somewhere in the name"
// fallback. WxH names the frame size only: it does not pin the stride to
// whatever decoder happened to be selected when the file was opened.
namespace RawImageFileName {

// Ordered key/value pairs as they appear in the name. Keys are
// normalized: "p" becomes "pipeline", "port" preceded by "[out]_"
// becomes "output", "w"/"h" become "width"/"height". Empty when the
// name does not follow the key[value] convention.
using Metadata = QList<QPair<QString, QString>>;
Metadata metadata(const QString &fileName);

// Translated label for a metadata key; returns the key unchanged when
// it has no dedicated label.
QString displayName(const QString &key);

// True for keys the viewer reports from the resolved layout instead of
// from the raw name (width, height, stride, scanline).
bool isLayoutKey(const QString &key);

// What the file name itself declared. stride and scanline are set only
// when the name wrote `_stride[N]` / `_scanline[N]`; a `4096x3072.raw`
// leaves them empty so the current format can supply its own tight row.
struct NamedLayout
{
    int width = 0;
    int height = 0;
    std::optional<int> stride;
    std::optional<int> scanline;
};

// A missing value (nullopt) means the name carries no dimensions, which
// is not an error: the user supplies them in the toolbar. An error means
// the name looks like it carries dimensions but they cannot be used.
using LayoutResult = std::expected<std::optional<NamedLayout>, QString>;
LayoutResult layout(const QString &fileName);

} // namespace RawImageFileName

#endif // RAWIMAGEFILENAME_H
