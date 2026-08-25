// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#ifndef VIEWERFACTORY_H
#define VIEWERFACTORY_H

#include <QFile>
#include <QMainWindow>
#include <QMap>
#include <QMimeType>
#include <QString>
#include <QWidget>

class AbstractViewer;

class ViewerFactory
{
public:
    enum class DefaultPolicy {
        NeverDefault,
        DefaultToTxtViewer,
        DefaultToCustomViewer
    };

    explicit ViewerFactory(QWidget *displayWidget, QMainWindow *mainWindow,
                           DefaultPolicy policy = DefaultPolicy::DefaultToTxtViewer);
    ~ViewerFactory();

    DefaultPolicy defaultPolicy() const { return m_defaultPolicy; }
    void setDefaultPolicy(DefaultPolicy policy) { m_defaultPolicy = policy; }
    bool defaultWarning() const { return m_defaultWarning; }
    void setDefaultWarning(bool on) { m_defaultWarning = on; }

    AbstractViewer *viewer(QFile *file) const;
    AbstractViewer *viewer(const QByteArray &data, const QString &mimeType) const;

    // Bypasses the automatic detection and hands the file to the named viewer.
    // Returns nullptr when the plugin is not loaded or does not claim support
    // for the file, in which case ownership of file stays with the caller.
    AbstractViewer *namedViewer(QFile *file, const QString &viewerName) const;

    using ViewerMap = QMap<QString, AbstractViewer *>;
    using ViewerList = QList<AbstractViewer *>;
    QStringList viewerNames(bool showDefault = false) const;
    ViewerList viewers() const;
    bool hasViewer(const QString &viewerName) const;
    AbstractViewer *findViewer(const QString &viewerName) const;
    AbstractViewer *defaultViewer() const;
    QStringList supportedMimeTypes() const;

private:
    DefaultPolicy m_defaultPolicy;
    QWidget *m_displayWidget;
    QMainWindow *m_mainWindow;
    ViewerMap m_viewers;
    AbstractViewer *m_defaultViewer = nullptr;
    bool m_defaultWarning = true;

    void loadViewerPlugins();
    void addViewer(QObject *viewerObject);
    AbstractViewer *viewer(const QMimeType &type) const;
    AbstractViewer *viewer(const QString &extension) const;
    void unload();
};

#endif // VIEWERFACTORY_H
