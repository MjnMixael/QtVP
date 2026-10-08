#include "OptionsDialog.h"
#include "ui_OptionsDialog.h"

#include <QDir>
#include <QFileDialog>
#include <QMessageBox>
#include <QSettings>

OptionsDialog::OptionsDialog(QWidget* parent)
    : QDialog(parent)
    , ui(new Ui::OptionsDialog)
{
    ui->setupUi(this);
    ui->fsFolderEdit->setText(QDir::toNativeSeparators(fsFolder()));

    connect(ui->browseButton, &QToolButton::clicked, this, &OptionsDialog::onBrowse);
    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &OptionsDialog::accept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &OptionsDialog::reject);
}

OptionsDialog::~OptionsDialog()
{
    delete ui;
}

QString OptionsDialog::fsFolder()
{
    return QSettings().value("paths/fsFolder").toString();
}

void OptionsDialog::accept()
{
    QString folder = QDir::fromNativeSeparators(ui->fsFolderEdit->text().trimmed());

    if (!folder.isEmpty()) {
        QDir dir(folder);
        if (!dir.exists()) {
            QMessageBox::warning(this, windowTitle(), tr("The folder %1 does not exist.")
                .arg(QDir::toNativeSeparators(folder)));
            return;
        }

        // VP paths already start with data, so picking the data folder itself would nest them
        if (dir.dirName().compare("data", Qt::CaseInsensitive) == 0 && dir.cdUp()) {
            const auto answer = QMessageBox::question(this, windowTitle(),
                tr("%1 looks like a data folder. Files in a VP already start with data\\, so they would end up in data\\data.\n\nUse %2 instead?")
                    .arg(QDir::toNativeSeparators(folder), QDir::toNativeSeparators(dir.absolutePath())),
                QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
            if (answer == QMessageBox::Cancel)
                return;
            if (answer == QMessageBox::Yes)
                folder = dir.absolutePath();
        }
    }

    QSettings().setValue("paths/fsFolder", folder);
    QDialog::accept();
}

void OptionsDialog::onBrowse()
{
    const QString start = QDir::fromNativeSeparators(ui->fsFolderEdit->text().trimmed());
    const QString folder = QFileDialog::getExistingDirectory(this, tr("FreeSpace Folder"), start);
    if (!folder.isEmpty())
        ui->fsFolderEdit->setText(QDir::toNativeSeparators(folder));
}
