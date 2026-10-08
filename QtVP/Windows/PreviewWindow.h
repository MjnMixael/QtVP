#pragma once

#include <QWidget>

#include "Previews/PreviewContent.h"

class PreviewWidget;
class QToolButton;

// The larger, separate preview. It follows the main window's selection while open.
class PreviewWindow : public QWidget
{
    Q_OBJECT

public:
    explicit PreviewWindow(QWidget* parent = nullptr);

    void setContent(const PreviewContent& content);
    void setLoading(const QString& title);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void updatePlaybackButtons();

    PreviewWidget* m_preview = nullptr;
    QToolButton* m_playButton = nullptr;
    QToolButton* m_stopButton = nullptr;
};
