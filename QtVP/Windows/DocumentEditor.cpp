#include "DocumentEditor.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QUndoStack>
#include <QWidget>

#include <algorithm>
#include <tuple>

#include "Core/VpDocument.h"
#include "Core/VpWriter.h"

namespace {

constexpr int MaxNameLength = 31;

// The edit has already been made when the command is pushed, so the redo() that
// QUndoStack::push() calls straight away is skipped
class DocumentCommand : public QUndoCommand
{
public:
    DocumentCommand(const QString& text, std::function<void()> undo, std::function<void()> redo)
        : QUndoCommand(text)
        , m_undo(std::move(undo))
        , m_redo(std::move(redo))
    {
    }

    void undo() override { m_undo(); }

    void redo() override
    {
        if (m_applied)
            m_applied = false;
        else
            m_redo();
    }

private:
    std::function<void()> m_undo;
    std::function<void()> m_redo;
    bool m_applied = true;
};

quint32 fileTime(const QFileInfo& info)
{
    const qint64 seconds = info.lastModified().toSecsSinceEpoch();
    return seconds > 0 ? quint32(seconds) : 0;
}

} // namespace

DocumentEditor::DocumentEditor(QWidget* dialogParent, QObject* parent)
    : QObject(parent)
    , m_dialogParent(dialogParent)
    , m_undo(new QUndoStack(this))
{
    connect(m_undo, &QUndoStack::indexChanged, this, &DocumentEditor::documentChanged);
}

void DocumentEditor::setDocument(VpDocument* document)
{
    // The history refers to the old document, so it has to go first
    m_undo->clear();
    m_document = document;
}

bool DocumentEditor::isModified() const
{
    return !m_undo->isClean();
}

void DocumentEditor::markSaved()
{
    m_undo->setClean();
}

void DocumentEditor::push(const QString& text, std::function<void()> undo, std::function<void()> redo)
{
    m_undo->push(new DocumentCommand(text, std::move(undo), std::move(redo)));
}

bool DocumentEditor::addPaths(int folder, const QStringList& paths)
{
    if (!m_document || paths.isEmpty())
        return false;
    VpDocument& doc = *m_document;

    // First count the names already taken, so there is one question for all of them
    int clashes = 0;
    std::function<void(int, const QString&)> scan = [&](int target, const QString& path) {
        const QFileInfo info(path);
        if (info.isDir()) {
            const int existing = target < 0 ? -1 : doc.findFolder(target, info.fileName());
            for (const QFileInfo& child : QDir(path).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden))
                scan(existing, child.absoluteFilePath());
        } else if (target >= 0 && doc.findFile(target, info.fileName()) >= 0) {
            ++clashes;
        }
    };
    for (const QString& path : paths)
        scan(folder, path);

    bool replace = false;
    if (clashes > 0) {
        QMessageBox box(QMessageBox::Question, tr("Add"),
            tr("%n file(s) with the same name are already in the VP.", nullptr, clashes),
            QMessageBox::NoButton, m_dialogParent);
        QPushButton* replaceButton = box.addButton(tr("&Replace"), QMessageBox::AcceptRole);
        QPushButton* skipButton = box.addButton(tr("&Skip"), QMessageBox::AcceptRole);
        box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(replaceButton);
        box.exec();
        if (box.clickedButton() == replaceButton)
            replace = true;
        else if (box.clickedButton() != skipButton)
            return false;
    }

    // Then apply, recording what changed so it can be undone
    struct Replaced
    {
        int file;
        VpFileSource oldSource, newSource;
        qint64 oldSize, newSize;
        quint32 oldTime, newTime;
    };
    std::vector<int> newFiles;
    std::vector<int> newFolders;
    std::vector<Replaced> replaced;
    QStringList empty;
    QStringList longNames;

    std::function<void(int, const QString&)> apply = [&](int target, const QString& path) {
        const QFileInfo info(path);
        const QString name = info.fileName();
        if (name.size() > MaxNameLength)
            longNames << name;

        if (info.isDir()) {
            int sub = doc.findFolder(target, name);
            if (sub < 0) {
                sub = doc.addFolder(target, name);
                newFolders.push_back(sub);
            }
            for (const QFileInfo& child : QDir(path).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden))
                apply(sub, child.absoluteFilePath());
            return;
        }

        // A size 0 entry is a folder to the engine, so empty files cannot go in
        if (info.size() == 0) {
            empty << QDir::toNativeSeparators(path);
            return;
        }

        const VpFileSource source = VpFileSource::fromDisk(info.absoluteFilePath());
        const int existing = doc.findFile(target, name);
        if (existing < 0) {
            newFiles.push_back(doc.addFile(target, name, source, info.size(), fileTime(info)));
        } else if (replace) {
            const VpDocFile& old = doc.files()[existing];
            replaced.push_back({ existing, old.source, source, old.size, info.size(), old.timestamp, fileTime(info) });
            doc.setFileSource(existing, source, info.size(), fileTime(info));
        }
    };
    for (const QString& path : paths)
        apply(folder, path);

    const int changed = int(newFiles.size() + replaced.size());
    if (changed > 0 || !newFolders.empty()) {
        push(changed > 0 ? tr("Add %n file(s)", nullptr, changed) : tr("Add %n folder(s)", nullptr, int(newFolders.size())),
            [this, newFiles, newFolders, replaced] {
                for (const int file : newFiles)
                    m_document->setFileRemoved(file, true);
                for (auto it = newFolders.rbegin(); it != newFolders.rend(); ++it)
                    m_document->setFolderRemoved(*it, true);
                for (const Replaced& r : replaced)
                    m_document->setFileSource(r.file, r.oldSource, r.oldSize, r.oldTime);
            },
            [this, newFiles, newFolders, replaced] {
                for (const int folder : newFolders)
                    m_document->setFolderRemoved(folder, false);
                for (const int file : newFiles)
                    m_document->setFileRemoved(file, false);
                for (const Replaced& r : replaced)
                    m_document->setFileSource(r.file, r.newSource, r.newSize, r.newTime);
            });
    }

    if (!empty.isEmpty() || !longNames.isEmpty()) {
        QStringList notes;
        if (!empty.isEmpty())
            notes << tr("%n empty file(s) were skipped; a VP cannot hold an empty file.", nullptr, int(empty.size()));
        if (!longNames.isEmpty())
            notes << tr("%n name(s) are longer than %1 characters. Rename them before saving.", nullptr, int(longNames.size()))
                .arg(MaxNameLength);
        QMessageBox box(QMessageBox::Warning, tr("Add"), notes.join("\n\n"), QMessageBox::Ok, m_dialogParent);
        box.setDetailedText((empty + longNames).join('\n'));
        box.exec();
    }
    return true;
}

// Asks until the name is valid and free, or the user cancels (empty result)
QString DocumentEditor::askForName(const QString& title, const QString& label, const QString& current,
    const std::function<QString(const QString&)>& clash)
{
    QString name = current;
    for (;;) {
        bool ok = false;
        name = QInputDialog::getText(m_dialogParent, title, label, QLineEdit::Normal, name, &ok).trimmed();
        if (!ok || name.isEmpty())
            return QString();

        QString problem = VpWriter::nameProblem(name);
        if (problem.isEmpty())
            problem = clash(name);
        if (problem.isEmpty())
            return name;
        QMessageBox::warning(m_dialogParent, title, tr("\"%1\" %2.").arg(name, problem));
    }
}

int DocumentEditor::newFolder(int parent)
{
    if (!m_document)
        return -1;

    const QString name = askForName(tr("New Folder"), tr("Folder name:"), tr("New Folder"), [&](const QString& n) {
        return m_document->findFolder(parent, n) >= 0 ? tr("is already a folder here") : QString();
    });
    if (name.isEmpty())
        return -1;

    const int folder = m_document->addFolder(parent, name);
    push(tr("New Folder"),
        [this, folder] { m_document->setFolderRemoved(folder, true); },
        [this, folder] { m_document->setFolderRemoved(folder, false); });
    return folder;
}

bool DocumentEditor::renameFile(int file)
{
    if (!m_document)
        return false;

    const VpDocFile& f = m_document->files()[file];
    const QString oldName = f.name;
    const int folder = f.folder;
    const QString name = askForName(tr("Rename"), tr("New name:"), oldName, [&](const QString& n) {
        const int other = m_document->findFile(folder, n);
        return other >= 0 && other != file ? tr("is already a file in this folder") : QString();
    });
    if (name.isEmpty() || name == oldName)
        return false;

    m_document->renameFile(file, name);
    push(tr("Rename %1").arg(oldName),
        [this, file, oldName] { m_document->renameFile(file, oldName); },
        [this, file, name] { m_document->renameFile(file, name); });
    return true;
}

bool DocumentEditor::renameFolder(int folder)
{
    if (!m_document || folder == VpDocument::RootFolder)
        return false;

    const VpDocFolder& f = m_document->folders()[folder];
    const QString oldName = f.name;
    const int parent = f.parent;
    const QString name = askForName(tr("Rename Folder"), tr("New name:"), oldName, [&](const QString& n) {
        const int other = m_document->findFolder(parent, n);
        return other >= 0 && other != folder ? tr("is already a folder here") : QString();
    });
    if (name.isEmpty() || name == oldName)
        return false;

    m_document->renameFolder(folder, name);
    push(tr("Rename %1").arg(oldName),
        [this, folder, oldName] { m_document->renameFolder(folder, oldName); },
        [this, folder, name] { m_document->renameFolder(folder, name); });
    return true;
}

void DocumentEditor::remove(const std::vector<int>& files, const std::vector<int>& folders)
{
    if (!m_document || (files.empty() && folders.empty()))
        return;

    std::vector<int> removedFolders;
    for (const int folder : folders) {
        if (folder != VpDocument::RootFolder)
            removedFolders.push_back(folder);
    }

    for (const int file : files)
        m_document->setFileRemoved(file, true);
    for (const int folder : removedFolders)
        m_document->setFolderRemoved(folder, true);

    const QString text = removedFolders.empty()
        ? tr("Delete %n file(s)", nullptr, int(files.size()))
        : tr("Delete %n folder(s)", nullptr, int(removedFolders.size()));
    push(text,
        [this, files, removedFolders] {
            for (const int folder : removedFolders)
                m_document->setFolderRemoved(folder, false);
            for (const int file : files)
                m_document->setFileRemoved(file, false);
        },
        [this, files, removedFolders] {
            for (const int file : files)
                m_document->setFileRemoved(file, true);
            for (const int folder : removedFolders)
                m_document->setFolderRemoved(folder, true);
        });
}

void DocumentEditor::moveFiles(const std::vector<int>& files, int folder)
{
    if (!m_document)
        return;

    // A file whose name is already taken in the target stays where it is
    std::vector<std::pair<int, int>> moved;     // file, old folder
    QStringList clashes;
    for (const int file : files) {
        const VpDocFile& f = m_document->files()[file];
        if (f.folder == folder)
            continue;
        if (m_document->findFile(folder, f.name) >= 0) {
            clashes << f.name;
            continue;
        }
        moved.emplace_back(file, f.folder);
        m_document->moveFile(file, folder);
    }

    if (!moved.empty()) {
        push(tr("Move %n file(s)", nullptr, int(moved.size())),
            [this, moved] {
                for (const auto& [file, oldFolder] : moved)
                    m_document->moveFile(file, oldFolder);
            },
            [this, moved, folder] {
                for (const auto& [file, oldFolder] : moved)
                    m_document->moveFile(file, folder);
            });
    }

    if (!clashes.isEmpty()) {
        QMessageBox box(QMessageBox::Warning, tr("Move"),
            tr("%n file(s) were not moved because the folder already has a file with that name.", nullptr, int(clashes.size())),
            QMessageBox::Ok, m_dialogParent);
        box.setDetailedText(clashes.join('\n'));
        box.exec();
    }
}

void DocumentEditor::replaceContents(const std::map<int, QString>& files)
{
    if (!m_document || files.empty())
        return;

    struct Change
    {
        int file;
        VpFileSource oldSource, newSource;
        qint64 oldSize, newSize;
        quint32 oldTime, newTime;
    };
    std::vector<Change> changes;
    for (const auto& [file, path] : files) {
        const QFileInfo info(path);
        const VpDocFile& f = m_document->files()[file];
        const VpFileSource source = VpFileSource::fromDisk(info.absoluteFilePath());
        changes.push_back({ file, f.source, source, f.size, info.size(), f.timestamp, fileTime(info) });
        m_document->setFileSource(file, source, info.size(), fileTime(info));
    }

    push(tr("Pack %n edited file(s)", nullptr, int(changes.size())),
        [this, changes] {
            for (const Change& c : changes)
                m_document->setFileSource(c.file, c.oldSource, c.oldSize, c.oldTime);
        },
        [this, changes] {
            for (const Change& c : changes)
                m_document->setFileSource(c.file, c.newSource, c.newSize, c.newTime);
        });
}
