// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QDir>
#include <QStringLiteral>

class AbstractViewer;
class RecentFiles;
class ViewerFactory;
class Translator;

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(Translator &translator, QWidget *parent = nullptr);
    ~MainWindow();
    bool hasPlugins() const;

public slots:
    bool openFile(const QString &fileName);
    bool openData(const QByteArray &data, const QString &mimeType);

protected:
    void changeEvent(QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void onActionOpenTriggered();
    void onActionAboutTriggered();
    void onActionAboutQtTriggered();
    void onActionSwitchLanguage(QLocale::Language lang);
    void onActionSwitchTheme(const QString &themeId);
    void onActionSwitchViewer(const QString &viewerName);

private:
    void applyTheme(const QString &themeId);
    void setupModeMenu();
    void updateModeMenu();
    void showViewerError(const QString &message);
    void detachViewer();
    bool attachViewer(AbstractViewer *viewer);
    // An empty viewerName lets the factory pick the viewer by file type.
    bool openFileWithViewer(const QString &fileName, const QString &viewerName);
    void readSettings();
    void saveSettings() const;
    void restoreViewerSettings();
    void resetViewer() const;
    void saveViewerSettings() const;

    QDir m_currentDir;
    QString m_currentFile;
    AbstractViewer *m_viewer = nullptr;
    std::unique_ptr<Ui::MainWindow> ui;
    std::unique_ptr<RecentFiles> m_recentFiles;
    std::unique_ptr<ViewerFactory> m_factory;
    std::array<QMetaObject::Connection, 3> m_viewerConnections;
    Translator &m_translator;
    QString m_themeId = QStringLiteral("dark");

    static constexpr QLatin1StringView settingsDir = QLatin1StringView("WorkingDir");
    static constexpr QLatin1StringView settingsMainWindow = QLatin1StringView("MainWindow");
    static constexpr QLatin1StringView settingsViewers = QLatin1StringView("Viewers");
    static constexpr QLatin1StringView settingsFiles = QLatin1StringView("RecentFiles");
    static constexpr QLatin1StringView settingsTheme = QLatin1StringView("Theme");
    static constexpr QLatin1StringView themeDark = QLatin1StringView("dark");
    static constexpr QLatin1StringView themeNone = QLatin1StringView("none");
};

#endif // MAINWINDOW_H
