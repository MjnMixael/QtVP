#pragma once

#include <QDialog>

class QLineEdit;
namespace Ui { class OptionsDialog; }

// Loads the current settings on construction; saves and applies them on OK
class OptionsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit OptionsDialog(QWidget* parent = nullptr);
    ~OptionsDialog() override;

    // Saved settings, with their defaults
    static bool autoplayMedia();        // off
    static bool cleanupTempOnExit();    // on

    void accept() override;

private:
    void browseForTool(QLineEdit* edit, const QString& name);

    Ui::OptionsDialog* ui;
};
