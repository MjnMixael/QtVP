#pragma once

#include <QList>
#include <QMainWindow>
#include <QUrl>

#include <memory>
#include <vector>

class FileListModel;
class FolderTreeModel;
class QLabel;
class QMenu;
class QModelIndex;
class QSortFilterProxyModel;
class QTemporaryDir;
class VpArchive;
namespace Ui { class MainWindow; }

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    void openVp(const QString& path);
    void closeVp();

protected:
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void setupIcons();
    void setupModels();
    void setupConnections();
    void restoreLayout();
    void saveLayout();
    void updateActions();
    void updateSelectionStatus();

    void showFolder(const QModelIndex& index);
    void onFilterChanged(const QString& text);
    std::vector<int> selectedEntries() const;
    bool extractionSet(std::vector<int>& entries, int& baseFolder) const;
    void extractTo(const QString& target, std::vector<int> entries, int baseFolder);
    QList<QUrl> extractForDrag(const std::vector<int>& entries);

    void addRecentFile(const QString& path);
    void removeRecentFile(const QString& path);
    void updateRecentMenu();

    void onLoadVp();
    void onExtractToDir();
    void onAbout();

    Ui::MainWindow* ui;
    std::unique_ptr<VpArchive> m_archive;
    FolderTreeModel* m_folderModel = nullptr;
    FileListModel* m_fileModel = nullptr;
    QSortFilterProxyModel* m_fileProxy = nullptr;
    QLabel* m_selectionLabel = nullptr;
    QMenu* m_recentMenu = nullptr;
    int m_currentFolder;

    // Drag-out extracts here; removed when the app exits
    std::unique_ptr<QTemporaryDir> m_dragDir;
    int m_dragCount = 0;
};
