// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#include "abstractviewer.h"
#include "translator.h"

#include <QApplication>
#include <QHeaderView>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QScrollArea>
#include <QStatusBar>
#include <QTableWidget>
#include <QTabWidget>
#include <QToolBar>

#include <QAction>

#include <QFile>
#include <QSettings>

#ifdef DOCUMENTVIEWER_PRINTSUPPORT
#include <QPrinter>
#include <QPrintDialog>
#endif // DOCUMENTVIEWER_PRINTSUPPORT

using namespace Qt::StringLiterals;

namespace {

QStringList infoTabHeaders()
{
    return { AbstractViewer::tr("Property"), AbstractViewer::tr("Value") };
}

} // namespace

AbstractViewer::AbstractViewer() : m_file(nullptr), m_widget(nullptr)
{
}

void AbstractViewer::init(QFile *file, QWidget *widget, QMainWindow *mainWindow)
{
    Q_ASSERT(widget);
    Q_ASSERT(mainWindow);
    Q_ASSERT(file);
    m_file.reset(file);
    m_widget = widget;
    m_uiAssets.mainWindow = mainWindow;
    mainWindow->installEventFilter(this);
}

AbstractViewer::~AbstractViewer()
{
    // Async tasks may be executing code of a plugin library. Cancel them and
    // wait for completion before the viewer (and potentially the plugin) is
    // destroyed, so no worker thread outlives the code it runs.
    cancelAsyncTasks();
    waitForAsyncTasks();
    AbstractViewer::cleanup();
}

void AbstractViewer::cancelAsyncTasks()
{
    ++m_taskGeneration;
    for (QFutureWatcherBase *watcher : std::as_const(m_asyncWatchers))
        watcher->cancel();
}

void AbstractViewer::waitForAsyncTasks()
{
    const QList<QFutureWatcherBase *> watchers = std::exchange(m_asyncWatchers, {});
    for (QFutureWatcherBase *watcher : watchers) {
        watcher->disconnect(this);
        watcher->waitForFinished();
        delete watcher;
    }
    if (m_busyTasks > 0) {
        m_busyTasks = 0;
        busyChanged(false);
    }
}

void AbstractViewer::asyncTaskStarted()
{
    if (++m_busyTasks == 1)
        busyChanged(true);
}

void AbstractViewer::asyncTaskFinished()
{
    if (--m_busyTasks == 0)
        busyChanged(false);
}

void AbstractViewer::busyChanged(bool busy)
{
#if QT_CONFIG(cursor)
    if (busy)
        QGuiApplication::setOverrideCursor(Qt::BusyCursor);
    else
        QGuiApplication::restoreOverrideCursor();
#endif
}

QTableWidget *AbstractViewer::addInfoTab(const QString &title)
{
    QTabWidget *tabs = m_uiAssets.tabs;
    if (!tabs)
        return nullptr;

    auto *table = new QTableWidget(tabs);
    table->setColumnCount(2);
    table->setHorizontalHeaderLabels(infoTabHeaders());
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->verticalHeader()->hide();
    table->horizontalHeader()->setStretchLastSection(true);
    tabs->addTab(table, title);
    m_infoTabs.append(table);
    return table;
}

void AbstractViewer::addTabPage(QWidget *page, const QString &title)
{
    QTabWidget *tabs = m_uiAssets.tabs;
    if (!tabs || !page)
        return;

    page->setParent(tabs);
    tabs->addTab(page, title);
    m_tabPages.append(page);
}

std::expected<QByteArray, QString> AbstractViewer::readFileChunked(
        const QString &fileName, const ReadProgressCallback &progress)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        return std::unexpected(tr("Cannot open the file: %1")
                                   .arg(file.errorString()));
    }

    constexpr qint64 chunkSize = 4 * 1024 * 1024;
    const qint64 total = file.size();
    QByteArray data(total, Qt::Uninitialized);
    qint64 offset = 0;
    while (offset < total) {
        const qint64 bytesRead = file.read(data.data() + offset,
                                           qMin(chunkSize, total - offset));
        if (bytesRead < 0) {
            return std::unexpected(tr("Failed while reading the file: %1")
                                       .arg(file.errorString()));
        }
        if (bytesRead == 0)
            break;  // the file shrank mid-read; reported below
        offset += bytesRead;
        if (progress && !progress(offset, total))
            return std::unexpected(tr("Loading canceled."));
    }

    if (offset != total) {
        return std::unexpected(tr("The file read was incomplete. "
                                  "Expected %1 bytes, received %2 bytes.")
                                   .arg(total)
                                   .arg(offset));
    }
    return data;
}

void AbstractViewer::setTranslationBaseName(const QString &baseName)
{
    m_translator.reset(new Translator);
    m_translator->setBaseName(baseName);
    m_translator->install();
}

bool AbstractViewer::eventFilter(QObject *, QEvent *event)
{
    if (event->type() != QEvent::LanguageChange)
        return false;

    const QLocale locale;
    if (locale != m_currentLocale) {
        m_currentLocale = locale;
        if (m_translator) {
            m_translator->setLanguage(locale.language());
            m_translator->install();
        }
        retranslate();
        for (QTableWidget *table : std::as_const(m_infoTabs))
            table->setHorizontalHeaderLabels(infoTabHeaders());
    }
    return false;
}

bool AbstractViewer::isEmpty() const
{
    return !hasContent();
}

bool AbstractViewer::isPrintingEnabled() const
{
    return m_printingEnabled;
}

bool AbstractViewer::hasContent() const
{
    return false;
}

bool AbstractViewer::supportsOverview() const
{
    return false;
}

bool AbstractViewer::isModified() const
{
    return false;
}

bool AbstractViewer::saveDocument()
{
    return false;
}

bool AbstractViewer::saveDocumentAs()
{
    return false;
}

QList<QAction *> AbstractViewer::actions() const
{
    return m_actions;
}

QWidget *AbstractViewer::widget() const
{
    return m_widget;
}

QList<QToolBar *> AbstractViewer::toolBars() const
{
    return m_toolBars;
}

QList<QMenu *> AbstractViewer::menus() const
{
    return m_menus;
}

QMainWindow *AbstractViewer::mainWindow() const
{
    return m_uiAssets.mainWindow;
}

QStatusBar *AbstractViewer::statusBar() const
{
    return mainWindow()->statusBar();
}

QMenuBar *AbstractViewer::menuBar() const
{
    return mainWindow()->menuBar();
}

void AbstractViewer::maybeEnablePrinting()
{
    maybeSetPrintingEnabled(true);
}

void AbstractViewer::disablePrinting()
{
    maybeSetPrintingEnabled(false);
}

bool AbstractViewer::supportsExtensionlessFiles() const
{
    return false;
}

bool AbstractViewer::isDefaultViewer() const
{
    return false;
}

AbstractViewer *AbstractViewer::viewer()
{
    return this;
}

const AbstractViewer *AbstractViewer::viewer() const
{
    return this;
}

void AbstractViewer::statusMessage(const QString &message, const QString &type, int timeout)
{
    const QString msg = viewerName()
                        + (type.isEmpty() ? ": "_L1 : "/"_L1 + type + ": "_L1) + message;
    emit showMessage(msg, timeout);
}

QToolBar *AbstractViewer::addToolBar()
{
    auto *bar = new QToolBar();
    mainWindow()->addToolBar(bar);
    bar->setObjectName(viewerName() + "ToolBar"_L1);
    m_toolBars.append(bar);
    return bar;
}

QMenu *AbstractViewer::addMenu()
{
    QMenu *menu = new QMenu(menuBar());
    menu->setObjectName(viewerName() + "Menu"_L1);
    menuBar()->insertMenu(m_uiAssets.help, menu);
    m_menus.append(menu);
    return menu;
}

void AbstractViewer::cleanup()
{
    // delete all objects created by the viewer which need to be displayed
    // and therefore parented on MainWindow
    if (m_uiAssets.mainWindow)
        m_uiAssets.mainWindow->removeEventFilter(this);

    if (m_file)
        m_file.reset();

    qDeleteAll(m_menus);
    m_menus.clear();
    qDeleteAll(m_toolBars);
    m_toolBars.clear();

    // Deleting a page widget also removes its tab from the tab widget.
    qDeleteAll(m_infoTabs);
    m_infoTabs.clear();
    qDeleteAll(m_tabPages);
    m_tabPages.clear();

    for (const auto &connection : m_connections)
        QObject::disconnect(connection);

    m_connections.clear();
    maybeSetPrintingEnabled(false);
}

void AbstractViewer::print()
{
#ifdef DOCUMENTVIEWER_PRINTSUPPORT
    static const QString type = tr("Printing");
    if (!hasContent()) {
        statusMessage(tr("No content to print."), type);
        return;
    }

    QPrinter printer(QPrinter::HighResolution);
    QPrintDialog dlg(&printer, mainWindow());
    dlg.setWindowTitle(tr("Print Document"));
    if (dlg.exec() == QDialog::Accepted) {
        printDocument(&printer);
    } else {
        statusMessage(tr("Printing canceled!"), type);
        return;
    }

    const QPrinter::PrinterState state = printer.printerState();
    QString message = viewerName() + " :"_L1;
    switch (state) {
    case QPrinter::PrinterState::Aborted:
        message += tr("Printing aborted.");
        break;
    case QPrinter::PrinterState::Active:
        message += tr("Printing active.");
        break;
    case QPrinter::PrinterState::Idle:
        message += tr("Printing completed.");
        break;
    case QPrinter::PrinterState::Error:
        message += tr("Printing error.");
        break;
    }
    statusMessage(message, type);
#else
    statusMessage(tr("Printing not supported!"));
#endif
}

/*!
   \brief AbstractViewer::setPrintingEnabled
   Enables / disables printing.
   If printing is not supported or the viewer has no content to display,
   \param enabled is overridden with \c false;
   The signal printingEnabledChanged is emitted if the status has changed.
 */
void AbstractViewer::maybeSetPrintingEnabled(bool enabled)
{
#ifndef DOCUMENTVIEWER_PRINTSUPPORT
    enabled = false;
#else
    if (!hasContent())
        enabled = false;
#endif

    if (enabled == m_printingEnabled)
        return;

    m_printingEnabled = enabled;
    emit printingEnabledChanged(enabled);
}

void AbstractViewer::initViewer(QAction *back, QAction *forward, QAction *help, QTabWidget *tabs)
{
    // Viewers need back & forward buttons and a tab widget.
    Q_ASSERT(back);
    Q_ASSERT(forward);
    Q_ASSERT(tabs);
    Q_ASSERT(help);

    m_uiAssets.back = back;
    m_uiAssets.forward = forward;
    m_uiAssets.help = help;
    m_uiAssets.tabs = tabs;

    // Tabs can be populated individually by the viewer, if it supports overview
    tabs->clear();
    tabs->setVisible(supportsOverview());

    emit uiInitialized();
}
