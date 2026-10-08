#include "PreviewWidget.h"

#include <QAudioOutput>
#include <QBuffer>
#include <QFontDatabase>
#include <QLabel>
#include <QMediaPlayer>
#include <QPainter>
#include <QPlainTextEdit>
#include <QStackedLayout>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {

QBrush checkerBrush()
{
    static const QBrush brush = [] {
        QPixmap tile(16, 16);
        tile.fill(QColor(204, 204, 204));
        QPainter painter(&tile);
        painter.fillRect(0, 0, 8, 8, QColor(153, 153, 153));
        painter.fillRect(8, 8, 8, 8, QColor(153, 153, 153));
        return QBrush(tile);
    }();
    return brush;
}

} // namespace

ImageView::ImageView(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(1, 1);
}

void ImageView::setImage(const QImage& image)
{
    m_image = image;
    m_scaled = QPixmap();
    update();
}

void ImageView::paintEvent(QPaintEvent*)
{
    if (m_image.isNull())
        return;

    double scale = std::min(double(width()) / m_image.width(), double(height()) / m_image.height());
    const bool enlarging = scale >= 1.0;
    if (enlarging)
        scale = std::floor(scale);

    const QSize size(std::max(1, int(m_image.width() * scale)), std::max(1, int(m_image.height() * scale)));
    if (m_scaled.size() != size) {
        m_scaled = QPixmap::fromImage(m_image.scaled(size, Qt::IgnoreAspectRatio,
            enlarging ? Qt::FastTransformation : Qt::SmoothTransformation));
    }

    QRect target(QPoint(0, 0), size);
    target.moveCenter(rect().center());

    QPainter painter(this);
    if (m_image.hasAlphaChannel())
        painter.fillRect(target, checkerBrush());
    painter.drawPixmap(target, m_scaled);
}

PreviewWidget::PreviewWidget(QWidget* parent)
    : QFrame(parent)
{
    setFrameShape(QFrame::StyledPanel);

    m_message = new QLabel(this);
    m_message->setAlignment(Qt::AlignCenter);
    m_message->setWordWrap(true);
    m_message->setMargin(8);

    m_imageView = new ImageView(this);

    m_soundView = new QLabel(this);
    m_soundView->setAlignment(Qt::AlignCenter);
    m_soundView->setPixmap(style()->standardIcon(QStyle::SP_MediaVolume).pixmap(48, 48));

    m_textView = new QPlainTextEdit(this);
    m_textView->setReadOnly(true);
    m_textView->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_textView->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

    m_info = new QLabel(this);
    m_info->setAlignment(Qt::AlignCenter);
    m_info->setWordWrap(true);
    m_info->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_info->hide();

    m_frameLabel = new QLabel(this);
    m_frameLabel->setAlignment(Qt::AlignCenter);
    m_frameLabel->hide();

    m_stack = new QStackedLayout;
    m_stack->addWidget(m_message);
    m_stack->addWidget(m_imageView);
    m_stack->addWidget(m_soundView);
    m_stack->addWidget(m_textView);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(2);
    layout->addLayout(m_stack, 1);
    layout->addWidget(m_info);
    layout->addWidget(m_frameLabel);

    // Single-shot, restarted with each frame's own duration
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &PreviewWidget::nextFrame);

    setContent(PreviewContent::fromMessage(tr("Select a file to preview")));
}

void PreviewWidget::setContent(const PreviewContent& content)
{
    m_timer->stop();
    m_frames.clear();
    m_durations.clear();
    m_frame = 0;
    clearSound();
    if (content.kind != PreviewContent::Kind::Text)
        m_textView->clear();

    if (content.kind == PreviewContent::Kind::Animation && !content.frames.empty()) {
        m_frames = content.frames;
        m_durations = content.durations;
        m_durations.resize(m_frames.size(), 100);
        m_stack->setCurrentWidget(m_imageView);
        showFrame(0);
        if (isPlayable())
            m_timer->start(m_durations[0]);
    } else if (content.kind == PreviewContent::Kind::Sound) {
        m_imageView->setImage(QImage());
        m_audio = content.audio;
        m_audioName = content.title;
        m_stack->setCurrentWidget(m_soundView);
        m_frameLabel->setText(tr("Press Play to listen"));
    } else if (content.kind == PreviewContent::Kind::Text) {
        m_imageView->setImage(QImage());
        m_textView->setPlainText(content.text);
        m_stack->setCurrentWidget(m_textView);
    } else if (content.kind == PreviewContent::Kind::Image) {
        m_imageView->setImage(content.image);
        m_stack->setCurrentWidget(m_imageView);
    } else {
        m_imageView->setImage(QImage());
        m_message->setText(content.message);
        m_stack->setCurrentWidget(m_message);
    }

    m_info->setText(content.info);
    m_info->setVisible(!content.info.isEmpty());
    m_frameLabel->setVisible(isPlayable());
    emit playbackChanged();
}

bool PreviewWidget::isPlaying() const
{
    if (m_timer->isActive())
        return true;
    return m_player && m_player->playbackState() == QMediaPlayer::PlayingState;
}

// Play and pause
void PreviewWidget::togglePlay()
{
    if (!isPlayable())
        return;

    if (!m_audio.isEmpty()) {
        if (!m_player) {
            m_player = new QMediaPlayer(this);
            m_audioOutput = new QAudioOutput(this);
            m_player->setAudioOutput(m_audioOutput);
            m_audioBuffer = new QBuffer(this);
            connect(m_player, &QMediaPlayer::playbackStateChanged, this, &PreviewWidget::playbackChanged);
            connect(m_player, &QMediaPlayer::positionChanged, this, &PreviewWidget::updateSoundPosition);
            connect(m_player, &QMediaPlayer::durationChanged, this, &PreviewWidget::updateSoundPosition);
            connect(m_player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString& text) {
                m_frameLabel->setText(tr("Could not play the sound: %1").arg(text));
            });
        }
        if (!m_audioLoaded) {
            m_audioBuffer->setData(m_audio);
            m_audioBuffer->open(QIODevice::ReadOnly);
            // The file name in the URL tells the backend what format to expect
            m_player->setSourceDevice(m_audioBuffer, QUrl(m_audioName));
            m_audioLoaded = true;
        }
        if (m_player->playbackState() == QMediaPlayer::PlayingState)
            m_player->pause();
        else
            m_player->play();
        return;
    }

    if (isPlaying())
        m_timer->stop();
    else
        m_timer->start(m_durations[m_frame]);
    emit playbackChanged();
}

// Stops and goes back to the start
void PreviewWidget::stop()
{
    if (!isPlayable())
        return;

    if (!m_audio.isEmpty()) {
        if (m_player)
            m_player->stop();
        return;
    }

    m_timer->stop();
    showFrame(0);
    emit playbackChanged();
}

void PreviewWidget::showFrame(size_t index)
{
    m_frame = index;
    m_imageView->setImage(m_frames[index]);
    m_frameLabel->setText(tr("Frame %1 of %2").arg(index + 1).arg(m_frames.size()));
}

void PreviewWidget::nextFrame()
{
    showFrame((m_frame + 1) % m_frames.size());
    m_timer->start(m_durations[m_frame]);
}

// Detaches the player from the old buffer before its data goes away
void PreviewWidget::clearSound()
{
    if (m_player) {
        m_player->stop();
        m_player->setSourceDevice(nullptr);
        m_audioBuffer->close();
        m_audioBuffer->setData(QByteArray());
    }
    m_audio.clear();
    m_audioName.clear();
    m_audioLoaded = false;
}

void PreviewWidget::updateSoundPosition()
{
    auto format = [](qint64 ms) {
        return QString("%1:%2.%3").arg(ms / 60000).arg((ms / 1000) % 60, 2, 10, QChar('0')).arg((ms / 100) % 10);
    };
    m_frameLabel->setText(QString("%1 / %2").arg(format(m_player->position()), format(m_player->duration())));
}
