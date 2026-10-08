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

} // namespace

QIcon Icons::icon(Shape shape)
{
    return QIcon(new ShapeIconEngine(shape));
}
