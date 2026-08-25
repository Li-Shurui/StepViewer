// Copyright (C) 2026
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#include "yuvcontrols.h"

#include "rawimagedecoder.h"
#include "rawimageframe.h"

#include <QComboBox>
#include <QFont>
#include <QLabel>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolBar>

#include <climits>

namespace {
// Index 0 of the plane combo box is the composite view, so the planes of
// the decoder start at 1.
constexpr int compositeIndex = 0;
} // namespace

YuvControls::YuvControls(QToolBar *toolBar) : QObject(toolBar)
{
    Q_ASSERT(toolBar);

    const auto makeDimensionSpinBox = [toolBar](int initialValue) {
        auto *spinBox = new QSpinBox(toolBar);
        spinBox->setRange(RawImageDecoder::minimumDimension, RawImageDecoder::maximumDimension);
        // Most raw formats subsample chroma by two, so odd sizes are
        // rarely what the user wants to step through.
        spinBox->setSingleStep(2);
        spinBox->setValue(initialValue);
        spinBox->setKeyboardTracking(false);
        return spinBox;
    };

    m_widthLabel = new QLabel(toolBar);
    m_widthSpinBox = makeDimensionSpinBox(1920);
    m_heightLabel = new QLabel(toolBar);
    m_heightSpinBox = makeDimensionSpinBox(1080);

    m_formatLabel = new QLabel(toolBar);
    m_formatComboBox = new QComboBox(toolBar);
    for (const RawImageDecoder *decoder : RawImageDecoders::all()) {
        m_formatComboBox->addItem(decoder->displayName());
        m_formatComboBox->setMinimumContentsLength(12);
        m_formatComboBox->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    }

    m_planeLabel = new QLabel(toolBar);
    m_planeComboBox = new QComboBox(toolBar);

    m_frameLabel = new QLabel(toolBar);
    m_frameSpinBox = new QSpinBox(toolBar);
    m_frameSpinBox->setRange(1, 1);
    m_frameSpinBox->setKeyboardTracking(false);
    m_frameSpinBox->setEnabled(false);
    m_frameCountLabel = new QLabel(toolBar);

    // A changed frame size does not reload by itself; it only re-runs the
    // format hint, and the user confirms with Reload.
    const auto onDimensionChanged = [this] { highlightMatchingFormats(); };
    connect(m_widthSpinBox, &QSpinBox::valueChanged, this, onDimensionChanged);
    connect(m_heightSpinBox, &QSpinBox::valueChanged, this, onDimensionChanged);

    connect(m_formatComboBox, &QComboBox::activated, this, &YuvControls::formatSelected);
    connect(m_planeComboBox, &QComboBox::activated, this, &YuvControls::planeSelected);
    connect(m_frameSpinBox, &QSpinBox::valueChanged, this, &YuvControls::frameSelected);

    toolBar->addWidget(m_widthLabel);
    toolBar->addWidget(m_widthSpinBox);
    toolBar->addWidget(m_heightLabel);
    toolBar->addWidget(m_heightSpinBox);
    toolBar->addWidget(m_formatLabel);
    toolBar->addWidget(m_formatComboBox);
    toolBar->addWidget(m_planeLabel);
    toolBar->addWidget(m_planeComboBox);
    toolBar->addWidget(m_frameLabel);
    toolBar->addWidget(m_frameSpinBox);
    toolBar->addWidget(m_frameCountLabel);

    rebuildPlanes();
}

int YuvControls::imageWidth() const
{
    return m_widthSpinBox->value();
}

int YuvControls::imageHeight() const
{
    return m_heightSpinBox->value();
}

void YuvControls::setImageSize(int width, int height)
{
    const QSignalBlocker widthBlocker(m_widthSpinBox);
    const QSignalBlocker heightBlocker(m_heightSpinBox);
    m_widthSpinBox->setValue(width);
    m_heightSpinBox->setValue(height);
    highlightMatchingFormats();
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
    m_formatLabel->setText(tr("Format:"));
    m_frameLabel->setText(tr("Frame:"));
    m_planeLabel->setText(tr("Plane:"));
    m_widthSpinBox->setSuffix(tr(" px"));
    m_heightSpinBox->setSuffix(tr(" px"));
    if (m_planeComboBox->count() > compositeIndex)
        m_planeComboBox->setItemText(compositeIndex, tr("Composite"));
    highlightMatchingFormats();
}

void YuvControls::commitPendingEdits()
{
    m_widthSpinBox->interpretText();
    m_heightSpinBox->interpretText();
    m_frameSpinBox->interpretText();
}
