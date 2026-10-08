#include "PreviewWindow.h"

#include <QCloseEvent>
#include <QHBoxLayout>
#include <QSettings>
#include <QShortcut>
#include <QToolButton>
#include <QVBoxLayout>

#include "Icons.h"
#include "Previews/PreviewWidget.h"

PreviewWindow::PreviewWindow(QWidget* parent)
    : QWidget(parent, Qt::Window)
{
    m_preview = new PreviewWidget(this);

    m_playButton = new QToolButton(this);
    m_playButton->setAutoRaise(true);
    m_stopButton = new QToolButton(this);
    m_stopButton->setAutoRaise(true);
    m_stopButton->setIcon(Icons::icon(Icons::Shape::Stop));
    m_stopButton->setToolTip(tr("Stop"));

    auto* controls = new QHBoxLayout;
    controls->setSpacing(2);
    controls->addWidget(m_playButton);
    controls->addWidget(m_stopButton);
    controls->addStretch();

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(2);
    layout->addLayout(controls);
    layout->addWidget(m_preview, 1);

    connect(m_playButton, &QToolButton::clicked, m_preview, &PreviewWidget::togglePlay);
    connect(m_stopButton, &QToolButton::clicked, m_preview, &PreviewWidget::stop);
    connect(m_preview, &PreviewWidget::playbackChanged, this, &PreviewWindow::updatePlaybackButtons);

    new QShortcut(QKeySequence(Qt::Key_Escape), this, this, &QWidget::close);
    new QShortcut(QKeySequence(Qt::Key_Space), this, m_preview, &PreviewWidget::togglePlay);

    resize(800, 600);
    restoreGeometry(QSettings().value("window/previewGeometry").toByteArray());
    updatePlaybackButtons();
}

void PreviewWindow::setContent(const PreviewContent& content)
{
    m_preview->setContent(content);
    setWindowTitle(content.title.isEmpty() ? tr("Preview") : tr("Preview - %1").arg(content.title));
}

void PreviewWindow::setLoading(const QString& title)
{
    m_preview->setLoading(title);
    setWindowTitle(tr("Preview - %1").arg(title));
}

// Closing only hides the window, so stop and drop whatever it holds; a sound would
// otherwise keep playing with no visible way to stop it
void PreviewWindow::closeEvent(QCloseEvent* event)
{
    m_preview->setContent(PreviewContent());
    QSettings().setValue("window/previewGeometry", saveGeometry());
    event->accept();
}

void PreviewWindow::updatePlaybackButtons()
{
    const bool playable = m_preview->isPlayable();
    const bool playing = m_preview->isPlaying();
    m_playButton->setEnabled(playable);
    m_stopButton->setEnabled(playable);
    m_playButton->setIcon(Icons::icon(playing ? Icons::Shape::Pause : Icons::Shape::Play));
    m_playButton->setToolTip(playing ? tr("Pause") : tr("Play"));
}
