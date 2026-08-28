// Copyright (C) 2026
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#ifndef YUVCONTROLS_H
#define YUVCONTROLS_H

#include "rawimagedecoder.h"
#include "rawimagedisplay.h"
#include "rawimagefilename.h"

#include <QObject>
#include <QString>

#include <optional>

class QComboBox;
class QLabel;
class QSpinBox;
class QToolBar;

// The toolbar controls that describe how to interpret a raw file: frame
// size, row/plane padding, pixel format, sample packing, display transform,
// component plane and frame number.
//
// The instance is parented to the toolbar it populates, so it dies
// together with the widgets it refers to. Hold it in a QPointer and the
// viewer never sees a dangling control.
class YuvControls : public QObject
{
    Q_OBJECT

public:
    // Creates the controls and appends them to toolBar in reading order.
    explicit YuvControls(QToolBar *toolBar);

    int imageWidth() const;
    int imageHeight() const;
    int stride() const;    // first-plane row size in bytes
    int scanline() const;  // first-plane row count
    // Does not emit sizeChanged(); the caller already knows the size.
    void setImageSize(int width, int height);
    void setStride(int stride);
    void setScanline(int scanline);
    // Applies a session-saved padding only when the boxes are editable.
    // Tight files stay on the format's own row; a named `_stride[N]` has
    // already been filled by applyPadding().
    void restorePadding(int stride, int scanline);

    // Padding the file name declared. Applied while the toolbar width and
    // height still match that name; a WxH-only name leaves these empty so
    // each format can fill its own tight row.
    void setNamedLayout(const std::optional<RawImageFileName::NamedLayout> &layout);

    const RawImageDecoder *decoder() const;
    void setDecoder(const RawImageDecoder *decoder);

    // How to read the 16-bit containers of the current format. Like the
    // frame size this describes the file rather than the format, so it
    // survives a format change and is only seeded when a file is opened.
    RawSampleFormat sampleFormat() const;
    void setSampleFormat(RawSampleFormat format);

    // How the decoded frame is prepared for the screen. Affects nothing
    // that is read back out of the sample buffer.
    RawImageDisplayOptions displayOptions() const;
    void setDisplayOptions(const RawImageDisplayOptions &options);

    // RawImageFrame::compositePlane for the converted view, otherwise a
    // 0-based index into the current decoder's planeNames().
    int plane() const;
    // Ignores out-of-range values and reports what was actually selected.
    int setPlane(int plane);
    // Empty for the composite view; used to name exported files.
    QString selectedPlaneName() const;
    // Repopulates the plane list for the current decoder and selects the
    // composite view. Call after setDecoder().
    void rebuildPlanes();

    int frame() const;  // 1-based, as shown in the spin box
    void setFrame(int frame);
    void stepFrame(int delta);
    // Sets the navigable range and enables frame navigation for count > 1.
    void setFrameCount(qint64 count);

    // File size used to highlight the formats whose frame size divides it.
    void setFileSize(qint64 fileSize);

    void retranslate();

    // Commits in-progress spin box edits so value() matches what is on
    // screen. Call before reading width, height, stride, scanline or frame
    // for a reload.
    void commitPendingEdits();

signals:
    // A format, packing, display preset or plane the user picked, as opposed
    // to one restored or derived from the file name.
    void formatSelected();
    void sampleFormatSelected();
    void displayOptionsSelected();
    void planeSelected();
    void frameSelected();

private:
    void highlightMatchingFormats();
    void fillSampleFormats();
    void fillDisplayModes();
    void applyPadding();
    void setPaddingReadOnly(bool readOnly);
    bool tightPackedFits() const;
    bool namedPaddingApplies() const;
    // Only the formats with 16-bit containers read the packing.
    void updateSampleFormatEnabled();

    QLabel *m_widthLabel = nullptr;
    QLabel *m_heightLabel = nullptr;
    QLabel *m_strideLabel = nullptr;
    QLabel *m_scanlineLabel = nullptr;
    QLabel *m_formatLabel = nullptr;
    QLabel *m_sampleLabel = nullptr;
    QLabel *m_displayLabel = nullptr;
    QLabel *m_frameLabel = nullptr;
    QLabel *m_frameCountLabel = nullptr;
    QLabel *m_planeLabel = nullptr;
    QComboBox *m_formatComboBox = nullptr;
    QComboBox *m_sampleComboBox = nullptr;
    QComboBox *m_displayComboBox = nullptr;
    QComboBox *m_planeComboBox = nullptr;
    QSpinBox *m_widthSpinBox = nullptr;
    QSpinBox *m_heightSpinBox = nullptr;
    QSpinBox *m_strideSpinBox = nullptr;
    QSpinBox *m_scanlineSpinBox = nullptr;
    QSpinBox *m_frameSpinBox = nullptr;
    qint64 m_fileSize = 0;
    std::optional<RawImageFileName::NamedLayout> m_namedLayout;
};

#endif // YUVCONTROLS_H
