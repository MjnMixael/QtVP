#include "PreviewWidget.h"

#include "Icons.h"

#include <QAudioOutput>
#include <QBuffer>
#include <QEvent>
#include <QFontDatabase>
#include <QLabel>
#include <QMediaMetaData>
#include <QMediaPlayer>
#include <QPainter>
#include <QPlainTextEdit>
#include <QStackedLayout>
#include <QTimer>
#include <QVBoxLayout>
#include <QVideoWidget>

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

BusyIndicator::BusyIndicator(QWidget* parent)
    : QWidget(parent)
{
    m_timer = new QTimer(this);
    m_timer->setInterval(30);
    connect(m_timer, &QTimer::timeout, this, [this] {
        m_angle = (m_angle + 12) % 360;
        update();
    });
}

void BusyIndicator::setText(const QString& text)
{
    m_text = text;
    update();
}

void BusyIndicator::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QColor color = palette().color(QPalette::WindowText);
    const int side = std::min({ width(), height() - fontMetrics().height() * 2, 32 });
    const int textHeight = fontMetrics().height();
    const int top = (height() - side - textHeight - 8) / 2;

    if (side > 4) {
        QRectF arc((width() - side) / 2.0, top, side, side);
        arc.adjust(2, 2, -2, -2);
        QColor track = color;
        track.setAlphaF(0.2f);
        painter.setPen(QPen(track, 3));
        painter.drawEllipse(arc);
        painter.setPen(QPen(color, 3, Qt::SolidLine, Qt::RoundCap));
        painter.drawArc(arc, -m_angle * 16, 90 * 16);
    }

    painter.setPen(color);
    const QRect textRect(0, top + std::max(side, 0) + 8, width(), textHeight);
    painter.drawText(textRect, Qt::AlignHCenter | Qt::AlignTop,
        fontMetrics().elidedText(m_text, Qt::ElideMiddle, width() - 8));
}

void BusyIndicator::showEvent(QShowEvent*)
{
    m_timer->start();
}

void BusyIndicator::hideEvent(QHideEvent*)
{
    m_timer->stop();
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
    updateSoundIcon();

    m_videoView = new QVideoWidget(this);

    m_textView = new QPlainTextEdit(this);
    m_textView->setReadOnly(true);
    m_textView->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_textView->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

    m_busy = new BusyIndicator(this);

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
    m_stack->addWidget(m_videoView);
    m_stack->addWidget(m_textView);
    m_stack->addWidget(m_busy);

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

PreviewWidget* PreviewWidget::s_mediaOwner = nullptr;

PreviewWidget::~PreviewWidget()
{
    if (s_mediaOwner == this)
        s_mediaOwner = nullptr;
}

void PreviewWidget::setContent(const PreviewContent& content)
{
    stopAndClear();

    if (content.kind == PreviewContent::Kind::Animation && !content.frames.empty()) {
        m_frames = content.frames;
        m_durations = content.durations;
        m_durations.resize(m_frames.size(), 100);
        m_stack->setCurrentWidget(m_imageView);
        showFrame(0);
        if (isPlayable())
            m_timer->start(m_durations[0]);
    } else if (content.kind == PreviewContent::Kind::Sound) {
        m_audio = content.audio;
        m_mediaName = content.title;
        m_stack->setCurrentWidget(m_soundView);
        m_frameLabel->setText(tr("Press Play to listen"));
    } else if (content.kind == PreviewContent::Kind::Movie) {
        m_movie = content.movie;
        m_hasMovie = true;
        m_mediaName = content.title;
        m_stack->setCurrentWidget(m_videoView);
        m_frameLabel->setText(tr("Press Play to watch"));
    } else if (content.kind == PreviewContent::Kind::Text) {
        m_textView->setPlainText(content.text);
        m_stack->setCurrentWidget(m_textView);
    } else if (content.kind == PreviewContent::Kind::Image) {
        m_imageView->setImage(content.image);
        m_stack->setCurrentWidget(m_imageView);
    } else {
        m_message->setText(content.message);
        m_stack->setCurrentWidget(m_message);
    }

    m_info->setText(content.info);
    m_info->setVisible(!content.info.isEmpty());
    m_frameLabel->setVisible(isPlayable());
    emit playbackChanged();

    if (m_autoplayMedia && hasMedia())
        togglePlay();
}

void PreviewWidget::setLoading(const QString& title)
{
    stopAndClear();
    m_busy->setText(tr("Loading %1").arg(title));
    m_stack->setCurrentWidget(m_busy);
    m_info->hide();
    m_frameLabel->hide();
    emit playbackChanged();
}

// Stops any playback and drops the previous preview's data
void PreviewWidget::stopAndClear()
{
    m_timer->stop();
    m_frames.clear();
    m_durations.clear();
    m_frame = 0;
    clearSound();
    m_imageView->setImage(QImage());
    m_textView->clear();
}

void PreviewWidget::changeEvent(QEvent* event)
{
    QFrame::changeEvent(event);
    const QEvent::Type type = event->type();
    if (type == QEvent::PaletteChange || type == QEvent::StyleChange || type == QEvent::ThemeChange)
        updateSoundIcon();
}

// A pixmap does not repaint itself, so redraw it in the current text color
void PreviewWidget::updateSoundIcon()
{
    if (m_soundView)
        m_soundView->setPixmap(Icons::icon(Icons::Shape::Sound).pixmap(QSize(48, 48), devicePixelRatioF()));
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

    if (hasMedia()) {
        prepareAudio();
        if (!m_mediaLoaded) {
            QIODevice* device = m_audioBuffer;
            if (m_hasMovie) {
                QString error;
                m_mediaDevice = m_movie.openDevice(this, &error);
                if (!m_mediaDevice) {
                    m_frameLabel->setText(tr("Could not read the movie: %1").arg(error));
                    return;
                }
                device = m_mediaDevice;
            } else {
                m_audioBuffer->setData(m_audio);
                m_audioBuffer->open(QIODevice::ReadOnly);
            }
            // The file name in the URL tells the backend what format to expect
            m_player->setSourceDevice(device, QUrl(m_mediaName));
            m_mediaLoaded = true;
        }
        if (m_player->playbackState() == QMediaPlayer::PlayingState) {
            m_player->pause();
        } else {
            // The pane and the pop-out each have a player; never let both be heard
            if (s_mediaOwner && s_mediaOwner != this)
                s_mediaOwner->stop();
            s_mediaOwner = this;
            m_player->play();
        }
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

    if (hasMedia()) {
        if (m_player)
            m_player->stop();
        return;
    }

    m_timer->stop();
    showFrame(0);
    emit playbackChanged();
}

void PreviewWidget::stopMedia()
{
    if (m_player && hasMedia())
        m_player->stop();
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

// The first player in the app loads the multimedia backend and opens the audio
// device, which takes a noticeable moment; later players are cheap
void PreviewWidget::prepareAudio()
{
    if (m_player)
        return;

    m_player = new QMediaPlayer(this);
    m_audioOutput = new QAudioOutput(this);
    m_player->setAudioOutput(m_audioOutput);
    m_player->setVideoOutput(m_videoView);
    m_audioBuffer = new QBuffer(this);
    connect(m_player, &QMediaPlayer::playbackStateChanged, this, &PreviewWidget::playbackChanged);
    connect(m_player, &QMediaPlayer::positionChanged, this, &PreviewWidget::updateSoundPosition);
    connect(m_player, &QMediaPlayer::durationChanged, this, &PreviewWidget::updateSoundPosition);
    connect(m_player, &QMediaPlayer::metaDataChanged, this, &PreviewWidget::updateSoundPosition);
    connect(m_player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString& text) {
        m_frameLabel->setText(m_hasMovie ? tr("Could not play the movie: %1").arg(text)
                                         : tr("Could not play the sound: %1").arg(text));
    });
}

// Detaches the player from the old data before it goes away
void PreviewWidget::clearSound()
{
    // Only touch the player if it has this media; tearing a source down is not free
    if (m_player && m_mediaLoaded) {
        m_player->stop();
        m_player->setSourceDevice(nullptr);
        m_audioBuffer->close();
        m_audioBuffer->setData(QByteArray());
    }
    if (m_mediaDevice) {
        m_mediaDevice->deleteLater();
        m_mediaDevice = nullptr;
    }
    m_audio.clear();
    m_movie = VpFileSource();
    m_hasMovie = false;
    m_mediaName.clear();
    m_mediaLoaded = false;
    if (s_mediaOwner == this)
        s_mediaOwner = nullptr;
}

// Position and length, plus the picture size once a movie has loaded
void PreviewWidget::updateSoundPosition()
{
    if (!m_mediaLoaded)
        return;

    auto format = [](qint64 ms) {
        return QString("%1:%2.%3").arg(ms / 60000).arg((ms / 1000) % 60, 2, 10, QChar('0')).arg((ms / 100) % 10);
    };
    QString text = QString("%1 / %2").arg(format(m_player->position()), format(m_player->duration()));
    if (m_hasMovie) {
        const QSize resolution = m_player->metaData().value(QMediaMetaData::Resolution).toSize();
        if (resolution.isValid())
            text += tr(", %1 x %2").arg(resolution.width()).arg(resolution.height());
    }
    m_frameLabel->setText(text);
}
