#include "ui/UiIcons.h"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QStandardPaths>
#include <QSvgRenderer>

namespace finily {

namespace {

QByteArray loadSvgSource(const QString& pack, const QString& name) {
    static QHash<QString, QByteArray> cache;
    const QString key = pack + QLatin1Char('/') + name;
    auto it = cache.constFind(key);
    if (it != cache.constEnd())
        return *it;

    const QString relative = QStringLiteral("icons/%1/%2.svg").arg(pack, name);
    QByteArray data;
    const QString userFile =
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(relative);
    for (const QString& path : {userFile, QStringLiteral(":/resources/") + relative}) {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) {
            data = file.readAll();
            break;
        }
    }
    cache.insert(key, data);
    return data;
}

} // namespace

void paintUiIcon(QPainter* painter, const QRectF& rect, const QString& name, const QColor& color,
                 const QString& pack) {
    QByteArray svg = loadSvgSource(pack, name);
    if (svg.isEmpty() || !painter)
        return;
    QColor solid = color;
    const qreal alpha = solid.alphaF();
    solid.setAlpha(255);
    svg.replace("currentColor", solid.name(QColor::HexRgb).toLatin1());
    QSvgRenderer renderer(svg);
    painter->save();
    painter->setOpacity(painter->opacity() * alpha);
    renderer.render(painter, rect);
    painter->restore();
}

QIcon uiIcon(const QString& name, const QColor& color, int logicalSize, const QString& pack) {
    constexpr qreal kScale = 2.0;
    QPixmap pixmap(int(logicalSize * kScale), int(logicalSize * kScale));
    pixmap.setDevicePixelRatio(kScale);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    paintUiIcon(&painter, QRectF(0, 0, logicalSize, logicalSize), name, color, pack);
    painter.end();
    return QIcon(pixmap);
}

void paintDynamicPenIcon(QPainter* painter, const QRectF& rect,
                         const QColor& strokeColor, const QColor& nibColor) {
    if (!painter || rect.isEmpty())
        return;
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->translate(rect.topLeft());
    painter->scale(rect.width() / 24.0, rect.height() / 24.0);

    QPainterPath body;
    body.moveTo(15.5, 3.5);
    body.lineTo(20.5, 8.5);
    body.lineTo(9.0, 20.0);
    body.lineTo(4.0, 20.0);
    body.lineTo(4.0, 15.0);
    body.closeSubpath();

    QPen bodyPen(strokeColor, 1.65, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter->setPen(bodyPen);
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(body);
    painter->drawLine(QPointF(13.0, 6.0), QPointF(18.0, 11.0));

    QPainterPath nib;
    nib.moveTo(4.0, 20.0);
    nib.lineTo(7.4, 19.4);
    nib.lineTo(4.6, 16.6);
    nib.closeSubpath();
    painter->setPen(QPen(nibColor, 1.1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->setBrush(nibColor);
    painter->drawPath(nib);

    painter->restore();
}

void paintDynamicHighlighterIcon(QPainter* painter, const QRectF& rect,
                                 const QColor& strokeColor, const QColor& barColor) {
    if (!painter || rect.isEmpty())
        return;
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->translate(rect.topLeft());
    painter->scale(rect.width() / 24.0, rect.height() / 24.0);

    QPainterPath barrel;
    barrel.moveTo(14.0, 3.5);
    barrel.lineTo(20.5, 10.0);
    barrel.lineTo(13.5, 17.0);
    barrel.lineTo(7.0, 10.5);
    barrel.closeSubpath();

    QPainterPath tip;
    tip.moveTo(7.0, 10.5);
    tip.lineTo(13.5, 17.0);
    tip.lineTo(11.5, 19.0);
    tip.lineTo(8.0, 19.0);
    tip.lineTo(5.5, 16.5);
    tip.lineTo(5.5, 13.0);
    tip.closeSubpath();

    QPen pen(strokeColor, 1.65, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(barrel);
    painter->drawPath(tip);

    QPen barPen(barColor, 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter->setPen(barPen);
    painter->drawLine(QPointF(4.0, 21.0), QPointF(11.5, 21.0));

    painter->restore();
}

} // namespace finily
