#pragma once

#include <QMainWindow>

#include <memory>

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

private:
    void setupIcons();
    void setupConnections();
    void restoreLayout();
    void saveLayout();
    void updateActions();

    void onLoadVp();
    void onAbout();

    Ui::MainWindow* ui;
    std::unique_ptr<VpArchive> m_archive;
};
