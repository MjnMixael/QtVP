#include "MainWindow.h"
#include "ui_MainWindow.h"

#include "Icons.h"
#include "Core/VpDocument.h"
#include "Core/VpWriter.h"
#include "Models/FileListModel.h"
#include "Models/FolderTreeModel.h"
#include "Previews/PreviewLoader.h"
#include "Previews/PreviewWidget.h"
#include "Windows/DocumentEditor.h"
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
#include <QSignalBlocker>
#include <QSortFilterProxyModel>
#include <QStyle>
#include <QTemporaryDir>
#include <QTimer>
#include <QUndoStack>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>

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

    // Undo and redo are the only Edit menu items; the edits themselves are on right-click
    m_editor = new DocumentEditor(this, this);
    QAction* undo = m_editor->undoStack()->createUndoAction(this, tr("&Undo"));
    undo->setShortcut(QKeySequence::Undo);
    QAction* redo = m_editor->undoStack()->createRedoAction(this, tr("&Redo"));
    redo->setShortcut(QKeySequence::Redo);
    auto* editMenu = new QMenu(tr("&Edit"), this);
    editMenu->addActions({ undo, redo });
    menuBar()->insertMenu(ui->menuTools->menuAction(), editMenu);

    // Dropping on the tree or the list adds files; the filter handles every drag there
    for (QAbstractItemView* view : { static_cast<QAbstractItemView*>(ui->folderTree), static_cast<QAbstractItemView*>(ui->fileList) }) {
        view->setAcceptDrops(true);
        view->viewport()->setAcceptDrops(true);
        view->viewport()->installEventFilter(this);
    }

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
    if (!m_document || entries.empty())
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
        const VpDocFile& e = m_document->files()[entry];
        const QString dir = QDir(root).filePath(QString::number(i));

        std::vector<int> files{ entry };
        if (QFileInfo(e.name).suffix().compare("eff", Qt::CaseInsensitive) == 0) {
            const PreviewLoader::EffInfo eff = PreviewLoader::readEff(*m_document, entry);
            files.insert(files.end(), eff.frames.begin(), eff.frames.end());
            if (!eff.error.isEmpty())
                errors << tr("%1: %2").arg(e.name, eff.error);
        }

        QApplication::setOverrideCursor(Qt::WaitCursor);
        bool extracted = true;
        for (const int file : files) {
            const VpDocument::ExtractResult result = m_document->extract({ file }, dir, m_document->files()[file].folder);
            if (result.extracted != 1) {
                errors << result.errors;
                if (file == entry)
                    extracted = false;
                continue;
            }

            // Watched from now on, so edits made in the other app can be packed back in
            const QString diskPath = QDir(dir).filePath(m_document->files()[file].name);
            const QFileInfo info(diskPath);
            m_openedFiles.push_back({ file, m_document->filePath(file), diskPath, info.lastModified(), info.size() });
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

// Runs whenever QtVP becomes the active app again. Each change is asked about once;
// editing the same file again asks again.
void MainWindow::checkEditedFiles()
{
    if (!m_document || m_openedFiles.empty() || m_checkingEdits || QApplication::activeModalWidget())
        return;

    std::map<int, QString> edited;
    QStringList names;
    for (OpenedFile& opened : m_openedFiles) {
        const QFileInfo info(opened.diskPath);
        if (!info.exists() || (info.lastModified() == opened.modified && info.size() == opened.size))
            continue;
        opened.modified = info.lastModified();
        opened.size = info.size();

        // Deleted from the VP since, or emptied, which a VP cannot hold
        const VpDocFile& f = m_document->files()[opened.file];
        if (f.removed || !m_document->isFolderLive(f.folder) || info.size() == 0)
            continue;
        if (edited.emplace(opened.file, opened.diskPath).second)
            names << QDir::toNativeSeparators(m_document->filePath(opened.file));
    }
    if (edited.empty())
        return;

    m_checkingEdits = true;
    QMessageBox box(QMessageBox::Question, tr("Edited Files"),
        tr("One or more files were edited. Pack the edits back into this VP?"),
        QMessageBox::Yes | QMessageBox::No, this);
    box.setDetailedText(names.join('\n'));
    const bool pack = box.exec() == QMessageBox::Yes;
    m_checkingEdits = false;

    if (pack) {
        m_editor->replaceContents(edited);
        statusBar()->showMessage(tr("Packed %n edited file(s). Save to keep them.", nullptr, int(edited.size())));
    }
}

MainWindow::~MainWindow()
{
    // The models and the editor outlive this destructor as child objects; keep them off the document
    waitForPreviewLoad();
    m_editor->disconnect(this);
    m_editor->setDocument(nullptr);
    m_folderModel->setDocument(nullptr);
    m_fileModel->setDocument(nullptr);
    delete ui;
}

void MainWindow::openVp(const QString& path)
{
    if (maybeSave())
        loadVp(path);
}

void MainWindow::closeVp()
{
    if (!maybeSave())
        return;
    setDocument(nullptr);
    statusBar()->showMessage(tr("Ready"));
}

// A new VP starts with a data folder, since the engine only loads files inside one
void MainWindow::newVp()
{
    if (!maybeSave())
        return;

    auto document = std::make_unique<VpDocument>();
    const int data = document->addFolder(VpDocument::RootFolder, QStringLiteral("data"));
    setDocument(std::move(document));
    ui->folderTree->setCurrentIndex(m_folderModel->indexOf(data));
    statusBar()->showMessage(tr("New VP. Add files or folders, then save."));
}

// Opens without asking about unsaved changes; saving uses this to reload the new file
void MainWindow::loadVp(const QString& path)
{
    auto archive = std::make_unique<VpArchive>();
    if (!archive->open(path)) {
        QMessageBox::warning(this, tr("Load VP"), tr("Could not open %1.\n\n%2")
            .arg(QDir::toNativeSeparators(path), archive->errorString()));
        if (!QFileInfo::exists(path))
            removeRecentFile(path);
        return;
    }

    const QStringList warnings = archive->warnings();
    setDocument(std::make_unique<VpDocument>(std::move(archive)));
    addRecentFile(path);

    QString status = tr("Opened %1").arg(QDir::toNativeSeparators(path));
    if (!warnings.isEmpty())
        status += tr(". Warning: %1").arg(warnings.join(' '));
    statusBar()->showMessage(status);
}

// Swaps in a document, or none, with a fresh history, showing <All files>
void MainWindow::setDocument(std::unique_ptr<VpDocument> document)
{
    // Detach everything and finish any preview load before the old document is destroyed.
    // A playing movie streams from the archive, so the preview goes first.
    waitForPreviewLoad();
    resetPreview();
    {
        const QSignalBlocker blocker(m_editor);
        m_editor->setDocument(nullptr);
    }
    m_folderModel->setDocument(nullptr);
    m_fileModel->setDocument(nullptr);
    m_document = std::move(document);
    m_currentFolder = FolderTreeModel::NoFolder;

    // Edits to files opened from another VP cannot be packed into this one
    m_openedFiles.clear();

    {
        const QSignalBlocker blocker(m_editor);
        m_editor->setDocument(m_document.get());
    }
    m_folderModel->setDocument(m_document.get());
    m_fileModel->setDocument(m_document.get());
    if (m_document) {
        ui->folderTree->expandToDepth(0);
        ui->folderTree->setCurrentIndex(m_folderModel->allFilesIndex());
    }

    resetPreview();
    updateTitle();
    updateSelectionStatus();
    updateActions();
}

void MainWindow::updateTitle()
{
    if (!m_document) {
        setWindowTitle(tr("QtVP"));
        setWindowModified(false);
        return;
    }
    const QString name = m_document->path().isEmpty() ? tr("Untitled") : QFileInfo(m_document->path()).fileName();
    setWindowTitle(tr("QtVP - %1[*]").arg(name));
    setWindowModified(m_editor->isModified());
}

// True if it is fine to drop the current document: nothing unsaved, or the user saved or discarded it
bool MainWindow::maybeSave()
{
    if (!m_document || !m_editor->isModified())
        return true;

    const QString name = m_document->path().isEmpty() ? tr("Untitled") : QFileInfo(m_document->path()).fileName();
    const auto answer = QMessageBox::warning(this, tr("QtVP"), tr("%1 has unsaved changes. Save them?").arg(name),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Save)
        return save();
    return answer == QMessageBox::Discard;
}

bool MainWindow::save()
{
    if (!m_document)
        return false;
    return m_document->path().isEmpty() ? saveAs() : saveTo(m_document->path());
}

bool MainWindow::saveAs()
{
    if (!m_document)
        return false;

    QSettings settings;
    const QString start = m_document->path().isEmpty()
        ? QDir(settings.value("paths/lastVpDir").toString()).filePath("untitled.vp")
        : m_document->path();
    QString path = QFileDialog::getSaveFileName(this, tr("Save VP As"), start, tr("VP archives (*.vp)"));
    if (path.isEmpty())
        return false;
    if (QFileInfo(path).suffix().isEmpty())
        path += QStringLiteral(".vp");

    settings.setValue("paths/lastVpDir", QFileInfo(path).absolutePath());
    return saveTo(path);
}

namespace {

// The folder at a '/' path such as "data/tables", or -1
int folderByPath(const VpDocument& document, const QString& path)
{
    int folder = VpDocument::RootFolder;
    for (const QString& part : path.split('/', Qt::SkipEmptyParts)) {
        folder = document.findFolder(folder, part);
        if (folder < 0)
            return -1;
    }
    return folder;
}

} // namespace

// Writes to a temporary file beside the target and swaps it in only when everything was
// written, so a failed save leaves the old VP as it was. The VP then reloads from the new
// file, which also clears the undo history.
bool MainWindow::saveTo(const QString& path)
{
    const VpWriter::Problems problems = VpWriter::check(*m_document);
    if (!problems.errors.isEmpty()) {
        QMessageBox box(QMessageBox::Warning, tr("Save"),
            tr("The VP cannot be saved until %n problem(s) are fixed.", nullptr, int(problems.errors.size())),
            QMessageBox::Ok, this);
        box.setDetailedText(problems.errors.join('\n'));
        box.exec();
        return false;
    }
    if (!problems.warnings.isEmpty()) {
        QMessageBox box(QMessageBox::Question, tr("Save"),
            tr("%n thing(s) may not work as expected in the game. Save anyway?", nullptr, int(problems.warnings.size())),
            QMessageBox::Save | QMessageBox::Cancel, this);
        box.setDetailedText(problems.warnings.join('\n'));
        if (box.exec() != QMessageBox::Save)
            return false;
    }

    waitForPreviewLoad();
    const QString folderPath = m_currentFolder > VpDocument::RootFolder ? m_document->folderPath(m_currentFolder) : QString();
    const bool sameFile = m_document->archive() && QFileInfo(m_document->path()) == QFileInfo(path);

    QProgressDialog progress(tr("Saving..."), tr("Cancel"), 0, m_document->fileCount(), this);
    progress.setWindowTitle(tr("Save"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(500);

    VpWriter writer(path);
    const bool written = writer.write(*m_document, [&](int done, int, const QString& file) {
        progress.setValue(done);
        if (!file.isEmpty())
            progress.setLabelText(tr("Saving %1").arg(QDir::toNativeSeparators(file)));
        return !progress.wasCanceled();
    });
    progress.reset();

    if (!written) {
        if (writer.wasCanceled())
            statusBar()->showMessage(tr("Save canceled"));
        else
            QMessageBox::warning(this, tr("Save"), writer.errorString());
        return false;
    }

    // Windows will not replace a file that is open, so let go of the VP being saved over,
    // after stopping any movie that streams from it
    if (sameFile) {
        resetPreview();
        m_document->archive()->close();
    }

    if (!writer.commit()) {
        // The old VP is untouched; reopen it so the unsaved edits can still read from it
        if (sameFile)
            m_document->archive()->open(path);
        QMessageBox::warning(this, tr("Save"), writer.errorString());
        return false;
    }

    std::vector<OpenedFile> opened = std::move(m_openedFiles);
    loadVp(path);

    // Files still open in other apps now belong to the reloaded VP
    if (m_document) {
        for (OpenedFile& o : opened) {
            const qsizetype slash = o.vpPath.lastIndexOf('/');
            const int folder = folderByPath(*m_document, slash < 0 ? QString() : o.vpPath.left(slash));
            o.file = folder < 0 ? -1 : m_document->findFile(folder, o.vpPath.mid(slash + 1));
            if (o.file >= 0)
                m_openedFiles.push_back(o);
        }
    }

    // Back to the folder the user was in
    if (m_document && !folderPath.isEmpty()) {
        const QModelIndex index = m_folderModel->indexOf(folderByPath(*m_document, folderPath));
        if (index.isValid()) {
            ui->folderTree->setCurrentIndex(index);
            ui->folderTree->scrollTo(index);
        }
    }
    statusBar()->showMessage(tr("Saved %1").arg(QDir::toNativeSeparators(path)));
    return true;
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (!maybeSave()) {
        event->ignore();
        return;
    }

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

    // Right-click menus. Rename and Delete act on the files when the list has focus and on
    // the folder when the tree does, so their F2 and Del shortcuts work in both.
    auto separator = [this] {
        auto* action = new QAction(this);
        action->setSeparator(true);
        return action;
    };
    ui->fileList->setContextMenuPolicy(Qt::ActionsContextMenu);
    ui->fileList->addActions({ ui->actionOpen, ui->actionOpenWith, separator(), ui->actionExtractToDir, separator(),
        ui->actionAddFiles, ui->actionAddFolder, ui->actionNewFolder, separator(), ui->actionRename, ui->actionDelete });
    ui->folderTree->setContextMenuPolicy(Qt::ActionsContextMenu);
    ui->folderTree->addActions({ ui->actionExtractToDir, separator(),
        ui->actionAddFiles, ui->actionAddFolder, ui->actionNewFolder, separator(), ui->actionRename, ui->actionDelete });
}

void MainWindow::setupConnections()
{
    connect(ui->actionLoadVp, &QAction::triggered, this, &MainWindow::onLoadVp);
    connect(ui->actionNewVp, &QAction::triggered, this, &MainWindow::newVp);
    connect(ui->actionSave, &QAction::triggered, this, [this] { save(); });
    connect(ui->actionSaveAs, &QAction::triggered, this, [this] { saveAs(); });
    connect(ui->actionCloseVp, &QAction::triggered, this, &MainWindow::closeVp);
    connect(ui->actionAddFiles, &QAction::triggered, this, &MainWindow::onAddFiles);
    connect(ui->actionAddFolder, &QAction::triggered, this, &MainWindow::onAddFolder);
    connect(ui->actionNewFolder, &QAction::triggered, this, &MainWindow::onNewFolder);
    connect(ui->actionRename, &QAction::triggered, this, &MainWindow::onRename);
    connect(ui->actionDelete, &QAction::triggered, this, &MainWindow::onDelete);
    connect(m_editor, &DocumentEditor::documentChanged, this, &MainWindow::refreshAfterEdit);

    // Coming back from another app is when edits made there get offered for packing
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state == Qt::ApplicationActive)
            QTimer::singleShot(0, this, &MainWindow::checkEditedFiles);
    });
    connect(m_editor->undoStack(), &QUndoStack::cleanChanged, this, &MainWindow::updateTitle);
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
    const bool hasVp = m_document != nullptr;
    ui->actionCloseVp->setEnabled(hasVp);
    ui->actionExtractToDir->setEnabled(hasVp);
    const bool hasSelection = hasVp && ui->fileList->selectionModel()->hasSelection();
    ui->actionOpen->setEnabled(hasSelection);
    ui->actionOpenWith->setEnabled(hasSelection);
    ui->popOutButton->setEnabled(hasVp);

    for (QAction* action : { ui->actionSave, ui->actionSaveAs, ui->actionAddFiles, ui->actionAddFolder,
             ui->actionNewFolder, ui->actionRename, ui->actionDelete })
        action->setEnabled(hasVp);
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
    // Gathered here so the worker never reads the document while it is being edited
    const PreviewLoader::Request request = PreviewLoader::request(*m_document, m_previewEntry);
    m_previewWatcher->setFuture(QtConcurrent::run([request] {
        return PreviewLoader::load(request);
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
    if (!m_document || m_previewEntry < 0)
        return;
    const QString name = m_document->files()[m_previewEntry].name;
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

// A running load reads through the document's archive, so it has to finish before the document is destroyed
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
            m_previewWindow->setLoading(m_document->files()[m_previewEntry].name);
        else
            m_previewWindow->setContent(m_preview);
    }
    m_previewWindow->show();
    m_previewWindow->raise();
    m_previewWindow->activateWindow();
}

void MainWindow::updateSelectionStatus()
{
    if (!m_document) {
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
        bytes += m_document->files()[entry].size;
    m_selectionLabel->setText(tr("%1 of %2 selected (%3)")
        .arg(selected.size()).arg(shown).arg(QLocale().formattedDataSize(bytes)));
}

void MainWindow::showFolder(const QModelIndex& index)
{
    m_currentFolder = m_folderModel->folderAt(index);

    std::vector<int> entries;
    if (m_currentFolder == FolderTreeModel::AllFiles) {
        entries = m_document->filesUnder(VpDocument::RootFolder);
    } else if (m_currentFolder >= 0) {
        entries = m_document->folders()[m_currentFolder].files;
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
    if (!m_document)
        return false;

    const std::vector<int> selected = selectedEntries();
    if (!selected.empty() && !ui->folderTree->hasFocus()) {
        entries = selected;
        baseFolder = m_currentFolder >= 0 ? m_currentFolder : VpDocument::RootFolder;
    } else if (m_currentFolder >= 0) {
        entries = m_document->filesUnder(m_currentFolder);
        baseFolder = m_document->folders()[m_currentFolder].parent;
    } else {
        entries = m_document->filesUnder(VpDocument::RootFolder);
        baseFolder = VpDocument::RootFolder;
    }

    if (entries.empty())
        statusBar()->showMessage(tr("Nothing to extract"));
    return !entries.empty();
}

void MainWindow::extractTo(const QString& target, std::vector<int> entries, int baseFolder)
{
    const QDir dir(target);
    const QString where = QDir::toNativeSeparators(target);
    auto exists = [&](int entry) { return QFileInfo::exists(dir.filePath(m_document->filePath(entry, baseFolder))); };

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

    const VpDocument::ExtractResult result = m_document->extract(entries, target, baseFolder,
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
    if (!m_document || entries.empty())
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
        const VpDocFile& e = m_document->files()[entry];

        // Same-named files from different folders (in <All files>) each get their own subfolder
        const int seen = nameCounts[e.name.toLower()]++;
        const QString dir = seen == 0 ? dragRoot : dragRoot + QString("/~%1").arg(seen);

        const VpDocument::ExtractResult result = m_document->extract({ entry }, dir, e.folder);
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
    int baseFolder = VpDocument::RootFolder;
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

// Where added files and new folders go: the folder shown, or the top level for <All files>
int MainWindow::editFolder() const
{
    return m_currentFolder >= 0 ? m_currentFolder : VpDocument::RootFolder;
}

void MainWindow::onAddFiles()
{
    if (!m_document)
        return;

    QSettings settings;
    const QStringList paths = QFileDialog::getOpenFileNames(this, tr("Add Files"),
        settings.value("paths/lastAddDir").toString());
    if (paths.isEmpty())
        return;

    settings.setValue("paths/lastAddDir", QFileInfo(paths.first()).absolutePath());
    m_editor->addPaths(editFolder(), paths);
}

void MainWindow::onAddFolder()
{
    if (!m_document)
        return;

    QSettings settings;
    const QString path = QFileDialog::getExistingDirectory(this, tr("Add Folder"),
        settings.value("paths/lastAddDir").toString());
    if (path.isEmpty())
        return;

    settings.setValue("paths/lastAddDir", QFileInfo(path).absolutePath());
    m_editor->addPaths(editFolder(), { path });
}

void MainWindow::onNewFolder()
{
    if (!m_document)
        return;

    const int folder = m_editor->newFolder(editFolder());
    const QModelIndex index = m_folderModel->indexOf(folder);
    if (index.isValid()) {
        ui->folderTree->setCurrentIndex(index);
        ui->folderTree->scrollTo(index);
    }
}

// The folder when the tree has focus, otherwise the one selected file
void MainWindow::onRename()
{
    if (!m_document)
        return;

    if (ui->folderTree->hasFocus()) {
        if (m_currentFolder > VpDocument::RootFolder)
            m_editor->renameFolder(m_currentFolder);
        return;
    }

    const std::vector<int> selected = selectedEntries();
    if (selected.size() == 1)
        m_editor->renameFile(selected.front());
    else
        statusBar()->showMessage(tr("Select one file to rename"));
}

// The folder when the tree has focus, otherwise the selected files. Undo brings them back.
void MainWindow::onDelete()
{
    if (!m_document)
        return;

    if (ui->folderTree->hasFocus()) {
        if (m_currentFolder > VpDocument::RootFolder)
            m_editor->remove({}, { m_currentFolder });
        return;
    }

    const std::vector<int> selected = selectedEntries();
    if (!selected.empty())
        m_editor->remove(selected, {});
}

// Rebuilds the tree and list after an edit, undo, or redo, keeping the expanded folders,
// the folder shown, and the selection wherever they still exist
void MainWindow::refreshAfterEdit()
{
    if (!m_document)
        return;

    std::vector<int> expanded;
    collectExpanded(QModelIndex(), expanded);
    const int folder = m_currentFolder;
    const std::vector<int> selected = selectedEntries();
    const int current = m_fileModel->entryAt(m_fileProxy->mapToSource(ui->fileList->currentIndex()));

    m_folderModel->setDocument(m_document.get());
    for (const int id : expanded) {
        const QModelIndex index = m_folderModel->indexOf(id);
        if (index.isValid())
            ui->folderTree->expand(index);
    }

    // A reset leaves no current index, so setting one always reloads the list
    QModelIndex index = folder == FolderTreeModel::AllFiles ? m_folderModel->allFilesIndex() : m_folderModel->indexOf(folder);
    if (!index.isValid())
        index = m_folderModel->allFilesIndex();
    ui->folderTree->setCurrentIndex(index);

    selectFiles(selected, current);
    updateActions();
}

void MainWindow::collectExpanded(const QModelIndex& parent, std::vector<int>& folders) const
{
    for (int row = 0; row < m_folderModel->rowCount(parent); ++row) {
        const QModelIndex index = m_folderModel->index(row, 0, parent);
        if (ui->folderTree->isExpanded(index)) {
            folders.push_back(m_folderModel->folderAt(index));
            collectExpanded(index, folders);
        }
    }
}

void MainWindow::selectFiles(const std::vector<int>& files, int current)
{
    const QHash<int, int> rows = m_fileModel->rowLookup();
    const int lastColumn = FileListModel::ColumnCount - 1;

    QItemSelection selection;
    for (const int file : files) {
        const auto row = rows.constFind(file);
        if (row == rows.constEnd())
            continue;
        const QModelIndex first = m_fileProxy->mapFromSource(m_fileModel->index(row.value(), 0));
        if (first.isValid())
            selection.select(first, first.siblingAtColumn(lastColumn));
    }
    ui->fileList->selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect);

    const auto row = rows.constFind(current);
    if (row != rows.constEnd()) {
        const QModelIndex index = m_fileProxy->mapFromSource(m_fileModel->index(row.value(), 0));
        ui->fileList->selectionModel()->setCurrentIndex(index, QItemSelectionModel::NoUpdate);
    }
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    const bool onTree = watched == ui->folderTree->viewport();
    const bool onList = watched == ui->fileList->viewport();
    if (onTree || onList) {
        switch (event->type()) {
        case QEvent::DragEnter:
        case QEvent::DragMove:
            handleViewDrag(onTree, static_cast<QDropEvent*>(event), false);
            return true;
        case QEvent::Drop:
            handleViewDrag(onTree, static_cast<QDropEvent*>(event), true);
            return true;
        default:
            break;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

// Drags over the folder tree or file list. Files from Explorer are added to the folder
// under the cursor (tree) or the folder shown (list); VP files can be opened instead; and
// files dragged from the list onto a folder in the tree move there.
bool MainWindow::handleViewDrag(bool onTree, QDropEvent* event, bool drop)
{
    const QPoint pos = event->position().toPoint();
    const int folderUnder = onTree ? m_folderModel->folderAt(ui->folderTree->indexAt(pos)) : FolderTreeModel::NoFolder;

    if (event->source() == ui->fileList) {
        if (!m_document || folderUnder < 0) {
            event->ignore();
            return false;
        }
        // Copy, so the list does not try to remove anything itself when the drag ends
        event->setDropAction(Qt::CopyAction);
        event->accept();
        if (drop) {
            const std::vector<int> files = selectedEntries();
            QTimer::singleShot(0, this, [this, files, folderUnder] { m_editor->moveFiles(files, folderUnder); });
        }
        return true;
    }

    QStringList paths;
    if (!event->source() && event->mimeData()->hasUrls()) {
        for (const QUrl& url : event->mimeData()->urls()) {
            if (url.isLocalFile())
                paths << url.toLocalFile();
        }
    }
    const bool allVps = !paths.isEmpty() && std::all_of(paths.cbegin(), paths.cend(), isVpPath);
    if (paths.isEmpty() || (!m_document && !allVps)) {
        event->ignore();
        return false;
    }

    event->setDropAction(Qt::CopyAction);
    event->accept();
    if (!drop)
        return true;

    const int folder = onTree ? (folderUnder >= 0 ? folderUnder : VpDocument::RootFolder) : editFolder();

    // After the drop returns, so Explorer is not held up while a question is open
    QTimer::singleShot(0, this, [this, paths, folder, allVps] {
        if (allVps) {
            if (!m_document) {
                openVp(paths.first());
                return;
            }
            QMessageBox box(QMessageBox::Question, tr("Drop"),
                tr("Open %1, or add the dropped files to this VP?").arg(QFileInfo(paths.first()).fileName()),
                QMessageBox::NoButton, this);
            QPushButton* open = box.addButton(tr("&Open"), QMessageBox::AcceptRole);
            QPushButton* add = box.addButton(tr("&Add to This VP"), QMessageBox::AcceptRole);
            box.addButton(QMessageBox::Cancel);
            box.setDefaultButton(open);
            box.exec();
            if (box.clickedButton() == open)
                openVp(paths.first());
            else if (box.clickedButton() == add && m_document)
                m_editor->addPaths(folder, paths);
            return;
        }
        if (m_document)
            m_editor->addPaths(folder, paths);
    });
    return true;
}
