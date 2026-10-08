#include "PreviewWidget.h"

#include <QLabel>
#include <QPainter>
#include <QStackedLayout>
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

    m_info = new QLabel(this);
    m_info->setAlignment(Qt::AlignCenter);
    m_info->setWordWrap(true);
    m_info->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_info->hide();

    m_stack = new QStackedLayout;
    m_stack->addWidget(m_message);
    m_stack->addWidget(m_imageView);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(2);
    layout->addLayout(m_stack, 1);
    layout->addWidget(m_info);

    setContent(PreviewContent::fromMessage(tr("Select a file to preview")));
}

void PreviewWidget::setContent(const PreviewContent& content)
{
    if (content.kind == PreviewContent::Kind::Image) {
        m_imageView->setImage(content.image);
        m_stack->setCurrentWidget(m_imageView);
    } else {
        m_imageView->setImage(QImage());
        m_message->setText(content.message);
        m_stack->setCurrentWidget(m_message);
    }

    m_info->setText(content.info);
    m_info->setVisible(!content.info.isEmpty());
}
