#include "Icons.h"

#include <QGuiApplication>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>

namespace {

// Paints on demand, so every pixmap Qt asks for uses the palette in effect right then
class ShapeIconEngine : public QIconEngine
{
public:
    explicit ShapeIconEngine(Icons::Shape shape)
        : m_shape(shape)
    {
    }

    QIconEngine* clone() const override { return new ShapeIconEngine(m_shape); }

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State) override
    {
        const QPalette palette = QGuiApplication::palette();
        const QColor color = palette.color(mode == QIcon::Disabled ? QPalette::Disabled : QPalette::Active,
            QPalette::ButtonText);

        // Shapes are drawn on a 16 x 16 grid, centered and scaled to fit
        const qreal side = qMin(rect.width(), rect.height());
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->translate(rect.x() + (rect.width() - side) / 2.0, rect.y() + (rect.height() - side) / 2.0);
        painter->scale(side / 16.0, side / 16.0);

        QPen pen(color, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        switch (m_shape) {
        case Icons::Shape::Play: {
            QPainterPath path;
            path.moveTo(4.5, 2.5);
            path.lineTo(13.5, 8);
            path.lineTo(4.5, 13.5);
            path.closeSubpath();
            painter->setPen(Qt::NoPen);
            painter->setBrush(color);
            painter->drawPath(path);
            break;
        }
        case Icons::Shape::Pause:
            painter->setPen(Qt::NoPen);
            painter->setBrush(color);
            painter->drawRoundedRect(QRectF(3.5, 2.5, 3.5, 11), 0.75, 0.75);
            painter->drawRoundedRect(QRectF(9, 2.5, 3.5, 11), 0.75, 0.75);
            break;
        case Icons::Shape::Stop:
            painter->setPen(Qt::NoPen);
            painter->setBrush(color);
            painter->drawRoundedRect(QRectF(3, 3, 10, 10), 1, 1);
            break;
        case Icons::Shape::PopOut:
            // A window with an arrow leaving its top-right corner
            painter->setPen(pen);
            painter->setBrush(Qt::NoBrush);
            painter->drawPolyline(QPolygonF({ { 7, 2.75 }, { 2.75, 2.75 }, { 2.75, 13.25 }, { 13.25, 13.25 }, { 13.25, 9 } }));
            painter->drawLine(QPointF(7.5, 8.5), QPointF(13.25, 2.75));
            painter->drawPolyline(QPolygonF({ { 9, 2.75 }, { 13.25, 2.75 }, { 13.25, 7 } }));
            break;
        case Icons::Shape::Sound: {
            QPainterPath speaker;
            speaker.moveTo(1.5, 6);
            speaker.lineTo(4.5, 6);
            speaker.lineTo(8.5, 2.5);
            speaker.lineTo(8.5, 13.5);
            speaker.lineTo(4.5, 10);
            speaker.lineTo(1.5, 10);
            speaker.closeSubpath();
            painter->setPen(Qt::NoPen);
            painter->setBrush(color);
            painter->drawPath(speaker);

            pen.setWidthF(1.25);
            painter->setPen(pen);
            painter->setBrush(Qt::NoBrush);
            painter->drawArc(QRectF(7, 5, 6, 6), -50 * 16, 100 * 16);
            painter->drawArc(QRectF(6, 2.5, 10, 11), -55 * 16, 110 * 16);
            break;
        }
        }
        painter->restore();
    }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override
    {
        QPixmap pixmap(size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        paint(&painter, QRect(QPoint(0, 0), size), mode, state);
        return pixmap;
    }

    // Draws at device pixels so the shapes stay crisp on scaled displays
    QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State state, qreal scale) override
    {
        QPixmap result = pixmap(size * scale, mode, state);
        result.setDevicePixelRatio(scale);
        return result;
    }

private:
    Icons::Shape m_shape;
};

// Mid-tone colors that read on both the light and dark themes
QColor kindColor(Icons::FileKind kind)
{
    switch (kind) {
    case Icons::FileKind::Image: return QColor(76, 175, 80);
    case Icons::FileKind::Animation: return QColor(171, 71, 188);
    case Icons::FileKind::Sound: return QColor(255, 152, 0);
    case Icons::FileKind::Movie: return QColor(229, 57, 53);
    case Icons::FileKind::Text: return QColor(66, 133, 244);
    case Icons::FileKind::Mission: return QColor(0, 172, 193);
    case Icons::FileKind::Model: return QColor(161, 136, 127);
    case Icons::FileKind::Script: return QColor(236, 64, 122);
    case Icons::FileKind::Font: return QColor(120, 144, 156);
    case Icons::FileKind::Other: break;
    }
    return QColor();
}

class FileIconEngine : public QIconEngine
{
public:
    explicit FileIconEngine(Icons::FileKind kind)
        : m_kind(kind)
    {
    }

    QIconEngine* clone() const override { return new FileIconEngine(m_kind); }

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State) override
    {
        const QPalette palette = QGuiApplication::palette();
        const bool disabled = mode == QIcon::Disabled;
        const QPalette::ColorGroup group = disabled ? QPalette::Disabled : QPalette::Active;
        QColor outline = palette.color(group, mode == QIcon::Selected ? QPalette::HighlightedText : QPalette::Text);
        outline.setAlphaF(0.7f);
        const QColor mark = disabled ? palette.color(QPalette::Disabled, QPalette::Text) : kindColor(m_kind);

        // Drawn on a 16 x 16 grid, centered and scaled to fit
        const qreal side = qMin(rect.width(), rect.height());
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->translate(rect.x() + (rect.width() - side) / 2.0, rect.y() + (rect.height() - side) / 2.0);
        painter->scale(side / 16.0, side / 16.0);

        // A page with a folded corner
        QPainterPath page;
        page.moveTo(3.5, 1.5);
        page.lineTo(9.5, 1.5);
        page.lineTo(12.5, 4.5);
        page.lineTo(12.5, 14.5);
        page.lineTo(3.5, 14.5);
        page.closeSubpath();
        painter->setPen(QPen(outline, 1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(page);
        painter->drawPolyline(QPolygonF({ { 9.5, 1.5 }, { 9.5, 4.5 }, { 12.5, 4.5 } }));

        const QPen markPen(mark, 1.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        switch (m_kind) {
        case Icons::FileKind::Image:
            // Mountains and a sun
            painter->setPen(Qt::NoPen);
            painter->setBrush(mark);
            painter->drawPolygon(QPolygonF({ { 5, 12.75 }, { 7.25, 9 }, { 8.75, 11 }, { 9.75, 9.75 }, { 11, 12.75 } }));
            painter->drawEllipse(QPointF(9.75, 7.25), 1.1, 1.1);
            break;
        case Icons::FileKind::Animation:
            // Two stacked frames
            painter->setPen(markPen);
            painter->setBrush(Qt::NoBrush);
            painter->drawRect(QRectF(6.75, 6.5, 4.25, 3.75));
            painter->setPen(Qt::NoPen);
            painter->setBrush(mark);
            painter->drawRect(QRectF(5, 8.75, 4.25, 3.75));
            break;
        case Icons::FileKind::Sound: {
            QPainterPath speaker;
            speaker.moveTo(5, 8.75);
            speaker.lineTo(6.5, 8.75);
            speaker.lineTo(8.25, 7);
            speaker.lineTo(8.25, 12.5);
            speaker.lineTo(6.5, 10.75);
            speaker.lineTo(5, 10.75);
            speaker.closeSubpath();
            painter->setPen(Qt::NoPen);
            painter->setBrush(mark);
            painter->drawPath(speaker);
            painter->setPen(markPen);
            painter->setBrush(Qt::NoBrush);
            painter->drawArc(QRectF(7.5, 7.5, 3.5, 4.5), -60 * 16, 120 * 16);
            break;
        }
        case Icons::FileKind::Movie:
            painter->setPen(Qt::NoPen);
            painter->setBrush(mark);
            painter->drawPolygon(QPolygonF({ { 5.75, 6.75 }, { 11, 9.75 }, { 5.75, 12.75 } }));
            break;
        case Icons::FileKind::Text:
            painter->setPen(markPen);
            painter->drawLine(QPointF(5.5, 7.5), QPointF(10.5, 7.5));
            painter->drawLine(QPointF(5.5, 9.75), QPointF(10.5, 9.75));
            painter->drawLine(QPointF(5.5, 12), QPointF(8.75, 12));
            break;
        case Icons::FileKind::Mission:
            // A flag
            painter->setPen(markPen);
            painter->drawLine(QPointF(5.75, 6.5), QPointF(5.75, 13));
            painter->setPen(Qt::NoPen);
            painter->setBrush(mark);
            painter->drawPolygon(QPolygonF({ { 5.75, 6.5 }, { 11, 8.25 }, { 5.75, 10 } }));
            break;
        case Icons::FileKind::Model: {
            // A cube
            const QPointF center(8, 9.75);
            const QPolygonF hexagon({ { 8, 6.5 }, { 10.75, 8.1 }, { 10.75, 11.4 }, { 8, 13 }, { 5.25, 11.4 }, { 5.25, 8.1 } });
            painter->setPen(markPen);
            painter->setBrush(Qt::NoBrush);
            painter->drawPolygon(hexagon);
            painter->drawLine(center, hexagon[3]);
            painter->drawLine(center, hexagon[1]);
            painter->drawLine(center, hexagon[5]);
            break;
        }
        case Icons::FileKind::Script:
            // < >
            painter->setPen(markPen);
            painter->drawPolyline(QPolygonF({ { 7, 7.25 }, { 5, 9.75 }, { 7, 12.25 } }));
            painter->drawPolyline(QPolygonF({ { 9, 7.25 }, { 11, 9.75 }, { 9, 12.25 } }));
            break;
        case Icons::FileKind::Font: {
            QFont font = painter->font();
            font.setPixelSize(8);
            font.setBold(true);
            painter->setFont(font);
            painter->setPen(mark);
            painter->drawText(QRectF(3.5, 5.5, 9, 8), Qt::AlignCenter, QStringLiteral("A"));
            break;
        }
        case Icons::FileKind::Other:
            break;
        }
        painter->restore();
    }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override
    {
        QPixmap pixmap(size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        paint(&painter, QRect(QPoint(0, 0), size), mode, state);
        return pixmap;
    }

    QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State state, qreal scale) override
    {
        QPixmap result = pixmap(size * scale, mode, state);
        result.setDevicePixelRatio(scale);
        return result;
    }

private:
    Icons::FileKind m_kind;
};

} // namespace

QIcon Icons::fileIcon(FileKind kind)
{
    return QIcon(new FileIconEngine(kind));
}

QIcon Icons::icon(Shape shape)
{
    return QIcon(new ShapeIconEngine(shape));
}
