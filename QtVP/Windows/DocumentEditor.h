#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <map>
#include <vector>

class QUndoStack;
class QWidget;
class VpDocument;

// Makes undoable edits to a VpDocument, asking the user where an edit needs a decision.
// Every edit is applied right away and recorded as an undo/redo pair on the stack.
class DocumentEditor : public QObject
{
    Q_OBJECT

public:
    explicit DocumentEditor(QWidget* dialogParent, QObject* parent = nullptr);

    // Clears the history; the document must outlive the editor or be replaced first
    void setDocument(VpDocument* document);

    QUndoStack* undoStack() const { return m_undo; }
    bool isModified() const;
    void markSaved();

    // Files and folders from disk; folders keep their structure and merge into
    // existing folders of the same name. Returns false if the user canceled.
    bool addPaths(int folder, const QStringList& paths);

    // newFolder returns the new folder, or -1; the renames return whether anything changed
    int newFolder(int parent);
    bool renameFile(int file);
    bool renameFolder(int folder);
    void remove(const std::vector<int>& files, const std::vector<int>& folders);
    void moveFiles(const std::vector<int>& files, int folder);

    // Points files at new contents on disk, for packing edited files back in
    void replaceContents(const std::map<int, QString>& files);

signals:
    // After any edit, undo, or redo
    void documentChanged();

private:
    QString askForName(const QString& title, const QString& label, const QString& current,
        const std::function<QString(const QString&)>& clash);
    void push(const QString& text, std::function<void()> undo, std::function<void()> redo);

    QWidget* m_dialogParent;
    VpDocument* m_document = nullptr;
    QUndoStack* m_undo;
};
