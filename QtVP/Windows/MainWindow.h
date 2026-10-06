#pragma once

#include <QMainWindow>

namespace Ui { class MainWindow; }

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    void openVp(const QString& path);

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
    QString m_currentPath;
};
