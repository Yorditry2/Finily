#include "ui/home/HomeTheme.h"

#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPixmap>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace finily {

QIcon homeGlyph(const QString& kind, const QColor& color) {
    QPixmap pixmap(40, 40);
    pixmap.setDevicePixelRatio(2.0);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(color, 1.65, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);

    if (kind == QLatin1String("all")) {
        p.drawRoundedRect(QRectF(3.5, 3.5, 5.5, 5.5), 1.5, 1.5);
        p.drawRoundedRect(QRectF(11.0, 3.5, 5.5, 5.5), 1.5, 1.5);
        p.drawRoundedRect(QRectF(3.5, 11.0, 5.5, 5.5), 1.5, 1.5);
        p.drawRoundedRect(QRectF(11.0, 11.0, 5.5, 5.5), 1.5, 1.5);
    } else if (kind == QLatin1String("recent")) {
        p.drawEllipse(QPointF(10, 10), 6.8, 6.8);
        p.drawLine(QPointF(10, 6.2), QPointF(10, 10.2));
        p.drawLine(QPointF(10, 10.2), QPointF(13.0, 11.8));
    } else if (kind == QLatin1String("search")) {
        p.drawEllipse(QPointF(8.8, 8.8), 5.2, 5.2);
        p.drawLine(QPointF(12.8, 12.8), QPointF(16.6, 16.6));
    } else if (kind == QLatin1String("bolt")) {
        QPainterPath z;
        z.moveTo(11.2, 2.8); z.lineTo(5.2, 11.0); z.lineTo(9.8, 11.0);
        z.lineTo(8.8, 17.2); z.lineTo(14.8, 9.0); z.lineTo(10.2, 9.0);
        z.closeSubpath();
        p.drawPath(z);
    } else if (kind == QLatin1String("plus")) {
        p.drawLine(QPointF(10, 4.5), QPointF(10, 15.5));
        p.drawLine(QPointF(4.5, 10), QPointF(15.5, 10));
    } else if (kind == QLatin1String("board")) {
        p.drawRoundedRect(QRectF(2.8, 4.2, 14.4, 11.6), 2.4, 2.4);
        QPainterPath wave;
        wave.moveTo(5.4, 12.2);
        wave.cubicTo(7.8, 7.2, 11.4, 13.0, 14.6, 8.0);
        p.drawPath(wave);
    } else if (kind == QLatin1String("notebook")) {
        p.drawRoundedRect(QRectF(4.0, 2.8, 12.0, 14.4), 2.0, 2.0);
        p.drawLine(QPointF(7.2, 3.0), QPointF(7.2, 17.0));
        p.drawLine(QPointF(9.5, 7.5), QPointF(13.5, 7.5));
        p.drawLine(QPointF(9.5, 10.5), QPointF(13.5, 10.5));
    } else if (kind == QLatin1String("pdf")) {
        QPainterPath doc;
        doc.moveTo(4.8, 2.6); doc.lineTo(11.8, 2.6); doc.lineTo(15.6, 6.4);
        doc.lineTo(15.6, 17.4); doc.lineTo(4.8, 17.4); doc.closeSubpath();
        p.drawPath(doc);
        p.drawLine(QPointF(10.2, 8.2), QPointF(10.2, 13.8));
        p.drawLine(QPointF(7.8, 11.5), QPointF(10.2, 13.8));
        p.drawLine(QPointF(12.6, 11.5), QPointF(10.2, 13.8));
    } else if (kind == QLatin1String("folder")) {
        QPainterPath f;
        f.moveTo(2.8, 5.2); f.lineTo(7.8, 5.2); f.lineTo(9.6, 7.2);
        f.lineTo(17.2, 7.2); f.lineTo(17.2, 15.8); f.lineTo(2.8, 15.8); f.closeSubpath();
        p.drawPath(f);
    } else if (kind == QLatin1String("restore")) {
        p.drawArc(QRectF(3.5, 3.5, 13, 13), 100 * 16, 290 * 16);
        p.drawLine(QPointF(8.2, 3.6), QPointF(5.4, 2.0));
        p.drawLine(QPointF(8.2, 3.6), QPointF(6.4, 6.4));
        p.drawLine(QPointF(10, 7.0), QPointF(10, 10.5));
        p.drawLine(QPointF(10, 10.5), QPointF(12.5, 12.0));
    } else if (kind == QLatin1String("moon")) {
        QPainterPath moon, bite;
        moon.addEllipse(QPointF(10, 10), 6.8, 6.8);
        bite.addEllipse(QPointF(13.8, 6.5), 5.8, 5.8);
        p.drawPath(moon.subtracted(bite));
    } else if (kind == QLatin1String("sun")) {
        p.drawEllipse(QPointF(10, 10), 3.4, 3.4);
        for (int i = 0; i < 8; ++i) {
            const qreal a = i * std::numbers::pi / 4.0;
            p.drawLine(QPointF(10 + std::cos(a) * 6.0, 10 + std::sin(a) * 6.0),
                       QPointF(10 + std::cos(a) * 8.0, 10 + std::sin(a) * 8.0));
        }
    } else if (kind == QLatin1String("more")) {
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        for (int i = 0; i < 3; ++i)
            p.drawEllipse(QPointF(5.0 + i * 5.0, 10.0), 1.5, 1.5);
    }
    return QIcon(pixmap);
}

QIcon symbolGlyph(int iconIndex, const QColor& color) {
    QPixmap pixmap(44, 44);
    pixmap.setDevicePixelRatio(2.0);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(color, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);

    switch (std::clamp(iconIndex, 0, 7)) {
        case 0:
            p.drawLine(QPointF(7.0, 16.0), QPointF(15.0, 6.0));
            break;
        case 1:
            p.drawRoundedRect(QRectF(4.5, 3.5, 13.0, 15.0), 2.0, 2.0);
            p.drawLine(QPointF(8.0, 3.8), QPointF(8.0, 18.2));
            p.drawLine(QPointF(10.5, 8.2), QPointF(14.8, 8.2));
            p.drawLine(QPointF(10.5, 11.4), QPointF(14.8, 11.4));
            break;
        case 2: {
            QPainterPath sig;
            sig.moveTo(15.5, 5.2); sig.lineTo(6.5, 5.2); sig.lineTo(12.0, 11.0);
            sig.lineTo(6.5, 16.8); sig.lineTo(15.5, 16.8);
            p.drawPath(sig);
            break;
        }
        case 3:
            p.save();
            p.translate(11.0, 11.0);
            for (int r : {0, 60, 120}) {
                p.save();
                p.rotate(r);
                p.drawEllipse(QPointF(0, 0), 7.6, 3.2);
                p.restore();
            }
            p.setPen(Qt::NoPen);
            p.setBrush(color);
            p.drawEllipse(QPointF(0, 0), 1.6, 1.6);
            p.restore();
            break;
        case 4: {
            QPainterPath tri;
            tri.moveTo(5.0, 17.0); tri.lineTo(5.0, 5.0); tri.lineTo(17.0, 17.0); tri.closeSubpath();
            p.drawPath(tri);
            p.drawRect(QRectF(5.0, 13.5, 3.5, 3.5));
            break;
        }
        case 5: {
            p.drawLine(QPointF(6.2, 16.8), QPointF(15.6, 7.4));
            p.drawLine(QPointF(13.4, 5.2), QPointF(16.8, 8.6));
            p.drawLine(QPointF(6.2, 16.8), QPointF(4.8, 17.6));
            p.drawLine(QPointF(4.8, 17.6), QPointF(5.6, 14.8));
            break;
        }
        case 6: {
            QPainterPath star;
            for (int i = 0; i < 10; ++i) {
                const qreal rad = (i % 2 == 0) ? 7.6 : 3.4;
                const qreal ang = -std::numbers::pi / 2.0 + i * std::numbers::pi / 5.0;
                const QPointF pt(11.0 + std::cos(ang) * rad, 11.3 + std::sin(ang) * rad);
                if (i == 0) star.moveTo(pt); else star.lineTo(pt);
            }
            star.closeSubpath();
            p.drawPath(star);
            break;
        }
        case 7:
            p.drawArc(QRectF(6.6, 4.0, 8.8, 8.8), -38 * 16, 256 * 16);
            p.drawLine(QPointF(8.8, 14.2), QPointF(13.2, 14.2));
            p.drawLine(QPointF(9.4, 16.8), QPointF(12.6, 16.8));
            p.drawLine(QPointF(11.0, 11.6), QPointF(11.0, 14.0));
            break;
    }
    return QIcon(pixmap);
}

} // namespace finily
