#pragma once

#include <QDialog>

namespace Ui { class OptionsDialog; }

// Loads the current settings on construction; saves and applies them on OK
class OptionsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit OptionsDialog(QWidget* parent = nullptr);
    ~OptionsDialog() override;

    void accept() override;

private:
    Ui::OptionsDialog* ui;
};
