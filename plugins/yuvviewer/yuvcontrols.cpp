// Copyright (C) 2026
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#include "yuvcontrols.h"

#include "rawimagedecoder.h"
#include "rawimageframe.h"

#include <QAbstractSpinBox>
#include <QAction>
#include <QComboBox>
#include <QFont>
#include <QLabel>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolBar>
#include <QWidget>
#include <QtMath>

#include <climits>
#include <iterator>

namespace {
// Index 0 of the plane combo box is the composite view, so the planes of
// the decoder start at 1.
constexpr int compositeIndex = 0;

// The packings offered for 16-bit containers, in combo box order. At 16
// bits the two alignments are the same thing, so it appears once. Depths
// run downwards because the question a user has is "how deep is my data",
// and 16 is the answer that needs no thought.
struct SamplePacking
{
    int bits;
    bool msbAligned;
};
constexpr SamplePacking samplePackings[] = {
    {16, true},
    {14, true}, {14, false},
    {12, true}, {12, false},
    {10, true}, {10, false},
    {8, true},  {8, false},
};

// Display transforms, in combo box order. Presets rather than three
// independent toggles: the useful combinations are few, and the toolbar
// already carries plenty. Linear comes first and stays the default, so a
// frame is shown as it is until the user asks for otherwise.
struct DisplayPreset
{
    bool autoLevel;
    bool grayWorldBalance;
    qreal gamma;
};
constexpr DisplayPreset displayPresets[] = {
    {false, false, 1.0},
    {false, false, 2.2},
    {true,  false, 1.0},
    {true,  false, 2.2},
    {true,  true,  2.2},
};
} // namespace

YuvControls::YuvControls(QToolBar *layoutBar, QToolBar *viewBar, QAction *reloadAction)
    : QObject(viewBar)
    , m_layoutBar(layoutBar)
{
    Q_ASSERT(layoutBar);
    Q_ASSERT(viewBar);
    Q_ASSERT(reloadAction);

    m_layoutSeparator = layoutBar->addSeparator();

    const auto makeDimensionSpinBox = [layoutBar](int initialValue) {
        auto *spinBox = new QSpinBox(layoutBar);
        spinBox->setRange(RawImageDecoder::minimumDimension, RawImageDecoder::maximumDimension);
        // Most raw formats subsample chroma by two, so odd sizes are
        // rarely what the user wants to step through.
        spinBox->setSingleStep(2);
        spinBox->setValue(initialValue);
        spinBox->setKeyboardTracking(false);
        return spinBox;
    };

    m_widthLabel = new QLabel(layoutBar);
    m_widthSpinBox = makeDimensionSpinBox(1920);
    m_heightLabel = new QLabel(layoutBar);
    m_heightSpinBox = makeDimensionSpinBox(1080);

    m_strideLabel = new QLabel(layoutBar);
    m_strideSpinBox = new QSpinBox(layoutBar);
    m_strideSpinBox->setRange(1, RawImageDecoder::maximumStride);
    m_strideSpinBox->setKeyboardTracking(false);
    m_strideSpinBox->setValue(1920);

    m_scanlineLabel = new QLabel(layoutBar);
    m_scanlineSpinBox = new QSpinBox(layoutBar);
    m_scanlineSpinBox->setRange(RawImageDecoder::minimumDimension,
                                RawImageDecoder::maximumDimension);
    m_scanlineSpinBox->setKeyboardTracking(false);
    m_scanlineSpinBox->setValue(1080);

    m_formatLabel = new QLabel(layoutBar);
    m_formatComboBox = new QComboBox(layoutBar);
    for (const RawImageDecoder *decoder : RawImageDecoders::all()) {
        m_formatComboBox->addItem(decoder->displayName());
        m_formatComboBox->setMinimumContentsLength(12);
        m_formatComboBox->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    }

    m_sampleLabel = new QLabel(layoutBar);
    m_sampleComboBox = new QComboBox(layoutBar);
    fillSampleFormats();

    m_displayLabel = new QLabel(viewBar);
    m_displayComboBox = new QComboBox(viewBar);
    fillDisplayModes();

    m_planeLabel = new QLabel(viewBar);
    m_planeComboBox = new QComboBox(viewBar);

    m_frameLabel = new QLabel(viewBar);
    m_frameSpinBox = new QSpinBox(viewBar);
    m_frameSpinBox->setRange(1, 1);
    m_frameSpinBox->setKeyboardTracking(false);
    m_frameSpinBox->setEnabled(false);
    m_frameCountLabel = new QLabel(viewBar);

    // A changed frame size does not reload by itself; it only re-runs the
    // format hint and refreshes auto stride/scanline. The user confirms
    // with Reload.
    const auto onDimensionChanged = [this] {
        highlightMatchingFormats();
        applyPadding();
    };
    connect(m_widthSpinBox, &QSpinBox::valueChanged, this, onDimensionChanged);
    connect(m_heightSpinBox, &QSpinBox::valueChanged, this, onDimensionChanged);

    connect(m_formatComboBox, &QComboBox::currentIndexChanged, this, [this] {
        updateSampleFormatEnabled();
        applyPadding();
    });
    connect(m_formatComboBox, &QComboBox::activated, this, &YuvControls::formatSelected);
    connect(m_sampleComboBox, &QComboBox::activated, this, &YuvControls::sampleFormatSelected);
    connect(m_displayComboBox, &QComboBox::activated, this,
            &YuvControls::displayOptionsSelected);
    connect(m_planeComboBox, &QComboBox::activated, this, &YuvControls::planeSelected);
    connect(m_frameSpinBox, &QSpinBox::valueChanged, this, &YuvControls::frameSelected);

    layoutBar->addWidget(m_widthLabel);
    layoutBar->addWidget(m_widthSpinBox);
    layoutBar->addWidget(m_heightLabel);
    layoutBar->addWidget(m_heightSpinBox);
    layoutBar->addWidget(m_strideLabel);
    layoutBar->addWidget(m_strideSpinBox);
    layoutBar->addWidget(m_scanlineLabel);
    layoutBar->addWidget(m_scanlineSpinBox);
    layoutBar->addWidget(m_formatLabel);
    layoutBar->addWidget(m_formatComboBox);
    layoutBar->addWidget(m_sampleLabel);
    layoutBar->addWidget(m_sampleComboBox);
    m_reloadSeparator = layoutBar->addSeparator();
    layoutBar->addAction(reloadAction);
    m_hostedReload = reloadAction;

    viewBar->addWidget(m_displayLabel);
    viewBar->addWidget(m_displayComboBox);
    viewBar->addWidget(m_planeLabel);
    viewBar->addWidget(m_planeComboBox);
    viewBar->addWidget(m_frameLabel);
    viewBar->addWidget(m_frameSpinBox);
    viewBar->addWidget(m_frameCountLabel);

    rebuildPlanes();
    updateSampleFormatEnabled();
    applyPadding();
}

YuvControls::~YuvControls()
{
    // Width/format widgets live on the main window's Open bar, which
    // outlives this viewer. Leave them there and they pile up on every
    // file change; pull them off only when that is the bar we used.
    if (!m_layoutBar || m_layoutBar->objectName() != QLatin1String("mainToolBar"))
        return;

    if (m_layoutSeparator) {
        m_layoutBar->removeAction(m_layoutSeparator);
        delete m_layoutSeparator;
        m_layoutSeparator = nullptr;
    }

    const auto takeWidget = [this](QWidget *widget) {
        if (!widget)
            return;
        for (QAction *action : m_layoutBar->actions()) {
            if (m_layoutBar->widgetForAction(action) == widget) {
                m_layoutBar->removeAction(action);
                delete action;
                return;
            }
        }
        delete widget;
    };

    takeWidget(m_widthLabel);
    takeWidget(m_widthSpinBox);
    takeWidget(m_heightLabel);
    takeWidget(m_heightSpinBox);
    takeWidget(m_strideLabel);
    takeWidget(m_strideSpinBox);
    takeWidget(m_scanlineLabel);
    takeWidget(m_scanlineSpinBox);
    takeWidget(m_formatLabel);
    takeWidget(m_formatComboBox);
    takeWidget(m_sampleLabel);
    takeWidget(m_sampleComboBox);

    if (m_hostedReload) {
        m_layoutBar->removeAction(m_hostedReload);
        m_hostedReload = nullptr;
    }
    if (m_reloadSeparator) {
        m_layoutBar->removeAction(m_reloadSeparator);
        delete m_reloadSeparator;
        m_reloadSeparator = nullptr;
    }
}

int YuvControls::imageWidth() const
{
    return m_widthSpinBox->value();
}

int YuvControls::imageHeight() const
{
    return m_heightSpinBox->value();
}

int YuvControls::stride() const
{
    return m_strideSpinBox->value();
}

int YuvControls::scanline() const
{
    return m_scanlineSpinBox->value();
}

void YuvControls::setStride(int stride)
{
    const QSignalBlocker blocker(m_strideSpinBox);
    m_strideSpinBox->setValue(stride);
}

void YuvControls::setScanline(int scanline)
{
    const QSignalBlocker blocker(m_scanlineSpinBox);
    m_scanlineSpinBox->setValue(scanline);
}

void YuvControls::restorePadding(int stride, int scanline)
{
    if (m_strideSpinBox->isReadOnly())
        return;
    if (namedPaddingApplies())
        return;
    if (stride < 1 || stride > RawImageDecoder::maximumStride
        || scanline < RawImageDecoder::minimumDimension
        || scanline > RawImageDecoder::maximumDimension) {
        return;
    }
    setStride(stride);
    setScanline(scanline);
}

void YuvControls::setNamedLayout(const std::optional<RawImageFileName::NamedLayout> &layout)
{
    m_namedLayout = layout;
    applyPadding();
}

bool YuvControls::namedPaddingApplies() const
{
    return m_namedLayout
        && imageWidth() == m_namedLayout->width
        && imageHeight() == m_namedLayout->height
        && (m_namedLayout->stride.has_value() || m_namedLayout->scanline.has_value());
}

bool YuvControls::tightPackedFits() const
{
    const RawImageDecoder *current = decoder();
    if (!current || m_fileSize <= 0)
        return true;

    const int width = imageWidth();
    const int height = imageHeight();
    const RawImageLayout tight{width, height, current->defaultStride(width), height};
    if (!current->validateLayout(tight))
        return false;
    const qint64 frameSize = current->expectedByteSize(tight);
    return frameSize > 0 && m_fileSize >= frameSize && (m_fileSize % frameSize) == 0;
}

void YuvControls::setPaddingReadOnly(bool readOnly)
{
    for (QSpinBox *box : {m_strideSpinBox, m_scanlineSpinBox}) {
        box->setReadOnly(readOnly);
        box->setButtonSymbols(readOnly ? QAbstractSpinBox::NoButtons
                                       : QAbstractSpinBox::UpDownArrows);
    }
    const QString autoTip = tr(
        "Filled from the current format: stride is the tightly packed row in bytes,\n"
        "scanline is the height. Edit them when the file has row or plane padding.");
    const QString manualTip = tr(
        "This file is larger than a tightly packed frame, or the name declares padding.\n"
        "Stride is the first plane's row size in bytes; scanline is its row count.");
    m_strideSpinBox->setToolTip(readOnly ? autoTip : manualTip);
    m_scanlineSpinBox->setToolTip(readOnly ? autoTip : manualTip);
}

void YuvControls::applyPadding()
{
    const int width = imageWidth();
    const int height = imageHeight();
    int stride = decoder() ? decoder()->defaultStride(width) : width;
    int scanline = height;
    if (m_namedLayout && width == m_namedLayout->width && height == m_namedLayout->height) {
        if (m_namedLayout->stride)
            stride = *m_namedLayout->stride;
        if (m_namedLayout->scanline)
            scanline = *m_namedLayout->scanline;
    }
    setStride(stride);
    setScanline(scanline);
    setPaddingReadOnly(!namedPaddingApplies() && tightPackedFits());
}

void YuvControls::setImageSize(int width, int height)
{
    const QSignalBlocker widthBlocker(m_widthSpinBox);
    const QSignalBlocker heightBlocker(m_heightSpinBox);
    m_widthSpinBox->setValue(width);
    m_heightSpinBox->setValue(height);
    highlightMatchingFormats();
    applyPadding();
}

const RawImageDecoder *YuvControls::decoder() const
{
    const QList<const RawImageDecoder *> &decoders = RawImageDecoders::all();
    const int index = m_formatComboBox->currentIndex();
    return index >= 0 && index < decoders.size() ? decoders.at(index) : nullptr;
}

void YuvControls::setDecoder(const RawImageDecoder *decoder)
{
    const int index = RawImageDecoders::all().indexOf(decoder);
    if (index < 0)
        return;
    const QSignalBlocker blocker(m_formatComboBox);
    m_formatComboBox->setCurrentIndex(index);
    updateSampleFormatEnabled();
    applyPadding();
}

// Depths above the container are meaningless, and at 16 bits the two
// alignments coincide, so the list stays short enough to scan.
void YuvControls::fillSampleFormats()
{
    const QSignalBlocker blocker(m_sampleComboBox);
    const int previous = qMax(0, m_sampleComboBox->currentIndex());
    m_sampleComboBox->clear();
    for (const SamplePacking &packing : samplePackings) {
        if (packing.bits == 16) {
            m_sampleComboBox->addItem(tr("%1 bit").arg(packing.bits));
            continue;
        }
        m_sampleComboBox->addItem(packing.msbAligned
                                      ? tr("%1 bit MSB").arg(packing.bits)
                                      : tr("%1 bit LSB").arg(packing.bits));
    }
    m_sampleComboBox->setCurrentIndex(previous);
    m_sampleComboBox->setToolTip(
        tr("Where the significant bits sit inside each 16-bit sample. MSB means the\n"
           "value is left-aligned and the low bits are padding (P010 and most ISP\n"
           "output); LSB means it is right-aligned and the high bits are zero (most\n"
           "sensor dumps). Reading right-aligned data as 16 bit yields a black frame."));
}

void YuvControls::updateSampleFormatEnabled()
{
    const RawImageDecoder *current = decoder();
    const bool applies = current && current->defaultSampleFormat().has_value();
    m_sampleLabel->setEnabled(applies);
    m_sampleComboBox->setEnabled(applies);
}

RawSampleFormat YuvControls::sampleFormat() const
{
    const int index = m_sampleComboBox->currentIndex();
    if (index < 0 || index >= int(std::size(samplePackings)))
        return {};
    return RawSampleFormat{samplePackings[index].bits, samplePackings[index].msbAligned};
}

void YuvControls::setSampleFormat(RawSampleFormat format)
{
    for (int i = 0; i < int(std::size(samplePackings)); ++i) {
        // At 16 bits the alignment carries no information, so match on the
        // depth alone and land on the single combined entry.
        const bool matches = samplePackings[i].bits == format.bits
            && (format.bits == 16 || samplePackings[i].msbAligned == format.msbAligned);
        if (!matches)
            continue;
        const QSignalBlocker blocker(m_sampleComboBox);
        m_sampleComboBox->setCurrentIndex(i);
        return;
    }
}

RawImageDisplayOptions YuvControls::displayOptions() const
{
    const int index = m_displayComboBox->currentIndex();
    if (index < 0 || index >= int(std::size(displayPresets)))
        return {};
    const DisplayPreset &preset = displayPresets[index];
    return {preset.autoLevel, preset.grayWorldBalance, preset.gamma};
}

void YuvControls::setDisplayOptions(const RawImageDisplayOptions &options)
{
    for (int i = 0; i < int(std::size(displayPresets)); ++i) {
        const DisplayPreset &preset = displayPresets[i];
        if (preset.autoLevel == options.autoLevel
            && preset.grayWorldBalance == options.grayWorldBalance
            && qAbs(preset.gamma - options.gamma) < 0.05) {
            const QSignalBlocker blocker(m_displayComboBox);
            m_displayComboBox->setCurrentIndex(i);
            return;
        }
    }
}

void YuvControls::fillDisplayModes()
{
    const QSignalBlocker blocker(m_displayComboBox);
    const int previous = qMax(0, m_displayComboBox->currentIndex());
    m_displayComboBox->clear();
    // Order must match displayPresets[].
    m_displayComboBox->addItem(tr("Linear"));
    m_displayComboBox->addItem(tr("Gamma 2.2"));
    m_displayComboBox->addItem(tr("Auto level"));
    m_displayComboBox->addItem(tr("Auto + Gamma 2.2"));
    m_displayComboBox->addItem(tr("Auto + WB + Gamma 2.2"));
    m_displayComboBox->setCurrentIndex(previous);
    m_displayComboBox->setToolTip(
        tr("Display-only. Does not change the samples the probe and histogram read.\n"
           "Linear shows the decoded values as they are. Auto stretches the range.\n"
           "WB equalizes the channel means (gray-world)."));
}

int YuvControls::plane() const
{
    return m_planeComboBox->currentIndex() - 1;
}

int YuvControls::setPlane(int plane)
{
    const int index = plane + 1;
    if (index <= compositeIndex || index >= m_planeComboBox->count())
        return RawImageFrame::compositePlane;

    const QSignalBlocker blocker(m_planeComboBox);
    m_planeComboBox->setCurrentIndex(index);
    return plane;
}

QString YuvControls::selectedPlaneName() const
{
    if (plane() == RawImageFrame::compositePlane)
        return {};
    return m_planeComboBox->currentText();
}

void YuvControls::rebuildPlanes()
{
    updateSampleFormatEnabled();
    const QSignalBlocker blocker(m_planeComboBox);
    m_planeComboBox->clear();
    m_planeComboBox->addItem(tr("Composite"));
    if (const RawImageDecoder *current = decoder())
        m_planeComboBox->addItems(current->planeNames());
    // Composite plus a single plane (e.g. Y8) offers nothing to switch.
    m_planeComboBox->setEnabled(m_planeComboBox->count() > 2);
}

int YuvControls::frame() const
{
    return m_frameSpinBox->value();
}

void YuvControls::setFrame(int frame)
{
    const QSignalBlocker blocker(m_frameSpinBox);
    m_frameSpinBox->setValue(qMax(1, frame));
}

void YuvControls::stepFrame(int delta)
{
    // Goes through setValue() so that the change is announced.
    m_frameSpinBox->setValue(m_frameSpinBox->value() + delta);
}

void YuvControls::setFrameCount(qint64 count)
{
    const qint64 frames = qMax<qint64>(count, 1);
    const QSignalBlocker blocker(m_frameSpinBox);
    m_frameSpinBox->setMaximum(frames > INT_MAX ? INT_MAX : int(frames));
    if (m_frameSpinBox->value() > m_frameSpinBox->maximum())
        m_frameSpinBox->setValue(m_frameSpinBox->maximum());

    m_frameSpinBox->setEnabled(frames > 1);
    m_frameCountLabel->setText(QStringLiteral("/ %1").arg(frames));
}

void YuvControls::setFileSize(qint64 fileSize)
{
    if (m_fileSize == fileSize)
        return;
    m_fileSize = fileSize;
    highlightMatchingFormats();
    applyPadding();
}

// Highlights the formats whose tight frame size divides the file size
// evenly. This inverts the usual workflow: with unknown data, set the
// expected width/height and pick one of the matching (bold) formats.
void YuvControls::highlightMatchingFormats()
{
    const int width = imageWidth();
    const int height = imageHeight();

    const QFont normalFont = m_formatComboBox->font();
    QFont boldFont = normalFont;
    boldFont.setBold(true);

    const QList<const RawImageDecoder *> &decoders = RawImageDecoders::all();
    for (int i = 0; i < decoders.size(); ++i) {
        const RawImageDecoder *candidate = decoders.at(i);
        // The heuristic assumes a tightly packed layout; padded files
        // (stride/scanline tags) intentionally do not match.
        const RawImageLayout tight{width, height, candidate->defaultStride(width), height};

        qint64 frames = 0;
        qint64 frameSize = 0;
        if (candidate->validateLayout(tight)) {
            frameSize = candidate->expectedByteSize(tight);
            if (frameSize > 0 && m_fileSize >= frameSize && (m_fileSize % frameSize) == 0)
                frames = m_fileSize / frameSize;
        }

        m_formatComboBox->setItemData(i, frames > 0 ? boldFont : normalFont, Qt::FontRole);
        m_formatComboBox->setItemData(
            i,
            frames > 0 ? tr("Matches the file size: %1 bytes per frame, %2 frames.")
                             .arg(frameSize)
                             .arg(frames)
                       : QString(),
            Qt::ToolTipRole);
    }
}

void YuvControls::retranslate()
{
    m_widthLabel->setText(tr("Width:"));
    m_heightLabel->setText(tr("Height:"));
    m_strideLabel->setText(tr("Stride:"));
    m_scanlineLabel->setText(tr("Scanline:"));
    m_formatLabel->setText(tr("Format:"));
    m_sampleLabel->setText(tr("Samples:"));
    m_displayLabel->setText(tr("View:"));
    m_frameLabel->setText(tr("Frame:"));
    m_planeLabel->setText(tr("Plane:"));
    m_widthSpinBox->setSuffix(tr(" px"));
    m_heightSpinBox->setSuffix(tr(" px"));
    m_strideSpinBox->setSuffix(tr(" bytes"));
    m_scanlineSpinBox->setSuffix(tr(" lines"));
    fillSampleFormats();
    fillDisplayModes();
    setPaddingReadOnly(m_strideSpinBox->isReadOnly());
    if (m_planeComboBox->count() > compositeIndex)
        m_planeComboBox->setItemText(compositeIndex, tr("Composite"));
    highlightMatchingFormats();
}

void YuvControls::commitPendingEdits()
{
    m_widthSpinBox->interpretText();
    m_heightSpinBox->interpretText();
    m_strideSpinBox->interpretText();
    m_scanlineSpinBox->interpretText();
    m_frameSpinBox->interpretText();
}
