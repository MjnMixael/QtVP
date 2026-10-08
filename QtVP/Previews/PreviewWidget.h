#pragma once

#include <QFrame>
#include <QImage>
#include <QPixmap>

#include <vector>

#include "Previews/PreviewContent.h"

class QAudioOutput;
class QBuffer;
class QLabel;
class QMediaPlayer;
class QPlainTextEdit;
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

// A spinning arc with a line of text under it, shown while a preview loads.
// Only animates while visible.
class BusyIndicator : public QWidget
{
public:
    explicit BusyIndicator(QWidget* parent = nullptr);

    void setText(const QString& text);

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    QTimer* m_timer = nullptr;
    QString m_text;
    int m_angle = 0;
};

// Shows a PreviewContent: an image, an animation, a sound, text, or a message, with an
// info line underneath. Used both in the main window's preview pane and in the
// pop-out window. Animations start playing as soon as they are shown; sounds wait
// for Play, so stepping through a folder of sounds stays quiet.
class PreviewWidget : public QFrame
{
    Q_OBJECT

public:
    explicit PreviewWidget(QWidget* parent = nullptr);

    void setContent(const PreviewContent& content);

    // Clears the current preview and shows a spinner until the next setContent()
    void setLoading(const QString& title);

    bool isPlayable() const { return m_frames.size() > 1 || !m_audio.isEmpty(); }
    bool isPlaying() const;

    void togglePlay();
    void stop();

    // Creates the sound player now instead of on the first Play
    void prepareAudio();

signals:
    void playbackChanged();

protected:
    void changeEvent(QEvent* event) override;

private:
    void stopAndClear();
    void updateSoundIcon();
    void showFrame(size_t index);
    void nextFrame();
    void clearSound();
    void updateSoundPosition();

    QStackedLayout* m_stack = nullptr;
    QLabel* m_message = nullptr;
    ImageView* m_imageView = nullptr;
    QLabel* m_soundView = nullptr;
    QPlainTextEdit* m_textView = nullptr;
    BusyIndicator* m_busy = nullptr;
    QLabel* m_info = nullptr;
    QLabel* m_frameLabel = nullptr;     // frame counter, or sound position

    QTimer* m_timer = nullptr;
    std::vector<QImage> m_frames;
    std::vector<int> m_durations;
    size_t m_frame = 0;

    // Created on first use; the source is only set when Play is pressed
    QMediaPlayer* m_player = nullptr;
    QAudioOutput* m_audioOutput = nullptr;
    QBuffer* m_audioBuffer = nullptr;
    QByteArray m_audio;
    QString m_audioName;
    bool m_audioLoaded = false;
};
