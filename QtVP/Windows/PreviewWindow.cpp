#include "PreviewWindow.h"

#include <QCloseEvent>
#include <QSettings>
#include <QShortcut>
#include <QVBoxLayout>

#include "Previews/PreviewWidget.h"

PreviewWindow::PreviewWindow(QWidget* parent)
    : QWidget(parent, Qt::Window)
{
    m_preview = new PreviewWidget(this);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(m_preview);

    new QShortcut(QKeySequence(Qt::Key_Escape), this, this, &QWidget::close);

    resize(800, 600);
    restoreGeometry(QSettings().value("window/previewGeometry").toByteArray());
}

void PreviewWindow::setContent(const PreviewContent& content)
{
    m_preview->setContent(content);
    setWindowTitle(content.title.isEmpty() ? tr("Preview") : tr("Preview - %1").arg(content.title));
}

void PreviewWindow::closeEvent(QCloseEvent* event)
{
    QSettings().setValue("window/previewGeometry", saveGeometry());
    event->accept();
}
