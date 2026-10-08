#include "FileOpener.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QUrl>
#include <QWidget>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#pragma comment(lib, "shell32.lib")
#endif

namespace {

const char* settingKey(FileOpener::Tool tool)
{
    return tool == FileOpener::Tool::AnimStudio ? "tools/animStudio" : "tools/pofTools";
}

// An app picked with Choose Application for one extension, where the system has no chooser
QString rememberedAppKey(const QString& path)
{
    return QStringLiteral("openWith/") + QFileInfo(path).suffix().toLower();
}

} // namespace

FileOpener::Tool FileOpener::toolFor(const QString& fileName)
{
    const QString ext = QFileInfo(fileName).suffix().toLower();
    if (ext == "ani" || ext == "eff")
        return Tool::AnimStudio;
    if (ext == "pof")
        return Tool::PofTools;
    return Tool::None;
}

QString FileOpener::toolName(Tool tool)
{
    return tool == Tool::AnimStudio ? QStringLiteral("AnimStudio") : QStringLiteral("POF Tools");
}

QString FileOpener::toolPath(Tool tool)
{
    return tool == Tool::None ? QString() : QSettings().value(settingKey(tool)).toString();
}

void FileOpener::setToolPath(Tool tool, const QString& path)
{
    if (tool != Tool::None)
        QSettings().setValue(settingKey(tool), path);
}

QString FileOpener::programFilter()
{
#if defined(Q_OS_WIN)
    return tr("Programs (*.exe);;All files (*)");
#elif defined(Q_OS_MACOS)
    return tr("Applications (*.app);;All files (*)");
#else
    return tr("All files (*)");
#endif
}

FileOpener::FileOpener(QWidget* parent)
    : m_parent(parent)
{
}

void FileOpener::open(const QString& path)
{
    const Tool tool = toolFor(path);
    if (tool == Tool::None) {
        openWithSystem(path);
        return;
    }

    Choice& choice = m_choices[tool];
    if (choice == Choice::Unasked)
        choice = QFileInfo::exists(toolPath(tool)) ? Choice::Tool : askAboutTool(tool, QFileInfo(path).fileName());

    if (choice == Choice::Tool && !launch(toolPath(tool), path))
        choice = Choice::Skip;
    else if (choice == Choice::System)
        openWithSystem(path);
}

void FileOpener::openWith(const QString& path)
{
#ifdef Q_OS_WIN
    // Windows' own "How do you want to open this file?" dialog, which can also make
    // the choice permanent for the extension
    const std::wstring file = QDir::toNativeSeparators(path).toStdWString();
    OPENASINFO info = {};
    info.pcszFile = file.c_str();
    info.oaifInFlags = OAIF_ALLOW_REGISTRATION | OAIF_REGISTER_EXT | OAIF_EXEC;
    const HWND owner = m_parent ? reinterpret_cast<HWND>(m_parent->window()->winId()) : nullptr;
    SHOpenWithDialog(owner, &info);
#else
    chooseApplication(path);
#endif
}

FileOpener::Choice FileOpener::askAboutTool(Tool tool, const QString& fileName)
{
    const QString name = toolName(tool);
    const QString saved = toolPath(tool);
    const QString ext = QFileInfo(fileName).suffix().toUpper();

    QMessageBox box(QMessageBox::Question, tr("Open"),
        saved.isEmpty()
            ? tr("%1 files open in %2, but where %2 is installed has not been set.").arg(ext, name)
            : tr("%1 files open in %2, but it is no longer at %3.").arg(ext, name, QDir::toNativeSeparators(saved)),
        QMessageBox::NoButton, m_parent);
    box.setInformativeText(tr("Locate %1 now, or open the file in your system's default app for it?").arg(name));
    QPushButton* locate = box.addButton(tr("&Locate %1...").arg(name), QMessageBox::AcceptRole);
    QPushButton* system = box.addButton(tr("Use the &Default App"), QMessageBox::ActionRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(locate);
    box.exec();

    if (box.clickedButton() == system)
        return Choice::System;
    if (box.clickedButton() != locate)
        return Choice::Skip;

    const QString program = QFileDialog::getOpenFileName(m_parent, tr("Locate %1").arg(name),
        QFileInfo(saved).absolutePath(), programFilter());
    if (program.isEmpty())
        return Choice::Skip;
    setToolPath(tool, program);
    return Choice::Tool;
}

bool FileOpener::launch(const QString& program, const QString& path)
{
    const QString file = QDir::toNativeSeparators(path);
    bool started = false;
#ifdef Q_OS_MACOS
    // An app bundle is a folder; open starts it with the file
    if (program.endsWith(".app", Qt::CaseInsensitive))
        started = QProcess::startDetached("open", { "-a", program, file });
    else
#endif
        started = QProcess::startDetached(program, { file }, QFileInfo(program).absolutePath());

    if (!started) {
        QMessageBox::warning(m_parent, tr("Open"), tr("Could not start %1.")
            .arg(QDir::toNativeSeparators(program)));
    }
    return started;
}

// Falls back to choosing an app when nothing is associated with the extension
void FileOpener::openWithSystem(const QString& path)
{
#ifndef Q_OS_WIN
    const QString remembered = QSettings().value(rememberedAppKey(path)).toString();
    if (!remembered.isEmpty() && QFileInfo::exists(remembered)) {
        launch(remembered, path);
        return;
    }
#endif
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path)))
        openWith(path);
}

// Where the system has no Open With dialog: pick a program, optionally for good
void FileOpener::chooseApplication(const QString& path)
{
    const QString name = QFileInfo(path).fileName();
    const QString program = QFileDialog::getOpenFileName(m_parent, tr("Choose an Application to Open %1").arg(name),
        QString(), programFilter());
    if (program.isEmpty())
        return;

    const QString ext = QFileInfo(path).suffix().toLower();
    if (!ext.isEmpty()) {
        const auto answer = QMessageBox::question(m_parent, tr("Open"),
            tr("Always open .%1 files with %2?").arg(ext, QFileInfo(program).completeBaseName()));
        if (answer == QMessageBox::Yes)
            QSettings().setValue(rememberedAppKey(path), program);
    }
    launch(program, path);
}
