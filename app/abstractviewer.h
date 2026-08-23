// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#ifndef ABSTRACTVIEWER_H
#define ABSTRACTVIEWER_H

#include "abstractviewerglobal.h"

#include <QAction>
#include <QFile>
#include <QFutureWatcher>
#include <QLocale>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QObject>
#include <QPrinter>
#include <QScrollArea>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>
#include <QtCompilerDetection>
#include <QtConcurrentRun>

#include <functional>
#include <type_traits>
#include <utility>

class Translator;

class ABSTRACTVIEWER_EXPORT AbstractViewer : public QObject
{
    Q_OBJECT

protected:
    AbstractViewer();

public:
    virtual ~AbstractViewer();
    virtual void init(QFile *file, QWidget *widget, QMainWindow *mainWindow);
    void initViewer(QAction *back, QAction *forward, QAction *help, QTabWidget *tabs);
    virtual bool isModified() const;
    virtual bool saveDocument();
    virtual bool saveDocumentAs();
    virtual QString viewerName() const = 0;
    bool eventFilter(QObject *object, QEvent *event) override;
    virtual bool supportsOverview() const;
    virtual QByteArray saveState() const = 0;
    virtual bool restoreState(QByteArray &) = 0;
    virtual bool hasContent() const;
    virtual QStringList supportedMimeTypes() const = 0;
    virtual QStringList supportedExtensions() const { return {}; }
    virtual bool isDefaultViewer() const;
    virtual void cleanup();
    void setTranslationBaseName(const QString &baseName);
    bool isEmpty() const;
    bool isPrintingEnabled() const;
    AbstractViewer *viewer();
    const AbstractViewer *viewer() const;

    QList<QAction *> actions() const;
    QWidget *widget() const;
    QList<QToolBar *> toolBars() const;
    QList<QMenu *> menus() const;

#ifdef DOCUMENTVIEWER_PRINTSUPPORT
protected:
    virtual void printDocument(QPrinter *) const {};
#endif

signals:
    void uiInitialized();
    void printingEnabledChanged(bool enabled);
    void showMessage(const QString &message, int timeout = 8000);
    void documentLoaded(const QString &fileName);

public slots:
    void print();

protected:

    struct UiAssets {
        QMainWindow *mainWindow = nullptr;
        QAction *back = nullptr;
        QAction *forward = nullptr;
        QAction *help = nullptr;
        QTabWidget *tabs = nullptr;
    } m_uiAssets;

    virtual void retranslate() { };
    void statusMessage(const QString &message, const QString &type = QString(), int timeout = 8000);
    QToolBar *addToolBar();
    QMenu *addMenu();
    QMainWindow *mainWindow() const;
    QStatusBar *statusBar() const;
    QMenuBar *menuBar() const;

    // Runs work on a thread-pool thread and invokes done with its result on
    // the UI thread. Results of tasks superseded by a newer task (or canceled
    // via cancelAsyncTasks()) are discarded. work must not touch UI objects
    // or members of this viewer: capture everything it needs by value, and
    // report errors through the result type (e.g. std::expected), not via
    // exceptions.
    template <typename Work, typename Done>
    void startAsyncTask(Work &&work, Done &&done);

    // Invalidates the results of all pending tasks and requests their
    // cancellation. Cancellation is cooperative: tasks poll
    // QPromise::isCanceled() to actually stop early.
    void cancelAsyncTasks();

    // Called on the UI thread when the number of running async tasks changes
    // between zero and non-zero. The base implementation toggles a busy
    // cursor; reimplementations that toggle viewer UI must call it.
    virtual void busyChanged(bool busy);
    bool isBusy() const { return m_busyTasks > 0; }

    std::unique_ptr<QFile> m_file;
    QList<QAction *> m_actions;
    QWidget *m_widget = nullptr;
    QList<QMetaObject::Connection> m_connections;

protected slots:
    void maybeSetPrintingEnabled(bool enabled);
    void maybeEnablePrinting();
    void disablePrinting();

private:
    void asyncTaskStarted();
    void asyncTaskFinished();
    void waitForAsyncTasks();

    QList<QMenu *> m_menus;
    QList<QToolBar *> m_toolBars;
    QList<QFutureWatcherBase *> m_asyncWatchers;
    quint64 m_taskGeneration = 0;
    int m_busyTasks = 0;
    bool m_printingEnabled = false;
    QLocale m_currentLocale;
    std::unique_ptr<Translator> m_translator;
};

template <typename Work, typename Done>
void AbstractViewer::startAsyncTask(Work &&work, Done &&done)
{
    using Result = std::invoke_result_t<std::decay_t<Work>>;

    auto *watcher = new QFutureWatcher<Result>(this);
    const quint64 generation = ++m_taskGeneration;
    m_asyncWatchers.append(watcher);
    asyncTaskStarted();

    // Keep our own future reference: QFutureWatcher does not expose a typed
    // takeResult(), and results of move-only types must be moved out.
    const QFuture<Result> future = QtConcurrent::run(std::forward<Work>(work));

    connect(watcher, &QFutureWatcherBase::finished, this,
            [this, watcher, generation, future,
             done = std::forward<Done>(done)]() mutable {
                m_asyncWatchers.removeAll(watcher);
                asyncTaskFinished();
                watcher->deleteLater();
                if (generation != m_taskGeneration)
                    return;  // superseded or canceled: discard the result
                done(future.takeResult());
            });

    watcher->setFuture(future);
}

#endif // ABSTRACTVIEWER_H
