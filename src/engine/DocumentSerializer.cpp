#include "engine/DocumentSerializer.h"

#include "engine/GeometryEngine.h"
#include "engine/InkEngine.h"
#include "engine/PdfEngine.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontMetricsF>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <utility>

namespace finily {
namespace {

bool finite(double v) { return std::isfinite(v); }

QString freshId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

QFont textFont(double size) {
    QFont font(QStringLiteral("Sans Serif"));
    font.setPixelSize(qRound(size));
    return font;
}

QRectF textBounds(const TextItem& item) {
    const QFontMetricsF metrics(textFont(item.fontSize));
    const QRectF ink = metrics.boundingRect(item.text).translated(item.pos + QPointF(0, metrics.ascent()));
    return ink.united(QRectF(item.pos, QSizeF(metrics.horizontalAdvance(item.text), metrics.height())));
}

void drawTextItem(QPainter& painter, const TextItem& item) {
    painter.save();
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setFont(textFont(item.fontSize));
    painter.setPen(item.color);
    painter.drawText(item.pos + QPointF(0, QFontMetricsF(painter.font()).ascent()), item.text);
    painter.restore();
}

QJsonObject strokeJson(const Stroke& s) {
    QJsonArray points;
    for (const auto& p : s.points)
        points.append(QJsonArray{p.x, p.y, p.pressure, double(p.timestampMs), p.tiltX, p.tiltY});
    return {{"id", s.id},
            {"groupId", s.groupId},
            {"tool", int(s.tool)},
            {"color", s.color.name(QColor::HexArgb)},
            {"size", s.size},
            {"opacity", s.opacity},
            {"linePattern", int(s.linePattern)},
            {"pressureProfile", int(s.pressureProfile)},
            {"pressureSensitivity", s.pressureSensitivity},
            {"hasHardwarePressure", s.hasHardwarePressure},
            {"tipStyle", s.tipStyle},
            {"points", points}};
}

QJsonObject shapeJson(const ShapeItem& shape) {
    QJsonArray vertices;
    for (auto p : shape.vertices)
        vertices.append(QJsonArray{p.x(), p.y()});
    QJsonObject result{{"id", shape.id},
                       {"groupId", shape.groupId},
                       {"kind", int(shape.kind)},
                       {"color", shape.color.name(QColor::HexArgb)},
                       {"size", shape.size},
                       {"opacity", shape.opacity},
                       {"linePattern", int(shape.linePattern)},
                       {"lineEnding", int(shape.lineEnding)},
                       {"closed", shape.closed},
                       {"locked", shape.locked},
                       {"solidKind", shape.solidKind},
                       {"vertices", vertices}};
    if (!shape.label.isEmpty())
        result.insert("label", shape.label);
    if (shape.kind == ShapeKind::ShadedRegion) {
        result.insert("shadePattern", int(shape.shadePattern));
        if (shape.shadeLayer > 0)
            result.insert("shadeLayer", shape.shadeLayer);
        if (!shape.shadeBoundary.isEmpty()) {
            QJsonObject boundary;
            for (auto it = shape.shadeBoundary.cbegin(); it != shape.shadeBoundary.cend(); ++it)
                boundary.insert(it.key(), it.value());
            result.insert("shadeBoundary", boundary);
            result.insert("shadeProbe", QJsonArray{shape.shadeProbe.x(), shape.shadeProbe.y()});
            result.insert("shadeVisible", shape.shadeVisible);
        }
        if (!shape.shadeContourStarts.isEmpty()) {
            QJsonArray starts;
            for (int start : shape.shadeContourStarts)
                starts.append(start);
            result.insert("shadeContourStarts", starts);
        }
    }
    if (shape.attachmentsKnown) {
        QJsonArray links;
        for (const auto& attachment : shape.attachments)
            links.append(QJsonArray{attachment.vertex, attachment.targetId, attachment.edge, attachment.position});
        result.insert("attachments", links);
    }
    return result;
}

struct PngCache {
    QMutex mutex;
    QHash<qint64, QString> previous, next;
};

PngCache& pngCache() {
    static PngCache cache;
    return cache;
}

bool pageHasContent(const PageData& page) {
    return !page.strokes.isEmpty() || !page.shapes.isEmpty() || !page.images.isEmpty() || !page.texts.isEmpty();
}

constexpr QSize kCoverThumbSize(132, 170);

} // namespace

QString autosavePath() {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/autosave.json");
}

QJsonObject documentSettings(ShapeKind kind, bool hold, bool guides, bool swap,
                             PressureProfile profile, ThemeId theme,
                             BackgroundTemplate tpl, Viewport viewport,
                             PageFormat pageFormat) {
    return {{"shapeKind", int(kind)},
            {"holdToShapeEnabled", hold},
            {"smartGuidesEnabled", guides},
            {"swapPenButtons", swap},
            {"pressureProfile", int(profile)},
            {"theme", int(theme)},
            {"template", int(tpl)},
            {"pageFormat", int(pageFormat)},
            {"viewport", QJsonObject{{"x", viewport.x}, {"y", viewport.y}, {"zoom", viewport.zoom}}}};
}

QJsonObject serializePage(const PageData& document, bool& valid) {
    QJsonArray strokes, shapes, images, texts;
    for (const auto& stroke : document.strokes)
        strokes.append(strokeJson(stroke));
    for (const auto& shape : document.shapes)
        shapes.append(shapeJson(shape));
    for (const auto& item : document.texts)
        texts.append(QJsonObject{{"id", item.id},
                                 {"groupId", item.groupId},
                                 {"pos", QJsonArray{item.pos.x(), item.pos.y()}},
                                 {"text", item.text},
                                 {"color", item.color.name(QColor::HexArgb)},
                                 {"fontSize", item.fontSize},
                                 {"locked", item.locked}});
    for (const auto& image : document.images) {
        QJsonObject value{{"id", image.id},
                          {"groupId", image.groupId},
                          {"rect", QJsonArray{image.rect.x(), image.rect.y(), image.rect.width(), image.rect.height()}},
                          {"isPdfPage", image.isPdfPage},
                          {"pdfPageIndex", image.pdfPageIndex},
                          {"pdfSourcePath", image.pdfSourcePath},
                          {"locked", image.locked}};
        const qint64 key = image.image.cacheKey();
        QString encoded;
        {
            QMutexLocker locker(&pngCache().mutex);
            encoded = pngCache().previous.value(key);
        }
        if (encoded.isEmpty()) {
            QByteArray png;
            QBuffer buffer(&png);
            buffer.open(QIODevice::WriteOnly);
            if (!image.image.save(&buffer, "PNG")) {
                valid = false;
                return {};
            }
            encoded = QString::fromLatin1(png.toBase64());
        }
        {
            QMutexLocker locker(&pngCache().mutex);
            pngCache().next.insert(key, encoded);
        }
        value.insert("png", encoded);
        images.append(value);
    }
    QJsonObject page{{"strokes", strokes},
                     {"shapes", shapes},
                     {"images", images},
                     {"texts", texts},
                     {"viewport", QJsonObject{{"x", document.viewport.x},
                                              {"y", document.viewport.y},
                                              {"zoom", document.viewport.zoom}}}};
    if (document.background)
        page.insert("pageBackground", int(*document.background));
    return page;
}

bool serializeDocumentToFile(const QString& path, const QVector<PageData>& pages,
                             int currentPage, QJsonObject root, bool newFile) {
    static QMutex writeMutex;
    QMutexLocker writeLocker(&writeMutex);

    QJsonArray serialized;
    bool valid = true;
    {
        QMutexLocker locker(&pngCache().mutex);
        pngCache().next.clear();
    }
    for (const auto& page : pages) {
        serialized.append(serializePage(page, valid));
        if (!valid)
            return false;
    }
    if (serialized.isEmpty())
        serialized.append(serializePage(PageData{}, valid));
    {
        QMutexLocker locker(&pngCache().mutex);
        pngCache().previous = std::move(pngCache().next);
        pngCache().next.clear();
    }
    root.remove("viewport");
    root.insert("version", 5);
    root.insert("pages", serialized);
    root.insert("currentPageIndex", currentPage);
    const auto data = QJsonDocument(root).toJson(QJsonDocument::Compact);
    if (newFile) {
        // Escribir en el temporal y publicarlo en la misma carpeta sin reemplazar otro cuaderno.
        QTemporaryFile pending(QFileInfo(path).absolutePath() + QStringLiteral("/.finily-XXXXXX"));
        if (!pending.open() || pending.write(data) != data.size() || !pending.flush()
            || !pending.rename(path))
            return false;
        pending.setAutoRemove(false);
        return true;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    if (file.write(data) != data.size()) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

QJsonObject loadDocumentFromFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return {};
    const auto root = doc.object();
    const int version = root.value("version").toInt();
    if (version < 1 || version > 5)
        return {};
    if (version < 5 && !root.value("strokes").isArray())
        return {};
    if (version == 5) {
        if (!root.value("pages").isArray() || root.value("pages").toArray().isEmpty())
            return {};
        for (const auto& value : root.value("pages").toArray())
            if (!value.isObject() || !value.toObject().value("strokes").isArray())
                return {};
    }
    return root;
}

PageData deserializePage(const QJsonObject& root,
                         QHash<QString, QVector<ImageItem>>& pdfSources) {
    QVector<Stroke> loaded;
    for (const auto& value : root.value("strokes").toArray()) {
        if (!value.isObject())
            continue;
        const auto o = value.toObject();
        Stroke s;
        const int tool = o.value("tool").toInt(-1), pattern = o.value("linePattern").toInt(-1);
        if (tool < 0 || tool > int(ToolType::Highlighter) || pattern < 0 || pattern > int(LinePattern::Dimension))
            continue;
        const int profile = o.value("pressureProfile").toInt(int(PressureProfile::Light));
        if (profile < int(PressureProfile::Light) || profile > int(PressureProfile::Firm))
            continue;
        s.pressureProfile = PressureProfile(profile);
        s.pressureSensitivity = std::clamp(o.value("pressureSensitivity").toDouble(0.5), 0.0, 1.0);
        s.tool = ToolType(tool);
        s.linePattern = LinePattern(pattern);
        s.id = o.value("id").toString();
        s.groupId = o.value("groupId").toString();
        s.tipStyle = std::clamp(o.value("tipStyle").toInt(s.tool == ToolType::Pen ? 1 : 0),
                                0, s.tool == ToolType::Highlighter ? 2 : 3);
        if (s.id.isEmpty())
            s.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        s.color = QColor(o.value("color").toString());
        s.size = o.value("size").toDouble(2.6);
        s.opacity = o.value("opacity").toDouble(1.0);
        if (!s.color.isValid() || !finite(s.size) || s.size <= 0 || s.size > 200 || !finite(s.opacity))
            continue;
        s.opacity = std::clamp(s.opacity, 0.0, 1.0);
        s.hasHardwarePressure = o.value("hasHardwarePressure").toBool();
        bool valid = true;
        for (const auto& pv : o.value("points").toArray()) {
            const auto p = pv.toArray();
            if (p.size() != 4 && p.size() != 6) {
                valid = false;
                break;
            }
            for (const auto& n : p)
                if (!n.isDouble() || !finite(n.toDouble()))
                    valid = false;
            if (!valid)
                break;
            const double stamp = p[3].toDouble();
            if (std::abs(stamp) > 9e15) {
                valid = false;
                break;
            }
            s.points.append({p[0].toDouble(), p[1].toDouble(), std::clamp(p[2].toDouble(), 0.0, 1.0),
                             qint64(stamp), p.size() == 6 ? std::clamp(p[4].toDouble(), -60.0, 60.0) : 0.0,
                             p.size() == 6 ? std::clamp(p[5].toDouble(), -60.0, 60.0) : 0.0});
        }
        if (valid && !s.points.isEmpty()) {
            s.bounds = computeBounds(s.points, s.size * 0.7);
            loaded.append(s);
        }
    }
    QVector<ShapeItem> loadedShapes;
    for (const auto& value : root.value("shapes").toArray()) {
        if (!value.isObject())
            continue;
        const auto o = value.toObject();
        const int kind = o.value("kind").toInt(-1), pattern = o.value("linePattern").toInt(0);
        if (kind < 0 || kind > int(ShapeKind::AngleMarker) || pattern < 0 || pattern > int(LinePattern::Dimension))
            continue;
        ShapeItem shape;
        shape.kind = ShapeKind(kind);
        shape.linePattern = LinePattern(pattern);
        shape.locked = o.value("locked").toBool(false);
        shape.lineEnding = LineEnding(std::clamp(o.value("lineEnding").toInt(), 0, 2));
        if (shape.linePattern == LinePattern::Arrow || shape.linePattern == LinePattern::DoubleArrow) {
            shape.lineEnding = shape.linePattern == LinePattern::Arrow ? LineEnding::Arrow : LineEnding::DoubleArrow;
            shape.linePattern = LinePattern::Solid;
        }
        shape.label = o.value("label").toString().left(80);
        shape.solidKind = std::clamp(o.value("solidKind").toInt(0), 0, 9);
        shape.id = o.value("id").toString();
        if (shape.id.isEmpty())
            shape.id = freshId();
        shape.groupId = o.value("groupId").toString();
        shape.color = QColor(o.value("color").toString());
        shape.size = o.value("size").toDouble(2.6);
        shape.opacity = o.value("opacity").toDouble(1);
        if (!shape.color.isValid() || !finite(shape.size) || shape.size <= 0 || shape.size > 200 || !finite(shape.opacity))
            continue;
        shape.opacity = std::clamp(shape.opacity, 0.0, 1.0);
        shape.closed = o.value("closed").toBool();
        bool valid = true;
        for (const auto& vertex : o.value("vertices").toArray()) {
            const auto a = vertex.toArray();
            if (a.size() != 2 || !a[0].isDouble() || !a[1].isDouble() || !finite(a[0].toDouble()) || !finite(a[1].toDouble())) {
                valid = false;
                break;
            }
            shape.vertices.append({a[0].toDouble(), a[1].toDouble()});
        }
        const bool threeVertices = shape.kind == ShapeKind::Triangle || shape.kind == ShapeKind::Radical
            || shape.kind == ShapeKind::Curve || shape.kind == ShapeKind::RightAngle
            || shape.kind == ShapeKind::Arc || shape.kind == ShapeKind::Sector || shape.kind == ShapeKind::AngleMarker;
        const qsizetype expected = threeVertices ? 3 : 2;
        if (!valid)
            continue;
        if (shape.kind == ShapeKind::ShadedRegion) {
            const int shade = o.value("shadePattern").toInt(0);
            if (shade < int(ShadePattern::Soft) || shade > int(ShadePattern::Grid) || !shape.closed)
                continue;
            int previous = 0;
            for (const auto& offset : o.value("shadeContourStarts").toArray()) {
                const int start = offset.toInt(-1);
                if (start <= previous || start >= shape.vertices.size()) {
                    valid = false;
                    break;
                }
                const int count = start - previous;
                if (count < 7 || (count - 1) % 3 != 0) {
                    valid = false;
                    break;
                }
                shape.shadeContourStarts.append(start);
                previous = start;
            }
            const auto lastCount = shape.vertices.size() - previous;
            if (!valid || lastCount < 7 || (lastCount - 1) % 3 != 0)
                continue;
            shape.shadePattern = ShadePattern(shade);
            shape.shadeLayer = std::clamp(o.value("shadeLayer").toInt(0), 0, 2);
            const auto boundary = o.value("shadeBoundary").toObject();
            const auto probe = o.value("shadeProbe").toArray();
            if (probe.size() == 2 && probe[0].isDouble() && probe[1].isDouble()
                && finite(probe[0].toDouble()) && finite(probe[1].toDouble())) {
                for (auto it = boundary.constBegin(); it != boundary.constEnd(); ++it) {
                    const int side = it.value().toInt();
                    if (!it.key().isEmpty() && side >= 1 && side <= 3)
                        shape.shadeBoundary.insert(it.key(), side);
                }
                shape.shadeProbe = {probe[0].toDouble(), probe[1].toDouble()};
                shape.shadeVisible = o.value("shadeVisible").toBool(true);
            }
        } else if (shape.vertices.size() < expected || (shape.kind != ShapeKind::Polyline && shape.vertices.size() != expected)) {
            continue;
        }
        const auto links = o.value("attachments");
        shape.attachmentsKnown = links.isArray();
        for (const auto& link : links.toArray()) {
            const auto fields = link.toArray();
            if (fields.size() != 4 || !fields[3].isDouble())
                continue;
            const int vertex = fields[0].toInt(-1), edge = fields[2].toInt(-4);
            const auto target = fields[1].toString();
            const double position = fields[3].toDouble();
            const bool validVertex = (vertex == 0 || vertex == shape.vertices.size() - 1
                                      || (shape.kind == ShapeKind::AngleMarker && vertex >= 0 && vertex < shape.vertices.size()));
            if (validVertex && !target.isEmpty() && target != shape.id
                && edge >= -3 && finite(position) && (edge == -2 || (position >= 0 && position <= 1)))
                shape.attachments.append({vertex, target, edge, position});
        }
        if (shape.kind == ShapeKind::Radical)
            shape.vertices[2].setY(shape.vertices[1].y());
        shape.bounds = computeShapeBounds(shape);
        loadedShapes.append(shape);
    }
    QVector<ImageItem> loadedImages;
    for (const auto& value : root.value("images").toArray()) {
        const auto o = value.toObject();
        const auto r = o.value("rect").toArray();
        if (r.size() != 4)
            continue;
        bool valid = true;
        for (const auto& number : r)
            if (!number.isDouble() || !finite(number.toDouble()))
                valid = false;
        if (!valid || r[2].toDouble() <= 0 || r[3].toDouble() <= 0)
            continue;
        ImageItem image;
        image.id = o.value("id").toString();
        if (image.id.isEmpty())
            image.id = freshId();
        image.rect = QRectF(r[0].toDouble(), r[1].toDouble(), r[2].toDouble(), r[3].toDouble());
        image.isPdfPage = o.value("isPdfPage").toBool();
        if (!image.isPdfPage)
            image.groupId = o.value("groupId").toString();
        image.locked = o.value("locked").toBool();
        image.pdfSourcePath = o.value("pdfSourcePath").toString();
        image.pdfPageIndex = o.value("pdfPageIndex").toInt(-1);
        image.image = QImage::fromData(QByteArray::fromBase64(o.value("png").toString().toLatin1()), "PNG");
        if (image.isPdfPage && image.image.isNull()) {
            if (!QFileInfo::exists(image.pdfSourcePath))
                continue;
            if (!pdfSources.contains(image.pdfSourcePath))
                pdfSources.insert(image.pdfSourcePath, importPdfPages(image.pdfSourcePath));
            for (const auto& page : pdfSources[image.pdfSourcePath])
                if (page.pdfPageIndex == image.pdfPageIndex) {
                    image.image = page.image;
                    break;
                }
        }
        if (!image.image.isNull())
            loadedImages.append(image);
    }
    QVector<TextItem> loadedTexts;
    for (const auto& value : root.value("texts").toArray()) {
        const auto o = value.toObject();
        const auto pos = o.value("pos").toArray();
        if (pos.size() != 2 || !pos[0].isDouble() || !pos[1].isDouble() || !finite(pos[0].toDouble()) || !finite(pos[1].toDouble()))
            continue;
        TextItem item;
        item.id = o.value("id").toString();
        if (item.id.isEmpty())
            item.id = freshId();
        item.groupId = o.value("groupId").toString();
        item.locked = o.value("locked").toBool();
        item.pos = {pos[0].toDouble(), pos[1].toDouble()};
        item.text = o.value("text").toString();
        item.color = QColor(o.value("color").toString());
        item.fontSize = o.value("fontSize").toDouble(22);
        if (item.text.trimmed().isEmpty() || !item.color.isValid() || !finite(item.fontSize) || item.fontSize < 1 || item.fontSize > 1000)
            continue;
        item.bounds = textBounds(item);
        loadedTexts.append(item);
    }
    initializeShapeLinks(loadedShapes);
    const auto view = root.value("viewport").toObject();
    const double x = view.value("x").toDouble(), y = view.value("y").toDouble(), zoom = view.value("zoom").toDouble(1);
    Viewport viewport;
    if (finite(x) && finite(y) && finite(zoom))
        viewport = {x, y, std::clamp(zoom, .2, 5.0)};
    std::optional<BackgroundTemplate> background;
    const int storedBackground = root.value("pageBackground").toInt(-1);
    if (storedBackground >= int(BackgroundTemplate::Blank) && storedBackground <= int(BackgroundTemplate::Ruled))
        background = BackgroundTemplate(storedBackground);
    return {loaded, loadedShapes, loadedImages, loadedTexts, viewport, background};
}

QImage renderPageThumbnail(const PageData& page, ThemeId themeId,
                           BackgroundTemplate fallbackTpl, PageFormat pageFormat,
                           const QSize& size) {
    QImage image(size * 2, QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(2.0);
    const auto theme = getThemeConfig(themeId);
    image.fill(theme.background);

    const auto& strokes = page.strokes;
    const auto& shapes = page.shapes;
    const auto& images = page.images;
    const auto& texts = page.texts;
    QRectF bounds = sheetRectForPageFormat(pageFormat);
    for (const auto& item : images) {
        if (item.isPdfPage) {
            bounds = item.rect;
            break;
        }
    }
    for (const auto& item : images)
        bounds = bounds.united(item.rect);
    for (const auto& item : strokes)
        bounds = bounds.united(item.bounds);
    for (const auto& item : shapes)
        bounds = bounds.united(item.bounds);
    for (const auto& item : texts)
        bounds = bounds.united(item.bounds);
    if (bounds.isEmpty())
        bounds = QRectF(0, 0, 1000, 700);
    else if (sheetRectForPageFormat(pageFormat).isEmpty())
        bounds = bounds.adjusted(-28, -28, 28, 28);

    const double zoom = std::min((size.width() - 8.0) / std::max(1.0, bounds.width()),
                                 (size.height() - 8.0) / std::max(1.0, bounds.height()));
    if (zoom <= 0)
        return image;

    QPainter painter(&image);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    painter.translate(size.width() / 2.0, size.height() / 2.0);
    painter.scale(zoom, zoom);
    painter.translate(-bounds.center());

    const QRectF visibleWorld(bounds.center().x() - size.width() / (2.0 * zoom),
                              bounds.center().y() - size.height() / (2.0 * zoom),
                              size.width() / zoom, size.height() / zoom);
    drawBackgroundTemplate(painter, page.background.value_or(fallbackTpl), theme, Viewport{0, 0, zoom}, visibleWorld);
    for (bool pdf : {true, false})
        for (const auto& item : images)
            if (item.isPdfPage == pdf)
                painter.drawImage(item.rect, item.image);
    for (const auto& shape : shapes)
        if (shape.kind == ShapeKind::ShadedRegion)
            drawShape(painter, shape);
    for (const auto& stroke : strokes)
        if (stroke.tool == ToolType::Highlighter)
            drawStroke(painter, stroke);
    for (const auto& shape : shapes)
        if (shape.kind != ShapeKind::ShadedRegion)
            drawShape(painter, shape);
    for (const auto& stroke : strokes)
        if (stroke.tool != ToolType::Highlighter)
            drawStroke(painter, stroke);
    for (const auto& item : texts)
        drawTextItem(painter, item);
    return image;
}

QString coverThumbnailPath(const QString& notebookPath) {
    const QString abs = QFileInfo(notebookPath).absoluteFilePath();
    const QByteArray hash = QCryptographicHash::hash(abs.toUtf8(), QCryptographicHash::Sha1).toHex();
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + QStringLiteral("/thumbs/") + QString::fromLatin1(hash) + QStringLiteral(".png");
}

void saveCoverThumbnail(const QString& notebookPath, const QImage& thumbnail) {
    if (notebookPath.isEmpty())
        return;
    static QMutex thumbMutex;
    QMutexLocker locker(&thumbMutex);
    const QString thumbPath = coverThumbnailPath(notebookPath);
    if (thumbnail.isNull()) {
        QFile::remove(thumbPath);
        return;
    }
    QDir().mkpath(QFileInfo(thumbPath).absolutePath());
    QSaveFile file(thumbPath);
    if (!file.open(QIODevice::WriteOnly))
        return;
    if (!thumbnail.save(&file, "PNG")) {
        file.cancelWriting();
        return;
    }
    file.commit();
}

void updateCoverThumbnailForPages(const QString& notebookPath, const QVector<PageData>& pages,
                                  ThemeId themeId, BackgroundTemplate fallbackTpl,
                                  PageFormat pageFormat) {
    if (notebookPath.isEmpty())
        return;
    if (pages.isEmpty() || !pageHasContent(pages.first())) {
        saveCoverThumbnail(notebookPath, QImage());
        return;
    }
    const QImage thumb = renderPageThumbnail(pages.first(), themeId, fallbackTpl, pageFormat, kCoverThumbSize);
    saveCoverThumbnail(notebookPath, thumb);
}

void moveCoverThumbnail(const QString& oldNotebookPath, const QString& newNotebookPath) {
    if (oldNotebookPath.isEmpty() || newNotebookPath.isEmpty())
        return;
    const QString oldThumb = coverThumbnailPath(oldNotebookPath);
    const QString newThumb = coverThumbnailPath(newNotebookPath);
    if (QFileInfo::exists(oldThumb)) {
        QDir().mkpath(QFileInfo(newThumb).absolutePath());
        QFile::remove(newThumb);
        QFile::rename(oldThumb, newThumb);
    }
}

void removeCoverThumbnail(const QString& notebookPath) {
    if (!notebookPath.isEmpty())
        QFile::remove(coverThumbnailPath(notebookPath));
}

QImage loadOrGenerateCoverThumbnail(const QString& notebookPath, const QDateTime& notebookModified) {
    if (notebookPath.isEmpty())
        return {};
    const QString thumbPath = coverThumbnailPath(notebookPath);
    const QFileInfo thumbInfo(thumbPath);
    if (thumbInfo.exists() && (!notebookModified.isValid() || thumbInfo.lastModified() >= notebookModified.addSecs(-2))) {
        QImage cached(thumbPath);
        if (!cached.isNull()) {
            cached.setDevicePixelRatio(2.0);
            return cached;
        }
    }

    const QJsonObject root = loadDocumentFromFile(notebookPath);
    if (root.isEmpty())
        return {};
    const int themeInt = std::clamp(root.value("theme").toInt(0), 0, int(ThemeId::PaperCream));
    const int tplInt = std::clamp(root.value("template").toInt(int(BackgroundTemplate::Dots)),
                                  0, int(BackgroundTemplate::Ruled));
    const int fmtInt = std::clamp(root.value("pageFormat").toInt(int(PageFormat::Infinite)),
                                  0, int(PageFormat::Slide16x9));

    QJsonObject firstPageObj = root;
    if (root.value("version").toInt() == 5) {
        const QJsonArray pagesArr = root.value("pages").toArray();
        if (pagesArr.isEmpty() || !pagesArr.first().isObject())
            return {};
        firstPageObj = pagesArr.first().toObject();
    }
    QHash<QString, QVector<ImageItem>> pdfSources;
    const PageData firstPage = deserializePage(firstPageObj, pdfSources);
    if (!pageHasContent(firstPage)) {
        QFile::remove(thumbPath);
        return {};
    }
    const QImage generated = renderPageThumbnail(firstPage, static_cast<ThemeId>(themeInt),
                                                 static_cast<BackgroundTemplate>(tplInt),
                                                 static_cast<PageFormat>(fmtInt), kCoverThumbSize);
    saveCoverThumbnail(notebookPath, generated);
    return generated;
}

} // namespace finily
