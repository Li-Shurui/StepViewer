// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "viewerfactory.h"
#include "abstractviewer.h"
#include "recentfiles.h"
#include "recentfilemenu.h"
#include "translator.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QLabel>
#include <QToolButton>
#include <QMessageBox>
#include <QMimeData>

#include <QDir>
#include <QSettings>

#include <memory>

using namespace Qt::StringLiterals;

MainWindow::MainWindow(Translator &translator, QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow), m_translator(translator)
{
    ui->setupUi(this);
    connect(ui->actionOpen, &QAction::triggered, this, &MainWindow::onActionOpenTriggered);
    connect(ui->actionAbout, &QAction::triggered, this, &MainWindow::onActionAboutTriggered);
    connect(ui->actionAboutQt, &QAction::triggered, this, &MainWindow::onActionAboutQtTriggered);
    connect(ui->actionChinese, &QAction::triggered, this,
            [this] { onActionSwitchLanguage(QLocale::Chinese); });
    connect(ui->actionEnglish, &QAction::triggered, this,
            [this] { onActionSwitchLanguage(QLocale::English); });

    auto *themeGroup = new QActionGroup(this);
    themeGroup->addAction(ui->actionThemeDark);
    themeGroup->addAction(ui->actionThemeNone);
    themeGroup->setExclusive(true);
    connect(themeGroup, &QActionGroup::triggered, this, [this](QAction *action) {
        onActionSwitchTheme(action == ui->actionThemeDark ? QString(themeDark)
                                                          : QString(themeNone));
    });

    m_recentFiles.reset(new RecentFiles(ui->actionRecent));
    connect(m_recentFiles.get(), &RecentFiles::countChanged, this, [&](int count){
        ui->actionRecent->setText(tr("%n recent files", nullptr, count));
    });

    setAcceptDrops(true);

    readSettings();
    m_factory.reset(new ViewerFactory(ui->viewArea, this));
    const QStringList &viewers = m_factory->viewerNames();

    const QString msg = tr("Available viewers: %1").arg(viewers.join(", "_L1));
    statusBar()->showMessage(msg);

    setupModeMenu();

    auto *menu = new RecentFileMenu(this, m_recentFiles.get());
    ui->actionRecent->setMenu(menu);
    connect(menu, &RecentFileMenu::fileOpened, this, &MainWindow::openFile);
    QWidget *w = ui->mainToolBar->widgetForAction(ui->actionRecent);
    auto *button = qobject_cast<QToolButton *>(w);
    if (button)
        connect(ui->actionRecent, &QAction::triggered, button, &QToolButton::showMenu);
}

void MainWindow::setupModeMenu()
{
    ui->actiontext_viewer->setData(u"TxtViewer"_s);
    ui->actionjson_viewer->setData(u"JsonViewer"_s);
    ui->actionimage_viewer->setData(u"ImageViewer"_s);
    ui->actionyuv_viewer->setData(u"YuvViewer"_s);

    auto *modeGroup = new QActionGroup(this);
    modeGroup->setExclusionPolicy(QActionGroup::ExclusionPolicy::ExclusiveOptional);

    const QList<QAction *> modeActions = ui->menuMode->actions();
    for (QAction *action : modeActions) {
        action->setCheckable(true);
        modeGroup->addAction(action);
    }

    connect(modeGroup, &QActionGroup::triggered, this, [this](QAction *action) {
        onActionSwitchViewer(action->data().toString());
    });

    updateModeMenu();
}

void MainWindow::updateModeMenu()
{
    const bool hasFile = !m_currentFile.isEmpty();
    const QString activeViewer = m_viewer ? m_viewer->viewerName() : QString();

    const QList<QAction *> modeActions = ui->menuMode->actions();
    for (QAction *action : modeActions) {
        const QString viewerName = action->data().toString();
        action->setEnabled(hasFile && m_factory && m_factory->hasViewer(viewerName));
        action->setChecked(!viewerName.isEmpty() && viewerName == activeViewer);
    }
}

void MainWindow::onActionSwitchViewer(const QString &viewerName)
{
    if (m_currentFile.isEmpty()) {
        showViewerError(tr("Open a file before choosing a viewer."));
        updateModeMenu();
        return;
    }

    openFileWithViewer(m_currentFile, viewerName);
}

void MainWindow::showViewerError(const QString &message)
{
    auto *label = new QLabel(message);
    label->setAlignment(Qt::AlignCenter);
    label->setWordWrap(true);
    label->setMargin(24);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    // The scroll area takes ownership and destroys the previous viewer widget.
    ui->scrollArea->setWidget(label);
}

bool MainWindow::hasPlugins() const
{
    return m_factory ? !m_factory->viewers().isEmpty() : false;
}

MainWindow::~MainWindow()
{
    saveSettings();
}

void MainWindow::onActionSwitchLanguage(QLocale::Language lang)
{
    QLocale::setDefault(QLocale(lang));
    QEvent event(QEvent::LocaleChange);
    QCoreApplication::sendEvent(this, &event);
}

void MainWindow::onActionSwitchTheme(const QString &themeId)
{
    applyTheme(themeId);
    QSettings settings;
    settings.setValue(settingsTheme, m_themeId);
}

void MainWindow::applyTheme(const QString &themeId)
{
    if (themeId != themeNone) {
        QFile file(u":/qdarkstyle/dark/darkstyle.qss"_s);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            statusBar()->showMessage(tr("Unable to load the dark theme."));
            qApp->setStyleSheet(QString());
            m_themeId = QString(themeNone);
        } else {
            qApp->setStyleSheet(QString::fromUtf8(file.readAll()));
            m_themeId = QString(themeDark);
        }
    } else {
        qApp->setStyleSheet(QString());
        m_themeId = QString(themeNone);
    }

    const QSignalBlocker darkBlocker(ui->actionThemeDark);
    const QSignalBlocker noneBlocker(ui->actionThemeNone);
    ui->actionThemeDark->setChecked(m_themeId == themeDark);
    ui->actionThemeNone->setChecked(m_themeId == themeNone);
}

void MainWindow::onActionOpenTriggered()
{
    QFileDialog fileDialog(this, tr("Open Document"), m_currentDir.absolutePath());
    fileDialog.setOptions(QFileDialog::DontUseNativeDialog);
    while (fileDialog.exec() == QDialog::Accepted
           && !openFile(fileDialog.selectedFiles().constFirst())) {
    }
}

bool MainWindow::openFile(const QString &fileName)
{
    return openFileWithViewer(fileName, QString());
}

bool MainWindow::openFileWithViewer(const QString &fileName, const QString &viewerName)
{
    auto file = std::make_unique<QFile>(fileName);
    if (!file->exists()) {
        const QString message = tr("File %1 could not be opened")
                                    .arg(QDir::toNativeSeparators(fileName));
        statusBar()->showMessage(message);
        return false;
    }

    const QFileInfo fileInfo(*file);
    m_currentDir = fileInfo.dir();
    m_currentFile = fileInfo.absoluteFilePath();
    m_recentFiles->addFile(m_currentFile);

    detachViewer();

    AbstractViewer *viewer = viewerName.isEmpty()
            ? m_factory->viewer(file.get())
            : m_factory->namedViewer(file.get(), viewerName);
    if (!viewer) {
        const QString message = viewerName.isEmpty()
                ? tr("File %1 can't be opened.").arg(QDir::toNativeSeparators(fileName))
                : tr("%1 cannot open %2.").arg(viewerName,
                                               QDir::toNativeSeparators(fileName));
        statusBar()->showMessage(message);
        showViewerError(message);
        updateModeMenu();
        return false;
    }

    file.release(); // The viewer owns the file from here on.
    return attachViewer(viewer);
}

bool MainWindow::openData(const QByteArray &data, const QString &mimeType)
{
    detachViewer();
    // Dropped data has no file behind it, so there is nothing to re-open in a
    // different mode.
    m_currentFile.clear();

    AbstractViewer *viewer = m_factory->viewer(data, mimeType);
    if (!viewer) {
        const QString message = tr("No viewer can display %1 data.").arg(mimeType);
        statusBar()->showMessage(message);
        showViewerError(message);
        updateModeMenu();
        return false;
    }

    return attachViewer(viewer);
}

void MainWindow::detachViewer()
{
    // Saves the viewer state and stops its background work while its widget is
    // still alive, then drops the pointer because whatever is shown next takes
    // over the scroll area and destroys that widget.
    resetViewer();

    for (const QMetaObject::Connection &connection : m_viewerConnections)
        disconnect(connection);
    m_viewerConnections = {};
    m_viewer = nullptr;
    ui->actionPrint->setEnabled(false);
}

bool MainWindow::attachViewer(AbstractViewer *viewer)
{
    Q_ASSERT(viewer);
    m_viewer = viewer;

    m_viewerConnections = {
        connect(m_viewer, &AbstractViewer::printingEnabledChanged, ui->actionPrint,
                &QAction::setEnabled),
        connect(ui->actionPrint, &QAction::triggered, m_viewer, &AbstractViewer::print),
        connect(m_viewer, &AbstractViewer::showMessage, statusBar(), &QStatusBar::showMessage)
    };

    m_viewer->initViewer(ui->actionBack, ui->actionForward, ui->menuHelp->menuAction(),
                         ui->tabWidget);
    restoreViewerSettings();
    ui->scrollArea->setWidget(m_viewer->widget());
    updateModeMenu();
    return true;
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    event->acceptProposedAction();
    return;
}

void MainWindow::dragMoveEvent(QDragMoveEvent *event)
{
    event->acceptProposedAction();
    return;
}

void MainWindow::dropEvent(QDropEvent *event)
{
    const QMimeData *mime = event->mimeData();

    if (mime->hasUrls()) {
        for (const QUrl &url : mime->urls()) {
            if (url.isLocalFile()) {
                if (openFile(url.toLocalFile())) {
                    event->acceptProposedAction();
                    return;
                }
            }
        }
    }

    for (const QString &format : mime->formats()) {
        QByteArray data = mime->data(format);

#ifdef Q_OS_WIN
        if (format.contains(QStringLiteral("FileContents"))) {
            for (int i = 0; ; ++i) {
                QString indexed = format + QStringLiteral(";index=") + QString::number(i);
                data = mime->data(indexed);

                if (data.isEmpty())
                    break;

                openData(data, QStringLiteral("application/octet-stream"));
                event->acceptProposedAction();
                return;
            }
        }
#endif

        if (data.isEmpty())
            continue;

        else if (format.contains(QStringLiteral("image/"))) {
            openData(data, format);
            event->acceptProposedAction();
            return;
        }

        else if (mime->hasImage())
            continue;

        else if (format == QStringLiteral("application/json") ||
                 format == QStringLiteral("text/plain")) {
            QString detectedFormat = format;

            if (format == QStringLiteral("text/plain")) {
                QByteArray trimmed = data.trimmed();

                if (trimmed.startsWith('{') || trimmed.startsWith('[')) {
                    detectedFormat = QStringLiteral("application/json");
                }
            }

            openData(data, detectedFormat);
            event->acceptProposedAction();
            return;
        }

    }
    event->ignore();
}

void MainWindow::changeEvent(QEvent *event)
{
    switch (event->type()) {
    case QEvent::LanguageChange:
        ui->retranslateUi(this);
        statusBar()->clearMessage();
        break;
    case QEvent::LocaleChange:
        m_translator.setLanguage(QLocale().language());
        m_translator.install();
        break;
    default:
        break;
    }

    QMainWindow::changeEvent(event);
}

void MainWindow::onActionAboutTriggered()
{
    const QString viewerNames = m_factory->viewerNames().join(", "_L1);
    const QString mimeTypes = m_factory->supportedMimeTypes().join(u'\n');
    QString text = tr("A Widgets application to display and print JSON, "
                      "text and PDF files. Demonstrates various features to use "
                      "in widget applications: Using QSettings, query and save "
                      "user preferences, manage file histories and control cursor "
                      "behavior when hovering over widgets.\n\n"
                      "This version has loaded the following plugins:\n%1\n"
                      "\n\nIt supports the following mime types:\n%2")
                    .arg(viewerNames, mimeTypes);

    if (auto *def = m_factory->defaultViewer())
        text += tr("\n\nOther mime types will be displayed with %1.").arg(def->viewerName());

    QMessageBox::about(this, tr("About StepViewer"), text);
}

void MainWindow::onActionAboutQtTriggered()
{
    QMessageBox::aboutQt(this);
}

void MainWindow::readSettings()
{
    QSettings settings;

    // Restore working directory
    if (settings.contains(settingsDir))
        m_currentDir = QDir(settings.value(settingsDir).toString());
    else
        m_currentDir = QDir::current();

    // Restore QMainWindow state
    if (settings.contains(settingsMainWindow)) {
        QByteArray mainWindowState = settings.value(settingsMainWindow).toByteArray();
        restoreState(mainWindowState);
    }

    // Restore recent files
    m_recentFiles->restoreFromSettings(settings, settingsFiles);

    // Dark is the default when no theme has been saved yet.
    applyTheme(settings.value(settingsTheme, QString(themeDark)).toString());
}

void MainWindow::saveSettings() const
{
    QSettings settings;

    // Save working directory
    settings.setValue(settingsDir, m_currentDir.absolutePath());

    // Save QMainWindow state
    settings.setValue(settingsMainWindow, saveState());

    // Save recent files
    m_recentFiles->saveSettings(settings, settingsFiles);

    settings.setValue(settingsTheme, m_themeId);

    settings.sync();
}

void MainWindow::saveViewerSettings() const
{
    if (!m_viewer)
        return;

    QSettings settings;
    settings.beginGroup(settingsViewers);
        settings.setValue(m_viewer->viewerName(), m_viewer->saveState());
    settings.endGroup();
    settings.sync();
}

void MainWindow::resetViewer() const
{
    if (!m_viewer)
        return;

    saveViewerSettings();
    m_viewer->cleanup();
}

void MainWindow::restoreViewerSettings()
{
    if (!m_viewer)
        return;

    QSettings settings;
    settings.beginGroup(settingsViewers);
    QByteArray viewerSettings = settings.value(m_viewer->viewerName(), QByteArray()).toByteArray();
    settings.endGroup();
    if (!viewerSettings.isEmpty())
        m_viewer->restoreState(viewerSettings);
}

