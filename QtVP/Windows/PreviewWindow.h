#pragma once

#include <QWidget>

#include "Previews/PreviewContent.h"

class PreviewWidget;

// The larger, separate preview. It follows the main window's selection while open.
class PreviewWindow : public QWidget
{
    Q_OBJECT

public:
    explicit PreviewWindow(QWidget* parent = nullptr);

    void setContent(const PreviewContent& content);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    PreviewWidget* m_preview = nullptr;
};
