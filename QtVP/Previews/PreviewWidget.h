#pragma once

#include <QFrame>
#include <QImage>
#include <QPixmap>

#include <vector>

#include "Previews/PreviewContent.h"

class QLabel;
class QStackedLayout;
class QTimer;

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

// Shows a PreviewContent: an image, an animation, or a message, with an info line
// underneath. Used both in the main window's preview pane and in the pop-out window.
// Animations start playing as soon as they are shown.
class PreviewWidget : public QFrame
{
    Q_OBJECT

public:
    explicit PreviewWidget(QWidget* parent = nullptr);

    void setContent(const PreviewContent& content);

    bool isPlayable() const { return m_frames.size() > 1; }
    bool isPlaying() const;

    void togglePlay();
    void stop();

signals:
    void playbackChanged();

private:
    void showFrame(size_t index);
    void nextFrame();

    QStackedLayout* m_stack = nullptr;
    QLabel* m_message = nullptr;
    ImageView* m_imageView = nullptr;
    QLabel* m_info = nullptr;
    QLabel* m_frameLabel = nullptr;

    QTimer* m_timer = nullptr;
    std::vector<QImage> m_frames;
    std::vector<int> m_durations;
    size_t m_frame = 0;
};
