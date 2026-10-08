#pragma once

#include <QFutureWatcher>
#include <QList>
#include <QMainWindow>
#include <QUrl>

#include <memory>
#include <vector>

#include "Previews/PreviewContent.h"

class DocumentEditor;
class FileListModel;
class FolderTreeModel;
class PreviewWindow;
class QLabel;
class QMenu;
class QModelIndex;
class QSortFilterProxyModel;
class QTemporaryDir;
class QTimer;
class VpDocument;
namespace Ui { class MainWindow; }

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    // Loads the slow-to-start parts up front; called while the splash screen shows
    void warmUp();

    // These ask about unsaved changes first
    void openVp(const QString& path);
    void closeVp();
    void newVp();

protected:
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void setupIcons();
    void setupModels();
    void setupConnections();
    void restoreLayout();
    void saveLayout();
    void updateActions();
    void updateSelectionStatus();
    void updatePreview();
    void updatePlaybackButtons();
    void resetPreview();
    void openPreviewWindow();
    bool previewWindowOpen() const;
    void startPreviewLoad();
    void onPreviewLoaded();
    void showPreviewSpinner();
    void showPreview(const PreviewContent& content);
    void waitForPreviewLoad();

    void showFolder(const QModelIndex& index);
    void onFilterChanged(const QString& text);
    std::vector<int> selectedEntries() const;
    bool extractionSet(std::vector<int>& entries, int& baseFolder) const;
    void extractTo(const QString& target, std::vector<int> entries, int baseFolder);
    QList<QUrl> extractForDrag(const std::vector<int>& entries);
    void openSelected(bool chooseApp);
    static QString openFolderRoot();
    void applySettings();

    void addRecentFile(const QString& path);
    void removeRecentFile(const QString& path);
    void updateRecentMenu();

    void onLoadVp();

    // Documents and saving
    void loadVp(const QString& path);
    void setDocument(std::unique_ptr<VpDocument> document);
    bool maybeSave();
    bool save();
    bool saveAs();
    bool saveTo(const QString& path);
    void updateTitle();

    // Editing
    int editFolder() const;
    void onAddFiles();
    void onAddFolder();
    void onNewFolder();
    void onRename();
    void onDelete();
    void refreshAfterEdit();
    void collectExpanded(const QModelIndex& parent, std::vector<int>& folders) const;
    void selectFiles(const std::vector<int>& files, int current);
    bool handleViewDrag(bool onTree, QDropEvent* event, bool drop);
    void onExtractToDir();
    void onAbout();

    Ui::MainWindow* ui;
    std::unique_ptr<VpDocument> m_document;
    DocumentEditor* m_editor = nullptr;
    FolderTreeModel* m_folderModel = nullptr;
    FileListModel* m_fileModel = nullptr;
    QSortFilterProxyModel* m_fileProxy = nullptr;
    QLabel* m_selectionLabel = nullptr;
    QMenu* m_recentMenu = nullptr;
    int m_currentFolder;

    // What the preview pane and pop-out window show; m_previewEntry is -1 when it is not a file
    PreviewContent m_preview;
    int m_previewEntry = -1;
    PreviewWindow* m_previewWindow = nullptr;

    // Previews load on a worker thread, one at a time. Each request gets a number so
    // results for files the user has already moved past are dropped.
    QFutureWatcher<PreviewContent>* m_previewWatcher = nullptr;
    QTimer* m_previewSpinnerTimer = nullptr;
    int m_previewRequest = 0;       // the newest request
    int m_loadingRequest = 0;       // the request the running load belongs to
    bool m_previewQueued = false;   // the newest request waits for the running load
    bool m_previewSpinnerShown = false;

    // Drag-out extracts here; removed when the app exits
    std::unique_ptr<QTemporaryDir> m_dragDir;
    int m_dragCount = 0;

    // Open extracts under here; created on first use, removed on exit if the option is on
    QString m_sessionOpenFolder;
    int m_openCount = 0;
};
