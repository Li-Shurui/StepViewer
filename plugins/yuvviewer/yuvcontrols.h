// Copyright (C) 2026
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#ifndef YUVCONTROLS_H
#define YUVCONTROLS_H

#include <QObject>
#include <QString>

class QComboBox;
class QLabel;
class QSpinBox;
class QToolBar;
class RawImageDecoder;

// The toolbar controls that describe how to interpret a raw file: frame
// size, pixel format, component plane and frame number.
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
    // Does not emit sizeChanged(); the caller already knows the size.
    void setImageSize(int width, int height);

    const RawImageDecoder *decoder() const;
    void setDecoder(const RawImageDecoder *decoder);

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

signals:
    // A format or plane the user picked, as opposed to one restored or
    // derived from the file name.
    void formatSelected();
    void planeSelected();
    void frameSelected();

private:
    void highlightMatchingFormats();

    QLabel *m_widthLabel = nullptr;
    QLabel *m_heightLabel = nullptr;
    QLabel *m_formatLabel = nullptr;
    QLabel *m_frameLabel = nullptr;
    QLabel *m_frameCountLabel = nullptr;
    QLabel *m_planeLabel = nullptr;
    QComboBox *m_formatComboBox = nullptr;
    QComboBox *m_planeComboBox = nullptr;
    QSpinBox *m_widthSpinBox = nullptr;
    QSpinBox *m_heightSpinBox = nullptr;
    QSpinBox *m_frameSpinBox = nullptr;
    qint64 m_fileSize = 0;
};

#endif // YUVCONTROLS_H
