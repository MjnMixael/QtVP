#include "MainWindow.h"
#include "ui_MainWindow.h"

#include "Icons.h"
#include "Core/VpArchive.h"
#include "Models/FileListModel.h"
#include "Models/FolderTreeModel.h"
#include "Previews/PreviewLoader.h"
#include "Previews/PreviewWidget.h"
#include "Windows/FileOpener.h"
#include "Windows/OptionsDialog.h"
#include "Windows/PreviewWindow.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHash>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QStyle>
#include <QTemporaryDir>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <numeric>

namespace {

constexpr int MaxRecentFiles = 10;

bool isVpPath(const QString& path)
{
    const QString suffix = QFileInfo(path).suffix();
    return suffix.compare("vp", Qt::CaseInsensitive) == 0 || suffix.compare("vpc", Qt::CaseInsensitive) == 0;
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_currentFolder(FolderTreeModel::NoFolder)
{
    ui->setupUi(this);

    // Tree and preview share the left column; the file list gets the extra width
    ui->mainSplitter->setStretchFactor(0, 0);
    ui->mainSplitter->setStretchFactor(1, 1);
    ui->mainSplitter->setSizes({ 300, 700 });
    ui->leftSplitter->setSizes({ 450, 150 });

    // Dropped VPs are handled by the window
    ui->filterEdit->setAcceptDrops(false);

    m_recentMenu = new QMenu(tr("&Recent VPs"), this);
    ui->menuFile->insertMenu(ui->actionExit, m_recentMenu);
    ui->menuFile->insertSeparator(ui->actionExit);

    setupIcons();
    setupModels();
    setupConnections();
    restoreLayout();
    applySettings();
    updateRecentMenu();
    updateActions();
    updatePlaybackButtons();
    updateSelectionStatus();

    statusBar()->showMessage(tr("Ready"));
}

// Runs behind the splash screen
void MainWindow::warmUp()
{
    // Starting the audio backend takes a moment and has to happen on the UI thread
    ui->previewArea->prepareAudio();

    // Files opened in other apps stay until the next start, so an editor never loses its
    // file because QtVP closed. Clear out anything older than a day.
    const QDir openRoot(openFolderRoot());
    const QDateTime cutoff = QDateTime::currentDateTime().addDays(-1);
    for (const QFileInfo& folder : openRoot.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (folder.lastModified() < cutoff)
            QDir(folder.absoluteFilePath()).removeRecursively();
    }
}

// Settings read at startup and after the Options dialog
void MainWindow::applySettings()
{
    // While the pop-out is open the pane stays quiet
    ui->previewArea->setAutoplayMedia(OptionsDialog::autoplayMedia() && !previewWindowOpen());
}

QString MainWindow::openFolderRoot()
{
    return QDir::temp().filePath("QtVP/Open");
}

// Extracts the selected files to a fresh temp folder and hands each to another app.
// Each file gets its own subfolder, so names never collide and an EFF keeps its frames beside it.
void MainWindow::openSelected(bool chooseApp)
{
    const std::vector<int> entries = selectedEntries();
    if (!m_archive || entries.empty())
        return;

    if (entries.size() > 10) {
        const auto answer = QMessageBox::question(this, tr("Open"),
            tr("Open %n files, each in its own app window?", nullptr, int(entries.size())));
        if (answer != QMessageBox::Yes)
            return;
    }

    // Everything this session opens lives under one folder, so cleanup on exit
    // never touches files another running QtVP handed out
    if (m_sessionOpenFolder.isEmpty()) {
        m_sessionOpenFolder = QDir(openFolderRoot()).filePath(QString("%1-%2")
            .arg(QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss")).arg(QCoreApplication::applicationPid()));
    }
    const QString root = QDir(m_sessionOpenFolder).filePath(QString::number(++m_openCount));
    FileOpener opener(this);
    QStringList errors;

    for (size_t i = 0; i < entries.size(); ++i) {
        const int entry = entries[i];
        const VpEntry& e = m_archive->entries()[entry];
        const QString dir = QDir(root).filePath(QString::number(i));

        std::vector<int> files{ entry };
        if (QFileInfo(e.name).suffix().compare("eff", Qt::CaseInsensitive) == 0) {
            const PreviewLoader::EffInfo eff = PreviewLoader::readEff(*m_archive, entry);
            files.insert(files.end(), eff.frames.begin(), eff.frames.end());
            if (!eff.error.isEmpty())
                errors << tr("%1: %2").arg(e.name, eff.error);
        }

        QApplication::setOverrideCursor(Qt::WaitCursor);
        bool extracted = true;
        for (const int file : files) {
            const VpArchive::ExtractResult result = m_archive->extract({ file }, dir, m_archive->entries()[file].folder);
            if (result.extracted != 1) {
                errors << result.errors;
                if (file == entry)
                    extracted = false;
            }
        }
        QApplication::restoreOverrideCursor();

        if (!extracted)
            continue;
        const QString path = QDir(dir).filePath(e.name);
        if (chooseApp)
            opener.openWith(path);
        else
            opener.open(path);
    }

    if (!errors.isEmpty()) {
        QMessageBox box(QMessageBox::Warning, tr("Open"), tr("Some files could not be prepared for opening."),
            QMessageBox::Ok, this);
        box.setDetailedText(errors.join('\n'));
        box.exec();
    }
}

MainWindow::~MainWindow()
{
    // The models outlive this destructor as child objects; keep them off the archive
    waitForPreviewLoad();
    m_folderModel->setArchive(nullptr);
    m_fileModel->setArchive(nullptr);
    delete ui;
}

void MainWindow::openVp(const QString& path)
{
    // Parse into a fresh archive so a bad file leaves the current one open
    auto archive = std::make_unique<VpArchive>();
    if (!archive->open(path)) {
        QMessageBox::warning(this, tr("Load VP"), tr("Could not open %1.\n\n%2")
            .arg(QDir::toNativeSeparators(path), archive->errorString()));
        if (!QFileInfo::exists(path))
            removeRecentFile(path);
        return;
    }

    // Detach the models and finish any preview load before the old archive is destroyed
    waitForPreviewLoad();
    m_folderModel->setArchive(nullptr);
    m_fileModel->setArchive(nullptr);
    m_archive = std::move(archive);
    m_folderModel->setArchive(m_archive.get());
    m_fileModel->setArchive(m_archive.get());

    // Start on <All files> with the top-level folders open
    ui->folderTree->expandToDepth(0);
    ui->folderTree->setCurrentIndex(m_folderModel->allFilesIndex());
    resetPreview();

    addRecentFile(path);
    setWindowTitle(tr("QtVP - %1").arg(QFileInfo(path).fileName()));

    QString status = tr("Opened %1").arg(QDir::toNativeSeparators(path));
    const QStringList warnings = m_archive->warnings();
    if (!warnings.isEmpty())
        status += tr(". Warning: %1").arg(warnings.join(' '));
    statusBar()->showMessage(status);

    updateActions();
}

void MainWindow::closeVp()
{
    waitForPreviewLoad();
    m_folderModel->setArchive(nullptr);
    m_fileModel->setArchive(nullptr);
    m_archive.reset();
    m_currentFolder = FolderTreeModel::NoFolder;
    resetPreview();

    setWindowTitle(tr("QtVP"));
    statusBar()->showMessage(tr("Ready"));
    updateSelectionStatus();
    updateActions();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    // Lets the pop-out save its geometry
    if (m_previewWindow)
        m_previewWindow->close();
    saveLayout();

    // Drag-out files go with their QTemporaryDir either way; this covers files opened in other apps
    if (OptionsDialog::cleanupTempOnExit() && !m_sessionOpenFolder.isEmpty())
        QDir(m_sessionOpenFolder).removeRecursively();

    event->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    // Our own drag-outs have a source widget; only take VPs from outside
    if (!event->source() && event->mimeData()->hasUrls()) {
        for (const QUrl& url : event->mimeData()->urls()) {
            if (url.isLocalFile() && isVpPath(url.toLocalFile())) {
                event->acceptProposedAction();
                return;
            }
        }
    }
    event->ignore();
}

void MainWindow::dropEvent(QDropEvent* event)
{
    for (const QUrl& url : event->mimeData()->urls()) {
        if (url.isLocalFile() && isVpPath(url.toLocalFile())) {
            event->acceptProposedAction();

            // Open after the drop returns so an error box does not hold up Explorer
            const QString path = url.toLocalFile();
            QTimer::singleShot(0, this, [this, path] { openVp(path); });
            return;
        }
    }
    event->ignore();
}

void MainWindow::setupIcons()
{
    // Placeholder platform icons until the app gets its own set
    QStyle* s = style();
    ui->actionLoadVp->setIcon(s->standardIcon(QStyle::SP_DialogOpenButton));
    ui->actionNewVp->setIcon(s->standardIcon(QStyle::SP_FileIcon));
    ui->actionExtractToDir->setIcon(s->standardIcon(QStyle::SP_DirOpenIcon));
    ui->actionOptions->setIcon(s->standardIcon(QStyle::SP_FileDialogDetailedView));

    ui->playButton->setIcon(Icons::icon(Icons::Shape::Play));
    ui->stopButton->setIcon(Icons::icon(Icons::Shape::Stop));
    ui->popOutButton->setIcon(Icons::icon(Icons::Shape::PopOut));
}

void MainWindow::setupModels()
{
    m_folderModel = new FolderTreeModel(this);
    ui->folderTree->setModel(m_folderModel);

    m_fileModel = new FileListModel(this);
    m_fileModel->setDragProvider([this](const std::vector<int>& entries) { return extractForDrag(entries); });

    m_fileProxy = new QSortFilterProxyModel(this);
    m_fileProxy->setSourceModel(m_fileModel);
    m_fileProxy->setSortRole(FileListModel::SortRole);
    m_fileProxy->setSortCaseSensitivity(Qt::CaseInsensitive);
    m_fileProxy->setFilterKeyColumn(FileListModel::NameColumn);
    ui->fileList->setModel(m_fileProxy);

    QHeaderView* header = ui->fileList->header();
    header->resizeSection(FileListModel::NameColumn, 220);
    header->resizeSection(FileListModel::PathColumn, 200);
    header->resizeSection(FileListModel::TypeColumn, 80);
    header->resizeSection(FileListModel::SizeColumn, 90);
    ui->fileList->setColumnHidden(FileListModel::PathColumn, true);
    ui->fileList->sortByColumn(FileListModel::NameColumn, Qt::AscendingOrder);

    m_selectionLabel = new QLabel(this);
    statusBar()->addPermanentWidget(m_selectionLabel);

    m_previewWatcher = new QFutureWatcher<PreviewContent>(this);
    connect(m_previewWatcher, &QFutureWatcher<PreviewContent>::finished, this, &MainWindow::onPreviewLoaded);
    m_previewSpinnerTimer = new QTimer(this);
    m_previewSpinnerTimer->setSingleShot(true);
    m_previewSpinnerTimer->setInterval(150);
    connect(m_previewSpinnerTimer, &QTimer::timeout, this, &MainWindow::showPreviewSpinner);

    // Right-click menus reuse the main actions
    auto* separator = new QAction(this);
    separator->setSeparator(true);
    ui->fileList->setContextMenuPolicy(Qt::ActionsContextMenu);
    ui->fileList->addActions({ ui->actionOpen, ui->actionOpenWith, separator, ui->actionExtractToDir });
    ui->folderTree->setContextMenuPolicy(Qt::ActionsContextMenu);
    ui->folderTree->addAction(ui->actionExtractToDir);
}

void MainWindow::setupConnections()
{
    connect(ui->actionLoadVp, &QAction::triggered, this, &MainWindow::onLoadVp);
    connect(ui->actionCloseVp, &QAction::triggered, this, &MainWindow::closeVp);
    connect(ui->actionExit, &QAction::triggered, this, &QWidget::close);
    connect(ui->actionExtractToDir, &QAction::triggered, this, &MainWindow::onExtractToDir);
    connect(ui->actionOptions, &QAction::triggered, this, [this] {
        if (OptionsDialog(this).exec() == QDialog::Accepted)
            applySettings();
    });
    connect(ui->actionAbout, &QAction::triggered, this, &MainWindow::onAbout);
    connect(ui->actionAboutQt, &QAction::triggered, qApp, &QApplication::aboutQt);
    // Not in any menu, so it needs the window to own it for Ctrl+F to work
    addAction(ui->actionFind);
    connect(ui->actionFind, &QAction::triggered, this, [this] {
        ui->filterEdit->setFocus();
        ui->filterEdit->selectAll();
    });

    connect(ui->folderTree->selectionModel(), &QItemSelectionModel::currentChanged, this,
        [this](const QModelIndex& current) { showFolder(current); });
    connect(ui->filterEdit, &QLineEdit::textChanged, this, &MainWindow::onFilterChanged);

    connect(ui->fileList->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::updateSelectionStatus);
    connect(ui->fileList->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::updatePreview);
    connect(ui->fileList->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::updateActions);

    // Double-click or Enter, like Explorer
    connect(ui->fileList, &QAbstractItemView::activated, this, [this] { openSelected(false); });
    connect(ui->actionOpen, &QAction::triggered, this, [this] { openSelected(false); });
    connect(ui->actionOpenWith, &QAction::triggered, this, [this] { openSelected(true); });
    connect(ui->popOutButton, &QToolButton::clicked, this, &MainWindow::openPreviewWindow);
    connect(ui->playButton, &QToolButton::clicked, ui->previewArea, &PreviewWidget::togglePlay);
    connect(ui->stopButton, &QToolButton::clicked, ui->previewArea, &PreviewWidget::stop);
    connect(ui->previewArea, &PreviewWidget::playbackChanged, this, &MainWindow::updatePlaybackButtons);
    connect(m_fileProxy, &QAbstractItemModel::modelReset, this, &MainWindow::updateSelectionStatus);
    connect(m_fileProxy, &QAbstractItemModel::rowsInserted, this, &MainWindow::updateSelectionStatus);
    connect(m_fileProxy, &QAbstractItemModel::rowsRemoved, this, &MainWindow::updateSelectionStatus);
}

void MainWindow::restoreLayout()
{
    QSettings settings;
    restoreGeometry(settings.value("window/geometry").toByteArray());
    ui->mainSplitter->restoreState(settings.value("window/mainSplitter").toByteArray());
    ui->leftSplitter->restoreState(settings.value("window/leftSplitter").toByteArray());

    QHeaderView* header = ui->fileList->header();
    if (header->restoreState(settings.value("window/fileListHeader").toByteArray()))
        ui->fileList->sortByColumn(header->sortIndicatorSection(), header->sortIndicatorOrder());
}

void MainWindow::saveLayout()
{
    QSettings settings;
    settings.setValue("window/geometry", saveGeometry());
    settings.setValue("window/mainSplitter", ui->mainSplitter->saveState());
    settings.setValue("window/leftSplitter", ui->leftSplitter->saveState());
    settings.setValue("window/fileListHeader", ui->fileList->header()->saveState());
}

void MainWindow::updateActions()
{
    const bool hasVp = m_archive != nullptr;
    ui->actionCloseVp->setEnabled(hasVp);
    ui->actionExtractToDir->setEnabled(hasVp);
    const bool hasSelection = hasVp && ui->fileList->selectionModel()->hasSelection();
    ui->actionOpen->setEnabled(hasSelection);
    ui->actionOpenWith->setEnabled(hasSelection);
    ui->popOutButton->setEnabled(hasVp);

    // Not wired up yet
    ui->actionNewVp->setEnabled(false);
}

void MainWindow::updatePlaybackButtons()
{
    const bool playable = ui->previewArea->isPlayable();
    const bool playing = ui->previewArea->isPlaying();
    ui->playButton->setEnabled(playable);
    ui->stopButton->setEnabled(playable);
    ui->playButton->setIcon(Icons::icon(playing ? Icons::Shape::Pause : Icons::Shape::Play));
    ui->playButton->setToolTip(playing ? tr("Pause") : tr("Play"));
}

// Previews the selected file when exactly one is selected
void MainWindow::updatePreview()
{
    const std::vector<int> selected = selectedEntries();
    const int entry = selected.size() == 1 ? selected.front() : -1;
    if (entry >= 0 && entry == m_previewEntry)
        return;

    m_previewEntry = entry;
    ++m_previewRequest;
    m_previewQueued = false;

    if (entry < 0) {
        showPreview(PreviewContent::fromMessage(selected.size() > 1
            ? tr("%n files selected", nullptr, int(selected.size()))
            : tr("Select a file to preview")));
        return;
    }

    // Quick loads swap straight to the result; slow ones get a spinner
    m_previewSpinnerTimer->start();

    // If a load is running, this file goes next and anything selected in between is skipped
    if (m_previewWatcher->isRunning())
        m_previewQueued = true;
    else
        startPreviewLoad();
}

void MainWindow::startPreviewLoad()
{
    m_previewQueued = false;
    m_loadingRequest = m_previewRequest;
    const VpArchive* archive = m_archive.get();
    const int entry = m_previewEntry;
    m_previewWatcher->setFuture(QtConcurrent::run([archive, entry] {
        return PreviewLoader::load(*archive, entry);
    }));
}

void MainWindow::onPreviewLoaded()
{
    // A late signal from a replaced load; result() would block on the current one
    if (!m_previewWatcher->isFinished())
        return;

    if (m_previewQueued) {
        startPreviewLoad();
        return;
    }
    if (m_loadingRequest == m_previewRequest)
        showPreview(m_previewWatcher->result());
}

void MainWindow::showPreviewSpinner()
{
    if (!m_archive || m_previewEntry < 0)
        return;
    const QString name = m_archive->entries()[m_previewEntry].name;
    m_previewSpinnerShown = true;
    ui->previewArea->setLoading(name);
    if (previewWindowOpen())
        m_previewWindow->setLoading(name);
}

void MainWindow::showPreview(const PreviewContent& content)
{
    m_previewSpinnerTimer->stop();
    m_previewSpinnerShown = false;
    m_preview = content;
    ui->previewArea->setContent(m_preview);
    if (previewWindowOpen())
        m_previewWindow->setContent(m_preview);
}

void MainWindow::resetPreview()
{
    m_previewEntry = -1;
    ++m_previewRequest;
    m_previewQueued = false;
    showPreview(PreviewContent::fromMessage(tr("Select a file to preview")));
}

// A running load reads the archive, so it has to finish before the archive is destroyed
void MainWindow::waitForPreviewLoad()
{
    m_previewQueued = false;
    ++m_previewRequest;
    m_previewWatcher->waitForFinished();
}

// A closed pop-out is hidden and empty; it gets nothing until it opens again
bool MainWindow::previewWindowOpen() const
{
    return m_previewWindow && m_previewWindow->isVisible();
}

void MainWindow::openPreviewWindow()
{
    if (!m_previewWindow) {
        m_previewWindow = new PreviewWindow(this);
        // The window still counts as visible while it closes, so set this directly
        connect(m_previewWindow, &PreviewWindow::closed, this, [this] {
            ui->previewArea->setAutoplayMedia(OptionsDialog::autoplayMedia());
        });
    }

    // The pop-out takes over: silence the pane and keep it from autoplaying behind it
    ui->previewArea->stopMedia();
    ui->previewArea->setAutoplayMedia(false);

    if (!m_previewWindow->isVisible()) {
        if (m_previewSpinnerShown)
            m_previewWindow->setLoading(m_archive->entries()[m_previewEntry].name);
        else
            m_previewWindow->setContent(m_preview);
    }
    m_previewWindow->show();
    m_previewWindow->raise();
    m_previewWindow->activateWindow();
}

void MainWindow::updateSelectionStatus()
{
    if (!m_archive) {
        m_selectionLabel->clear();
        return;
    }

    const int shown = m_fileProxy->rowCount();
    const std::vector<int> selected = selectedEntries();
    if (selected.empty()) {
        m_selectionLabel->setText(tr("%n file(s)", nullptr, shown));
        return;
    }

    qint64 bytes = 0;
    for (const int entry : selected)
        bytes += m_archive->entries()[entry].size;
    m_selectionLabel->setText(tr("%1 of %2 selected (%3)")
        .arg(selected.size()).arg(shown).arg(QLocale().formattedDataSize(bytes)));
}

void MainWindow::showFolder(const QModelIndex& index)
{
    m_currentFolder = m_folderModel->folderAt(index);

    std::vector<int> entries;
    if (m_currentFolder == FolderTreeModel::AllFiles) {
        entries.resize(m_archive->entries().size());
        std::iota(entries.begin(), entries.end(), 0);
    } else if (m_currentFolder >= 0) {
        entries = m_archive->folders()[m_currentFolder].entries;
    }

    m_fileModel->setEntries(std::move(entries));
    ui->fileList->setColumnHidden(FileListModel::PathColumn, m_currentFolder != FolderTreeModel::AllFiles);
    updateSelectionStatus();

    // A model reset clears the selection without signaling
    updatePreview();
}

// Plain text matches anywhere in the name; * and ? match whole names; ; separates patterns
void MainWindow::onFilterChanged(const QString& text)
{
    QStringList patterns;
    for (const QString& raw : text.split(';', Qt::SkipEmptyParts)) {
        const QString part = raw.trimmed();
        if (part.isEmpty())
            continue;
        if (part.contains('*') || part.contains('?'))
            patterns << QRegularExpression::wildcardToRegularExpression(part);
        else
            patterns << QRegularExpression::escape(part);
    }
    m_fileProxy->setFilterRegularExpression(
        QRegularExpression(patterns.join('|'), QRegularExpression::CaseInsensitiveOption));
}

std::vector<int> MainWindow::selectedEntries() const
{
    std::vector<int> entries;
    for (const QModelIndex& index : ui->fileList->selectionModel()->selectedRows(FileListModel::NameColumn)) {
        const int entry = m_fileModel->entryAt(m_fileProxy->mapToSource(index));
        if (entry >= 0)
            entries.push_back(entry);
    }
    return entries;
}

// The extract actions work on the selected files, or on the whole current folder when
// nothing is selected or the folder tree has focus. Selected files land directly in the
// target; a folder is recreated there with its subfolders.
bool MainWindow::extractionSet(std::vector<int>& entries, int& baseFolder) const
{
    if (!m_archive)
        return false;

    const std::vector<int> selected = selectedEntries();
    if (!selected.empty() && !ui->folderTree->hasFocus()) {
        entries = selected;
        baseFolder = m_currentFolder >= 0 ? m_currentFolder : VpArchive::RootFolder;
    } else if (m_currentFolder >= 0) {
        entries = m_archive->entriesUnder(m_currentFolder);
        baseFolder = m_archive->folders()[m_currentFolder].parent;
    } else {
        entries = m_archive->entriesUnder(VpArchive::RootFolder);
        baseFolder = VpArchive::RootFolder;
    }

    if (entries.empty())
        statusBar()->showMessage(tr("Nothing to extract"));
    return !entries.empty();
}

void MainWindow::extractTo(const QString& target, std::vector<int> entries, int baseFolder)
{
    const QDir dir(target);
    const QString where = QDir::toNativeSeparators(target);
    auto exists = [&](int entry) { return QFileInfo::exists(dir.filePath(m_archive->entryPath(entry, baseFolder))); };

    const int existing = int(std::count_if(entries.begin(), entries.end(), exists));
    if (existing > 0) {
        QMessageBox box(QMessageBox::Question, tr("Extract"),
            tr("%n file(s) already exist in %1.", nullptr, existing).arg(where), QMessageBox::NoButton, this);
        QPushButton* overwrite = box.addButton(tr("&Overwrite"), QMessageBox::AcceptRole);
        QPushButton* skip = box.addButton(tr("&Skip Existing"), QMessageBox::AcceptRole);
        box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(overwrite);
        box.exec();

        if (box.clickedButton() == skip)
            entries.erase(std::remove_if(entries.begin(), entries.end(), exists), entries.end());
        else if (box.clickedButton() != overwrite)
            return;
        if (entries.empty()) {
            statusBar()->showMessage(tr("Nothing extracted; every file already exists in %1").arg(where));
            return;
        }
    }

    QProgressDialog progress(tr("Extracting..."), tr("Cancel"), 0, int(entries.size()), this);
    progress.setWindowTitle(tr("Extract"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(500);

    const VpArchive::ExtractResult result = m_archive->extract(entries, target, baseFolder,
        [&](int done, int, const QString& path) {
            progress.setValue(done);
            if (!path.isEmpty())
                progress.setLabelText(tr("Extracting %1").arg(QDir::toNativeSeparators(path)));
            return !progress.wasCanceled();
        });
    progress.reset();

    if (result.canceled) {
        statusBar()->showMessage(tr("Extraction canceled after %n file(s)", nullptr, result.extracted));
    } else if (!result.errors.isEmpty()) {
        QMessageBox box(QMessageBox::Warning, tr("Extract"),
            tr("Extracted %1 of %2 files to %3. %n file(s) could not be extracted.", nullptr, int(result.errors.size()))
                .arg(result.extracted).arg(entries.size()).arg(where),
            QMessageBox::Ok, this);
        box.setDetailedText(result.errors.join('\n'));
        box.exec();
    } else {
        statusBar()->showMessage(tr("Extracted %n file(s) to %1", nullptr, result.extracted).arg(where));
    }
}

// Extracts the dragged files to a temp folder so Explorer can copy real files.
// Fine for the usual handful; a huge drag blocks until it is written out.
QList<QUrl> MainWindow::extractForDrag(const std::vector<int>& entries)
{
    if (!m_archive || entries.empty())
        return {};

    if (!m_dragDir)
        m_dragDir = std::make_unique<QTemporaryDir>(QDir::temp().filePath("QtVP-XXXXXX"));
    if (!m_dragDir->isValid()) {
        statusBar()->showMessage(tr("Could not create a temporary folder for dragging: %1").arg(m_dragDir->errorString()));
        return {};
    }

    // A new folder per drag, so a file Explorer may still be copying is never rewritten
    const QString dragRoot = m_dragDir->filePath(QString::number(++m_dragCount));

    QApplication::setOverrideCursor(Qt::WaitCursor);
    QList<QUrl> urls;
    QStringList errors;
    QHash<QString, int> nameCounts;
    for (const int entry : entries) {
        const VpEntry& e = m_archive->entries()[entry];

        // Same-named files from different folders (in <All files>) each get their own subfolder
        const int seen = nameCounts[e.name.toLower()]++;
        const QString dir = seen == 0 ? dragRoot : dragRoot + QString("/~%1").arg(seen);

        const VpArchive::ExtractResult result = m_archive->extract({ entry }, dir, e.folder);
        if (result.extracted == 1)
            urls << QUrl::fromLocalFile(QDir(dir).filePath(e.name));
        else
            errors << result.errors;
    }
    QApplication::restoreOverrideCursor();

    if (!errors.isEmpty()) {
        statusBar()->showMessage(tr("Could not extract %n file(s) for dragging: %1", nullptr, int(errors.size()))
            .arg(errors.first()));
    }
    return urls;
}

void MainWindow::addRecentFile(const QString& path)
{
    const QString native = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
    QSettings settings;
    QStringList files = settings.value("recentFiles").toStringList();
    files.removeIf([&](const QString& f) { return f.compare(native, Qt::CaseInsensitive) == 0; });
    files.prepend(native);
    while (files.size() > MaxRecentFiles)
        files.removeLast();
    settings.setValue("recentFiles", files);
    updateRecentMenu();
}

void MainWindow::removeRecentFile(const QString& path)
{
    const QString native = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
    QSettings settings;
    QStringList files = settings.value("recentFiles").toStringList();
    files.removeIf([&](const QString& f) { return f.compare(native, Qt::CaseInsensitive) == 0; });
    settings.setValue("recentFiles", files);
    updateRecentMenu();
}

void MainWindow::updateRecentMenu()
{
    m_recentMenu->clear();
    const QStringList files = QSettings().value("recentFiles").toStringList();

    // Queued, because opening rebuilds this menu and deletes the action that fired
    for (int i = 0; i < files.size(); ++i) {
        QString label = files[i];
        label.replace('&', "&&");
        if (i < 9)
            label = QString("&%1 %2").arg(i + 1).arg(label);
        QAction* action = m_recentMenu->addAction(label);
        connect(action, &QAction::triggered, this, [this, path = files[i]] { openVp(path); }, Qt::QueuedConnection);
    }

    if (!files.isEmpty()) {
        m_recentMenu->addSeparator();
        QAction* clear = m_recentMenu->addAction(tr("&Clear List"));
        connect(clear, &QAction::triggered, this, [this] {
            QSettings().remove("recentFiles");
            updateRecentMenu();
        }, Qt::QueuedConnection);
    }
    m_recentMenu->setEnabled(!files.isEmpty());
}

void MainWindow::onLoadVp()
{
    QSettings settings;
    const QString startDir = settings.value("paths/lastVpDir").toString();
    const QString path = QFileDialog::getOpenFileName(this, tr("Load VP"), startDir,
        tr("VP archives (*.vp *.vpc);;All files (*)"));
    if (path.isEmpty())
        return;

    settings.setValue("paths/lastVpDir", QFileInfo(path).absolutePath());
    openVp(path);
}

void MainWindow::onExtractToDir()
{
    std::vector<int> entries;
    int baseFolder = VpArchive::RootFolder;
    if (!extractionSet(entries, baseFolder))
        return;

    QSettings settings;
    const QString target = QFileDialog::getExistingDirectory(this,
        tr("Extract %n File(s) to Folder", nullptr, int(entries.size())),
        settings.value("paths/lastExtractDir").toString());
    if (target.isEmpty())
        return;

    settings.setValue("paths/lastExtractDir", target);
    extractTo(target, std::move(entries), baseFolder);
}

void MainWindow::onAbout()
{
    QMessageBox::about(this, tr("About QtVP"),
        tr("<h3>QtVP</h3>"
           "<p>A VP archive viewer and extractor for FreeSpace Open.</p>"
           "<p>Licensed under the GNU GPL v3.</p>"));
}
