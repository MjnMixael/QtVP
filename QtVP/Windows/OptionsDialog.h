#pragma once

#include <QDialog>

namespace Ui { class OptionsDialog; }

// Loads its settings on construction and saves them on OK
class OptionsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit OptionsDialog(QWidget* parent = nullptr);
    ~OptionsDialog() override;

    static QString fsFolder();

    void accept() override;

private:
    void onBrowse();

    Ui::OptionsDialog* ui;
};
