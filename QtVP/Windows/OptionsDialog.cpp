#include "OptionsDialog.h"
#include "ui_OptionsDialog.h"

#include "Theme.h"

OptionsDialog::OptionsDialog(QWidget* parent)
    : QDialog(parent)
    , ui(new Ui::OptionsDialog)
{
    ui->setupUi(this);

    ui->themeCombo->addItem(tr("System default"), int(Theme::Mode::System));
    ui->themeCombo->addItem(tr("Light"), int(Theme::Mode::Light));
    ui->themeCombo->addItem(tr("Dark"), int(Theme::Mode::Dark));
    ui->themeCombo->setCurrentIndex(ui->themeCombo->findData(int(Theme::saved())));

    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &OptionsDialog::accept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &OptionsDialog::reject);
}

OptionsDialog::~OptionsDialog()
{
    delete ui;
}

void OptionsDialog::accept()
{
    const auto theme = Theme::Mode(ui->themeCombo->currentData().toInt());
    if (theme != Theme::saved()) {
        Theme::save(theme);
        Theme::apply(theme);
    }
    QDialog::accept();
}
