#include "ui/UiIcons.h"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QPainter>
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

} // namespace finily
