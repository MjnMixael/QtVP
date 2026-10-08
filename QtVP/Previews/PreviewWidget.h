#pragma once

#include <QFrame>
#include <QImage>
#include <QPixmap>

#include "Previews/PreviewContent.h"

class QLabel;
class QStackedLayout;

// Draws an image scaled to fit, over a checkerboard where it is transparent.
// Small images are enlarged by whole multiples so pixels stay sharp.
class ImageView : public QWidget
{
public:
    explicit ImageView(QWidget* parent = nullptr);

    void setImage(const QImage& image);
    QSize sizeHint() const override { return { 64, 64 }; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QImage m_image;
    QPixmap m_scaled;   // cached at the last drawn size
};

// Shows a PreviewContent: an image or a message, with an info line underneath.
// Used both in the main window's preview pane and in the pop-out window.
class PreviewWidget : public QFrame
{
    Q_OBJECT

public:
    explicit PreviewWidget(QWidget* parent = nullptr);

    void setContent(const PreviewContent& content);

private:
    QStackedLayout* m_stack = nullptr;
    QLabel* m_message = nullptr;
    ImageView* m_imageView = nullptr;
    QLabel* m_info = nullptr;
};
