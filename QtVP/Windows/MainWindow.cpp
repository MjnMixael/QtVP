#include "MainWindow.h"
#include "ui_MainWindow.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QSettings>
#include <QStyle>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // Tree and preview share the left column; the file list gets the extra width
    ui->mainSplitter->setStretchFactor(0, 0);
    ui->mainSplitter->setStretchFactor(1, 1);
    ui->mainSplitter->setSizes({ 300, 700 });
    ui->leftSplitter->setSizes({ 450, 150 });

    setupIcons();
    setupConnections();
    restoreLayout();
    updateActions();

    statusBar()->showMessage(tr("Ready"));
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::openVp(const QString& path)
{
    // VP parsing lands in the next step; for now just track the selection
    m_currentPath = path;
    setWindowTitle(tr("QtVP - %1").arg(QFileInfo(path).fileName()));
    statusBar()->showMessage(tr("Opened %1 (reading not implemented yet)").arg(QDir::toNativeSeparators(path)));
    updateActions();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    saveLayout();
    event->accept();
}

void MainWindow::setupIcons()
{
    // Placeholder platform icons until the app gets its own set
    QStyle* s = style();
    ui->actionLoadVp->setIcon(s->standardIcon(QStyle::SP_DialogOpenButton));
    ui->actionNewVp->setIcon(s->standardIcon(QStyle::SP_FileIcon));
    ui->actionExtractToDir->setIcon(s->standardIcon(QStyle::SP_DirOpenIcon));
    ui->actionExtractToDataDir->setIcon(s->standardIcon(QStyle::SP_DriveHDIcon));
    ui->actionOptions->setIcon(s->standardIcon(QStyle::SP_FileDialogDetailedView));

    ui->playButton->setIcon(s->standardIcon(QStyle::SP_MediaPlay));
    ui->stopButton->setIcon(s->standardIcon(QStyle::SP_MediaStop));
    ui->popOutButton->setIcon(s->standardIcon(QStyle::SP_TitleBarMaxButton));
}

void MainWindow::setupConnections()
{
    connect(ui->actionLoadVp, &QAction::triggered, this, &MainWindow::onLoadVp);
    connect(ui->actionExit, &QAction::triggered, this, &QWidget::close);
    connect(ui->actionAbout, &QAction::triggered, this, &MainWindow::onAbout);
    connect(ui->actionAboutQt, &QAction::triggered, qApp, &QApplication::aboutQt);
}

void MainWindow::restoreLayout()
{
    QSettings settings;
    restoreGeometry(settings.value("window/geometry").toByteArray());
    ui->mainSplitter->restoreState(settings.value("window/mainSplitter").toByteArray());
    ui->leftSplitter->restoreState(settings.value("window/leftSplitter").toByteArray());
}

void MainWindow::saveLayout()
{
    QSettings settings;
    settings.setValue("window/geometry", saveGeometry());
    settings.setValue("window/mainSplitter", ui->mainSplitter->saveState());
    settings.setValue("window/leftSplitter", ui->leftSplitter->saveState());
}

void MainWindow::updateActions()
{
    const bool hasVp = !m_currentPath.isEmpty();
    ui->actionCloseVp->setEnabled(hasVp);
    ui->actionExtractToDir->setEnabled(hasVp);

    // Not wired up yet
    ui->actionNewVp->setEnabled(false);
    ui->actionExtractToDataDir->setEnabled(false);
    ui->actionOptions->setEnabled(false);
    ui->playButton->setEnabled(false);
    ui->stopButton->setEnabled(false);
    ui->popOutButton->setEnabled(false);
}

void MainWindow::onLoadVp()
{
    QSettings settings;
    const QString startDir = settings.value("paths/lastVpDir").toString();
    const QString path = QFileDialog::getOpenFileName(this, tr("Load VP"), startDir,
        tr("VP archives (*.vp);;All files (*)"));
    if (path.isEmpty())
        return;

    settings.setValue("paths/lastVpDir", QFileInfo(path).absolutePath());
    openVp(path);
}

void MainWindow::onAbout()
{
    QMessageBox::about(this, tr("About QtVP"),
        tr("<h3>QtVP</h3>"
           "<p>A VP archive viewer and extractor for FreeSpace Open.</p>"
           "<p>Licensed under the GNU GPL v3.</p>"));
}
