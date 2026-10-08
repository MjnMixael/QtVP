#include "OptionsDialog.h"
#include "ui_OptionsDialog.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QSettings>

#include "Theme.h"
#include "Windows/FileOpener.h"

OptionsDialog::OptionsDialog(QWidget* parent)
    : QDialog(parent)
    , ui(new Ui::OptionsDialog)
{
    ui->setupUi(this);

    ui->themeCombo->addItem(tr("System default"), int(Theme::Mode::System));
    ui->themeCombo->addItem(tr("Light"), int(Theme::Mode::Light));
    ui->themeCombo->addItem(tr("Dark"), int(Theme::Mode::Dark));
    ui->themeCombo->setCurrentIndex(ui->themeCombo->findData(int(Theme::saved())));
    ui->autoplayCheck->setChecked(autoplayMedia());
    ui->cleanupCheck->setChecked(cleanupTempOnExit());

    ui->animStudioEdit->setText(QDir::toNativeSeparators(FileOpener::toolPath(FileOpener::Tool::AnimStudio)));
    ui->pofToolsEdit->setText(QDir::toNativeSeparators(FileOpener::toolPath(FileOpener::Tool::PofTools)));

    connect(ui->animStudioBrowse, &QToolButton::clicked, this, [this] {
        browseForTool(ui->animStudioEdit, FileOpener::toolName(FileOpener::Tool::AnimStudio));
    });
    connect(ui->pofToolsBrowse, &QToolButton::clicked, this, [this] {
        browseForTool(ui->pofToolsEdit, FileOpener::toolName(FileOpener::Tool::PofTools));
    });
    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &OptionsDialog::accept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &OptionsDialog::reject);
}

OptionsDialog::~OptionsDialog()
{
    delete ui;
}

bool OptionsDialog::autoplayMedia()
{
    return QSettings().value("preview/autoplayMedia", false).toBool();
}

bool OptionsDialog::cleanupTempOnExit()
{
    return QSettings().value("temp/cleanupOnExit", true).toBool();
}

void OptionsDialog::accept()
{
    // A blank path is fine; a path to nothing is probably a typo
    const struct { QLineEdit* edit; FileOpener::Tool tool; } tools[] = {
        { ui->animStudioEdit, FileOpener::Tool::AnimStudio },
        { ui->pofToolsEdit, FileOpener::Tool::PofTools },
    };
    for (const auto& t : tools) {
        const QString path = t.edit->text().trimmed();
        if (!path.isEmpty() && !QFileInfo::exists(path)) {
            QMessageBox::warning(this, windowTitle(), tr("%1 was not found at %2.")
                .arg(FileOpener::toolName(t.tool), QDir::toNativeSeparators(path)));
            t.edit->setFocus();
            return;
        }
    }
    for (const auto& t : tools)
        FileOpener::setToolPath(t.tool, QDir::fromNativeSeparators(t.edit->text().trimmed()));

    QSettings settings;
    settings.setValue("preview/autoplayMedia", ui->autoplayCheck->isChecked());
    settings.setValue("temp/cleanupOnExit", ui->cleanupCheck->isChecked());

    const auto theme = Theme::Mode(ui->themeCombo->currentData().toInt());
    if (theme != Theme::saved()) {
        Theme::save(theme);
        Theme::apply(theme);
    }
    QDialog::accept();
}

void OptionsDialog::browseForTool(QLineEdit* edit, const QString& name)
{
    const QString current = QDir::fromNativeSeparators(edit->text().trimmed());
    const QString path = QFileDialog::getOpenFileName(this, tr("Locate %1").arg(name),
        current.isEmpty() ? QString() : QFileInfo(current).absolutePath(), FileOpener::programFilter());
    if (!path.isEmpty())
        edit->setText(QDir::toNativeSeparators(path));
}
