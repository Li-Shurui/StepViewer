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
#include <QPromise>
#include <QScrollArea>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>
#include <QtCompilerDetection>
#include <QtConcurrentRun>

#include <expected>
#include <functional>
#include <type_traits>
#include <utility>

class Translator;
class QTableWidget;

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

    // Viewers returning true are offered files that have no file name
    // extension (e.g. raw data), before the factory falls back to mime
    // type sniffing and the default viewer.
    virtual bool supportsExtensionlessFiles() const;

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

    // Variant for tasks with progress reporting: work must take a
    // QPromise<Result> & as first parameter, return void, and deliver its
    // result via QPromise::addResult() (QtConcurrent promise-mode rules).
    // progress is invoked on the UI thread with the latest progress value.
    template <typename Result, typename Work, typename Progress, typename Done>
    void startAsyncTaskWithProgress(Work &&work, Progress &&progress, Done &&done);

    // Invalidates the results of all pending tasks and requests their
    // cancellation. Cancellation is cooperative: tasks poll
    // QPromise::isCanceled() to actually stop early.
    void cancelAsyncTasks();

    // Reads a whole file in chunks, invoking progress(bytesRead, totalBytes)
    // after each chunk; the callback returns false to abort the read.
    // Suitable for worker threads; opens its own QFile.
    using ReadProgressCallback = std::function<bool(qint64 bytesRead, qint64 totalBytes)>;
    static std::expected<QByteArray, QString> readFileChunked(const QString &fileName,
                                                              const ReadProgressCallback &progress);

    // Creates a two-column read-only property table as a new page in the
    // overview tab widget and returns it. Call only after uiInitialized()
    // (the tab widget is not available during init()). Pages are owned by
    // the viewer and deleted automatically in cleanup(); do not delete them
    // manually, and reset any stored pointer when overriding cleanup().
    // Only visible if supportsOverview() returns true.
    QTableWidget *addInfoTab(const QString &title);

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
    QList<QTableWidget *> m_infoTabs;
    quint64 m_taskGeneration = 0;
    int m_busyTasks = 0;
    bool m_printingEnabled = false;
    QLocale m_currentLocale;
    std::unique_ptr<Translator> m_translator;
};

template <typename Result, typename Work, typename Progress, typename Done>
void AbstractViewer::startAsyncTaskWithProgress(Work &&work, Progress &&progress, Done &&done)
{
    static_assert(std::is_invocable_v<std::decay_t<Work>, QPromise<Result> &>,
                  "work must be callable as void(QPromise<Result> &)");

    auto *watcher = new QFutureWatcher<Result>(this);
    const quint64 generation = ++m_taskGeneration;
    m_asyncWatchers.append(watcher);
    asyncTaskStarted();

    // Keep our own future reference: QFutureWatcher does not expose a typed
    // takeResult(), and results of move-only types must be moved out.
    QFuture<Result> future = QtConcurrent::run(std::forward<Work>(work));

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
    connect(watcher, &QFutureWatcherBase::progressValueChanged, this,
            [this, generation,
             progress = std::forward<Progress>(progress)](int value) mutable {
                if (generation == m_taskGeneration)
                    progress(value);
            });

    watcher->setFuture(future);
}

template <typename Work, typename Done>
void AbstractViewer::startAsyncTask(Work &&work, Done &&done)
{
    using Result = std::invoke_result_t<std::decay_t<Work>>;

    startAsyncTaskWithProgress<Result>(
        [work = std::forward<Work>(work)](QPromise<Result> &promise) mutable {
            promise.addResult(work());
        },
        [](int) { },
        std::forward<Done>(done));
}

#endif // ABSTRACTVIEWER_H
