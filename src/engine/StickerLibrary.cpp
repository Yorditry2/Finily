#include "engine/StickerLibrary.h"

#include "core/ShapeProtocols.h"
#include "core/Utils.h"
#include "engine/GeometryEngine.h"
#include "engine/InkEngine.h"
#include "engine/StickerCatalogInternal.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>
#include <utility>

namespace finily {
namespace {

QColor resolveSemanticColor(const QColor& c, const ThemeConfig& theme, bool isCustom) {
    if (isCustom)
        return c;
    const QString hex = c.name(QColor::HexRgb).toLower();
    QColor mapped = c;
    if (hex == QLatin1String("#f8fafc")) {
        mapped = theme.isDark ? QColor(QStringLiteral("#f1f5f9")) : QColor(QStringLiteral("#1e293b"));
    } else if (hex == QLatin1String("#38bdf8")) {
        mapped = theme.isDark ? QColor(QStringLiteral("#38bdf8")) : QColor(QStringLiteral("#0284c7"));
    } else if (hex == QLatin1String("#facc15")) {
        mapped = theme.isDark ? QColor(QStringLiteral("#facc15")) : QColor(QStringLiteral("#d97706"));
    } else if (hex == QLatin1String("#4ade80")) {
        mapped = theme.isDark ? QColor(QStringLiteral("#4ade80")) : QColor(QStringLiteral("#16a34a"));
    }
    mapped.setAlpha(c.alpha());
    return mapped;
}

TextItem makeBannerText(const QString& text,
                        const QColor& color,
                        double fontSize,
                        double centerX,
                        double yEdge,
                        bool placeAbove) {
    const double safeFontSize = std::max(8.0, fontSize);
    const QFontMetricsF metrics(textFont(safeFontSize));
    TextItem item;
    item.text = text;
    item.color = color;
    item.fontSize = safeFontSize;
    const double posY = placeAbove ? (yEdge - metrics.height()) : yEdge;
    item.pos = QPointF(centerX - metrics.horizontalAdvance(text) * 0.5, posY);
    item.bounds = textBounds(item);
    return item;
}

QVector<StickerTemplate> buildBuiltinTemplates() {
    QVector<StickerTemplate> list;
    list.reserve(sticker_detail::kBuiltinStickerCount);
    sticker_detail::appendGeometryStickers(list);
    sticker_detail::appendScienceStickers(list);
    return list;
}

const QVector<StickerTemplate>& builtinTemplates() {
    static const QVector<StickerTemplate> builtins = buildBuiltinTemplates();
    return builtins;
}

// ---------------------------------------------------------------------------
// Persistencia JSON de stickers personalizados (stickers.json)
// ---------------------------------------------------------------------------
QString customStickersFilePath() {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/stickers.json");
}

QJsonObject serializeStroke(const Stroke& s) {
    QJsonObject o;
    o[QStringLiteral("tool")] = static_cast<int>(s.tool);
    o[QStringLiteral("color")] = s.color.name(QColor::HexArgb);
    o[QStringLiteral("size")] = s.size;
    o[QStringLiteral("opacity")] = s.opacity;
    o[QStringLiteral("linePattern")] = static_cast<int>(s.linePattern);
    o[QStringLiteral("pressureProfile")] = static_cast<int>(s.pressureProfile);
    o[QStringLiteral("pressureSensitivity")] = s.pressureSensitivity;
    o[QStringLiteral("hasHardwarePressure")] = s.hasHardwarePressure;
    o[QStringLiteral("tipStyle")] = s.tipStyle;
    QJsonArray pts;
    for (const auto& p : s.points) {
        QJsonArray pa;
        pa.append(p.x);
        pa.append(p.y);
        pa.append(p.pressure);
        pa.append(static_cast<double>(p.timestampMs));
        pa.append(p.tiltX);
        pa.append(p.tiltY);
        pts.append(pa);
    }
    o[QStringLiteral("points")] = pts;
    return o;
}

Stroke deserializeStroke(const QJsonObject& o) {
    Stroke s;
    const int rawTool = o[QStringLiteral("tool")].toInt(0);
    s.tool = static_cast<ToolType>(
        std::clamp(rawTool, int(ToolType::Pen), int(ToolType::Highlighter)));
    s.color = QColor(o[QStringLiteral("color")].toString(QStringLiteral("#f8fafc")));
    if (!s.color.isValid())
        s.color = QColor(QStringLiteral("#f8fafc"));
    const double rawSize = o[QStringLiteral("size")].toDouble(2.6);
    s.size = std::isfinite(rawSize) ? std::clamp(rawSize, 0.2, 200.0) : 2.6;
    const double rawOpacity = o[QStringLiteral("opacity")].toDouble(1.0);
    s.opacity = std::isfinite(rawOpacity) ? std::clamp(rawOpacity, 0.0, 1.0) : 1.0;
    const int rawPattern = o[QStringLiteral("linePattern")].toInt(0);
    s.linePattern = static_cast<LinePattern>(
        std::clamp(rawPattern, int(LinePattern::Solid), int(LinePattern::Dimension)));
    const int rawProfile = o[QStringLiteral("pressureProfile")].toInt(0);
    s.pressureProfile = static_cast<PressureProfile>(
        std::clamp(rawProfile, int(PressureProfile::Light), int(PressureProfile::Firm)));
    const double rawSensitivity = o[QStringLiteral("pressureSensitivity")].toDouble(0.5);
    s.pressureSensitivity =
        std::isfinite(rawSensitivity) ? std::clamp(rawSensitivity, 0.0, 1.0) : 0.5;
    s.hasHardwarePressure = o[QStringLiteral("hasHardwarePressure")].toBool(false);
    const int maxTip = (s.tool == ToolType::Highlighter) ? 2 : 3;
    s.tipStyle = std::clamp(o[QStringLiteral("tipStyle")].toInt(0), 0, maxTip);
    const QJsonArray pts = o[QStringLiteral("points")].toArray();
    s.points.reserve(pts.size());
    for (const auto& v : pts) {
        const QJsonArray pa = v.toArray();
        if (pa.size() < 2)
            continue;
        const double px = pa[0].toDouble();
        const double py = pa[1].toDouble();
        if (!std::isfinite(px) || !std::isfinite(py))
            continue;
        StrokePoint sp;
        sp.x = px;
        sp.y = py;
        if (pa.size() >= 3) {
            const double pr = pa[2].toDouble(0.5);
            sp.pressure = std::isfinite(pr) ? std::clamp(pr, 0.0, 1.0) : 0.5;
        }
        if (pa.size() >= 4) {
            const double ts = pa[3].toDouble(0.0);
            if (std::isfinite(ts) && std::abs(ts) <= 9e15)
                sp.timestampMs = static_cast<qint64>(ts);
        }
        if (pa.size() >= 5) {
            const double tx = pa[4].toDouble(0.0);
            sp.tiltX = std::isfinite(tx) ? std::clamp(tx, -60.0, 60.0) : 0.0;
        }
        if (pa.size() >= 6) {
            const double ty = pa[5].toDouble(0.0);
            sp.tiltY = std::isfinite(ty) ? std::clamp(ty, -60.0, 60.0) : 0.0;
        }
        s.points.append(sp);
    }
    s.bounds = computeBounds(s.points, s.size);
    return s;
}

QJsonObject serializeShape(const ShapeItem& sh) {
    QJsonObject o;
    o[QStringLiteral("kind")] = static_cast<int>(sh.kind);
    o[QStringLiteral("color")] = sh.color.name(QColor::HexArgb);
    o[QStringLiteral("size")] = sh.size;
    o[QStringLiteral("opacity")] = sh.opacity;
    o[QStringLiteral("linePattern")] = static_cast<int>(sh.linePattern);
    o[QStringLiteral("lineEnding")] = static_cast<int>(sh.lineEnding);
    o[QStringLiteral("label")] = sh.label;
    o[QStringLiteral("closed")] = sh.closed;
    o[QStringLiteral("shadePattern")] = static_cast<int>(sh.shadePattern);
    if (sh.shadeLayer > 0)
        o[QStringLiteral("shadeLayer")] = sh.shadeLayer;
    if (!sh.shadeBoundary.isEmpty()) {
        QJsonObject boundary;
        for (auto it = sh.shadeBoundary.cbegin(); it != sh.shadeBoundary.cend(); ++it)
            boundary.insert(it.key(), it.value());
        o[QStringLiteral("shadeBoundary")] = boundary;
        o[QStringLiteral("shadeVisible")] = sh.shadeVisible;
    }
    if (!sh.shadeBoundary.isEmpty() || !sh.shadeProbe.isNull())
        o[QStringLiteral("shadeProbe")] = QJsonArray{sh.shadeProbe.x(), sh.shadeProbe.y()};
    o[QStringLiteral("solidKind")] = sh.solidKind;
    QJsonArray verts;
    for (const auto& p : sh.vertices) {
        QJsonArray pa;
        pa.append(p.x());
        pa.append(p.y());
        verts.append(pa);
    }
    o[QStringLiteral("vertices")] = verts;
    if (!sh.shadeContourStarts.isEmpty()) {
        QJsonArray cs;
        for (int idx : sh.shadeContourStarts)
            cs.append(idx);
        o[QStringLiteral("shadeContourStarts")] = cs;
    }
    return o;
}

ShapeItem deserializeShape(const QJsonObject& o) {
    ShapeItem sh;
    const int rawKind = o[QStringLiteral("kind")].toInt(0);
    sh.kind = static_cast<ShapeKind>(
        std::clamp(rawKind, int(ShapeKind::Line), int(ShapeKind::AngleMarker)));
    sh.color = QColor(o[QStringLiteral("color")].toString(QStringLiteral("#f8fafc")));
    if (!sh.color.isValid())
        sh.color = QColor(QStringLiteral("#f8fafc"));
    const double rawSize = o[QStringLiteral("size")].toDouble(2.6);
    sh.size = std::isfinite(rawSize) ? std::clamp(rawSize, 0.2, 200.0) : 2.6;
    const double rawOpacity = o[QStringLiteral("opacity")].toDouble(1.0);
    sh.opacity = std::isfinite(rawOpacity) ? std::clamp(rawOpacity, 0.0, 1.0) : 1.0;
    const int rawPattern = o[QStringLiteral("linePattern")].toInt(0);
    sh.linePattern = static_cast<LinePattern>(
        std::clamp(rawPattern, int(LinePattern::Solid), int(LinePattern::Dimension)));
    const int rawEnding = o[QStringLiteral("lineEnding")].toInt(0);
    sh.lineEnding = static_cast<LineEnding>(
        std::clamp(rawEnding, int(LineEnding::None), int(LineEnding::DoubleArrow)));
    sh.label = o[QStringLiteral("label")].toString();
    sh.closed = o[QStringLiteral("closed")].toBool(false);
    const int rawShade = o[QStringLiteral("shadePattern")].toInt(0);
    sh.shadePattern = static_cast<ShadePattern>(
        std::clamp(rawShade, int(ShadePattern::Soft), int(ShadePattern::Grid)));
    sh.shadeLayer = std::clamp(o[QStringLiteral("shadeLayer")].toInt(0), 0, 2);
    sh.solidKind = std::clamp(o[QStringLiteral("solidKind")].toInt(0), 0, 9);
    const QJsonArray verts = o[QStringLiteral("vertices")].toArray();
    sh.vertices.reserve(verts.size());
    for (const auto& v : verts) {
        const QJsonArray pa = v.toArray();
        if (pa.size() < 2)
            continue;
        const double vx = pa[0].toDouble();
        const double vy = pa[1].toDouble();
        if (std::isfinite(vx) && std::isfinite(vy))
            sh.vertices.append(QPointF(vx, vy));
    }
    const QJsonArray cs = o[QStringLiteral("shadeContourStarts")].toArray();
    for (const auto& v : cs) {
        const int idx = v.toInt(-1);
        if (idx > 0 && idx < sh.vertices.size())
            sh.shadeContourStarts.append(idx);
    }
    const QJsonObject boundary = o[QStringLiteral("shadeBoundary")].toObject();
    for (auto it = boundary.constBegin(); it != boundary.constEnd(); ++it) {
        const int side = it.value().toInt(0);
        if (!it.key().isEmpty() && side >= 1 && side <= 3)
            sh.shadeBoundary.insert(it.key(), side);
    }
    const QJsonArray probe = o[QStringLiteral("shadeProbe")].toArray();
    if (probe.size() == 2 && probe[0].isDouble() && probe[1].isDouble()) {
        const double px = probe[0].toDouble();
        const double py = probe[1].toDouble();
        if (std::isfinite(px) && std::isfinite(py))
            sh.shadeProbe = QPointF(px, py);
    }
    sh.shadeVisible = o[QStringLiteral("shadeVisible")].toBool(true);
    sh.bounds = computeShapeBounds(sh);
    return sh;
}

QJsonObject serializeText(const TextItem& t) {
    QJsonObject o;
    o[QStringLiteral("x")] = t.pos.x();
    o[QStringLiteral("y")] = t.pos.y();
    o[QStringLiteral("text")] = t.text;
    o[QStringLiteral("color")] = t.color.name(QColor::HexArgb);
    o[QStringLiteral("fontSize")] = t.fontSize;
    return o;
}

TextItem deserializeText(const QJsonObject& o) {
    TextItem t;
    const double rawX = o[QStringLiteral("x")].toDouble(0.0);
    const double rawY = o[QStringLiteral("y")].toDouble(0.0);
    t.pos = QPointF(std::isfinite(rawX) ? rawX : 0.0, std::isfinite(rawY) ? rawY : 0.0);
    t.text = o[QStringLiteral("text")].toString();
    t.color = QColor(o[QStringLiteral("color")].toString(QStringLiteral("#f8fafc")));
    if (!t.color.isValid())
        t.color = QColor(QStringLiteral("#f8fafc"));
    const double rawFont = o[QStringLiteral("fontSize")].toDouble(16.0);
    t.fontSize = std::isfinite(rawFont) ? std::clamp(rawFont, 1.0, 1000.0) : 16.0;
    t.bounds = textBounds(t);
    return t;
}

QVector<StickerTemplate> loadCustomTemplatesFromFile(const QString& path) {
    QVector<StickerTemplate> out;
    QFile f(path);
    if (!f.exists() || !f.open(QIODevice::ReadOnly))
        return out;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject())
        return out;
    const QJsonArray arr = doc.object()[QStringLiteral("stickers")].toArray();
    out.reserve(arr.size());
    for (const auto& val : arr) {
        const QJsonObject obj = val.toObject();
        StickerTemplate tpl;
        tpl.id = obj[QStringLiteral("id")].toString();
        if (tpl.id.isEmpty())
            continue;
        tpl.title = obj[QStringLiteral("title")].toString(QStringLiteral("Sticker"));
        const QString cat = obj[QStringLiteral("category")].toString().trimmed();
        tpl.category = cat.isEmpty() ? QStringLiteral("Mis ★") : cat;
        tpl.formula = obj[QStringLiteral("formula")].toString();
        tpl.isCustom = true;
        const double rawW = obj[QStringLiteral("baseW")].toDouble(220.0);
        const double rawH = obj[QStringLiteral("baseH")].toDouble(140.0);
        tpl.baseSize = QSizeF(std::isfinite(rawW) ? std::max(1.0, rawW) : 220.0,
                              std::isfinite(rawH) ? std::max(1.0, rawH) : 140.0);
        for (const auto& kw : obj[QStringLiteral("keywords")].toArray())
            tpl.keywords.append(kw.toString());
        for (const auto& sv : obj[QStringLiteral("strokes")].toArray())
            tpl.strokes.append(deserializeStroke(sv.toObject()));
        for (const auto& shv : obj[QStringLiteral("shapes")].toArray())
            tpl.shapes.append(deserializeShape(shv.toObject()));
        for (const auto& tv : obj[QStringLiteral("texts")].toArray())
            tpl.texts.append(deserializeText(tv.toObject()));
        out.append(std::move(tpl));
    }
    return out;
}

bool writeCustomTemplatesToFile(const QString& path, const QVector<StickerTemplate>& customs) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QJsonArray arr;
    for (const auto& tpl : customs) {
        QJsonObject obj;
        obj[QStringLiteral("id")] = tpl.id;
        obj[QStringLiteral("title")] = tpl.title;
        obj[QStringLiteral("category")] = tpl.category.trimmed().isEmpty()
            ? QStringLiteral("Mis ★")
            : tpl.category.trimmed();
        obj[QStringLiteral("formula")] = tpl.formula;
        obj[QStringLiteral("baseW")] = tpl.baseSize.width();
        obj[QStringLiteral("baseH")] = tpl.baseSize.height();
        QJsonArray kws;
        for (const auto& kw : tpl.keywords)
            kws.append(kw);
        obj[QStringLiteral("keywords")] = kws;
        QJsonArray strokes;
        for (const auto& s : tpl.strokes)
            strokes.append(serializeStroke(s));
        obj[QStringLiteral("strokes")] = strokes;
        QJsonArray shapes;
        for (const auto& sh : tpl.shapes)
            shapes.append(serializeShape(sh));
        obj[QStringLiteral("shapes")] = shapes;
        QJsonArray texts;
        for (const auto& t : tpl.texts)
            texts.append(serializeText(t));
        obj[QStringLiteral("texts")] = texts;
        arr.append(obj);
    }
    QJsonObject root;
    root[QStringLiteral("version")] = 1;
    root[QStringLiteral("stickers")] = arr;

    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    if (file.write(bytes) != bytes.size()) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

struct CustomStickerCache {
    QMutex mutex;
    QVector<StickerTemplate> templates;
    QString loadedPath;
    QDateTime lastModified;
    qint64 fileSize = -1;
    bool fileExists = false;
    bool loaded = false;
};

CustomStickerCache& customStickerCache() {
    static CustomStickerCache cache;
    return cache;
}

const QVector<StickerTemplate>& ensureCustomCacheLocked(CustomStickerCache& cache, const QString& path) {
    const QFileInfo info(path);
    const bool exists = info.exists();
    const QDateTime mtime = exists ? info.lastModified() : QDateTime();
    const qint64 size = exists ? info.size() : -1;
    const bool pathChanged = (cache.loadedPath != path);
    const bool fileChanged =
        (cache.fileExists != exists || cache.lastModified != mtime || cache.fileSize != size);
    if (!cache.loaded || pathChanged || fileChanged) {
        cache.templates = loadCustomTemplatesFromFile(path);
        cache.loadedPath = path;
        cache.fileExists = exists;
        cache.lastModified = mtime;
        cache.fileSize = size;
        cache.loaded = true;
    }
    return cache.templates;
}

void invalidateCustomCacheLocked(CustomStickerCache& cache) {
    cache.loaded = false;
    cache.templates.clear();
}

QVector<StickerTemplate> cachedCustomTemplates() {
    CustomStickerCache& cache = customStickerCache();
    QMutexLocker locker(&cache.mutex);
    return ensureCustomCacheLocked(cache, customStickersFilePath());
}

QRectF unionContentBounds(const QVector<ShapeItem>& shapes,
                          const QVector<Stroke>& strokes,
                          const QVector<TextItem>& texts) {
    QRectF total;
    bool hasAny = false;
    auto unite = [&](const QRectF& r) {
        if (r.isNull() || !std::isfinite(r.x()) || !std::isfinite(r.y()))
            return;
        total = hasAny ? total.united(r) : r;
        hasAny = true;
    };
    for (const auto& sh : shapes)
        unite(sh.bounds.isValid() ? sh.bounds : computeShapeBounds(sh));
    for (const auto& st : strokes)
        unite(st.bounds.isValid() ? st.bounds : computeBounds(st.points, st.size));
    for (const auto& tx : texts)
        unite(tx.bounds.isValid() ? tx.bounds : textBounds(tx));
    return hasAny ? total : QRectF(-100.0, -60.0, 200.0, 120.0);
}

} // namespace

QStringList stickerCategories() {
    return {
        QStringLiteral("Todos"),
        QStringLiteral("Triángulos"),
        QStringLiteral("Notables"),
        QStringLiteral("Congruencia"),
        QStringLiteral("Circunferencia"),
        QStringLiteral("Métricas"),
        QStringLiteral("Áreas"),
        QStringLiteral("Física"),
        QStringLiteral("Química"),
        QStringLiteral("Biología"),
        QStringLiteral("Mis ★"),
    };
}

QVector<StickerTemplate> allStickerTemplates() {
    const QVector<StickerTemplate>& builtins = builtinTemplates();
    const QVector<StickerTemplate> customs = cachedCustomTemplates();
    QVector<StickerTemplate> result;
    result.reserve(builtins.size() + customs.size());
    result.append(builtins);
    result.append(customs);
    return result;
}

std::optional<StickerTemplate> findStickerTemplate(const QString& id) {
    if (id.isEmpty())
        return std::nullopt;
    for (const auto& tpl : builtinTemplates()) {
        if (tpl.id == id)
            return tpl;
    }
    const QVector<StickerTemplate> customs = cachedCustomTemplates();
    for (const auto& tpl : customs) {
        if (tpl.id == id)
            return tpl;
    }
    return std::nullopt;
}

InstantiatedSticker instantiateSticker(const StickerTemplate& tpl,
                                       const QPointF& centerWorld,
                                       const ThemeConfig& theme,
                                       StickerStyle style,
                                       double scale) {
    InstantiatedSticker out;
    out.groupId = freshId();
    out.title = tpl.title;

    const double sc = std::clamp(scale, 0.2, 5.0);

    // 1. Copiar, escalar y adaptar colores del diagrama base
    out.shapes.reserve(tpl.shapes.size() + 3);
    for (ShapeItem sh : tpl.shapes) {
        sh.color = resolveSemanticColor(sh.color, theme, tpl.isCustom);
        sh.size = std::max(0.8, sh.size * sc);
        for (QPointF& v : sh.vertices)
            v *= sc;
        if (!sh.shadeBoundary.isEmpty() || !sh.shadeProbe.isNull())
            sh.shadeProbe *= sc;
        sh.bounds = computeShapeBounds(sh);
        out.shapes.append(sh);
    }

    out.strokes.reserve(tpl.strokes.size());
    for (Stroke st : tpl.strokes) {
        st.color = resolveSemanticColor(st.color, theme, tpl.isCustom);
        st.size = std::max(0.8, st.size * sc);
        for (StrokePoint& pt : st.points) {
            pt.x *= sc;
            pt.y *= sc;
        }
        st.cachedPath = QPainterPath();
        st.cachedShapeHash = 0;
        st.bounds = computeBounds(st.points, st.size);
        out.strokes.append(st);
    }

    out.texts.reserve(tpl.texts.size() + 2);
    for (TextItem tx : tpl.texts) {
        tx.color = resolveSemanticColor(tx.color, theme, tpl.isCustom);
        const QFontMetricsF oldM(textFont(tx.fontSize));
        const QPointF oldCenter(tx.pos.x() + oldM.horizontalAdvance(tx.text) * 0.5,
                                tx.pos.y() + oldM.height() * 0.5);
        tx.fontSize = std::max(8.0, tx.fontSize * sc);
        const QFontMetricsF newM(textFont(tx.fontSize));
        const QPointF newCenter = oldCenter * sc;
        tx.pos = QPointF(newCenter.x() - newM.horizontalAdvance(tx.text) * 0.5,
                         newCenter.y() - newM.height() * 0.5);
        tx.bounds = textBounds(tx);
        out.texts.append(tx);
    }

    // Orden de capas limpio para plantillas integradas:
    // 0: rellenos -> 1: arcos y ángulos rectos -> 2: figuras -> 3: marcas de congruencia
    if (!tpl.isCustom) {
        auto layerOf = [](const ShapeItem& s) -> int {
            if (s.kind == ShapeKind::ShadedRegion)
                return 0;
            if (s.kind == ShapeKind::AngleMarker || s.kind == ShapeKind::RightAngle)
                return 1;
            if (isEdgeMark(s))
                return 3;
            return 2;
        };
        std::stable_sort(out.shapes.begin(), out.shapes.end(),
                         [&](const ShapeItem& a, const ShapeItem& b) {
                             return layerOf(a) < layerOf(b);
                         });
    }

    const QRectF diagramBounds = unionContentBounds(out.shapes, out.strokes, out.texts);
    const QColor mainInk = resolveSemanticColor(sticker_detail::kInkMain, theme, false);
    const QColor accent1 = resolveSemanticColor(sticker_detail::kAccent1, theme, false);

    // 2. Decoración según estilo
    if (style == StickerStyle::CleanWithTitle) {
        if (!tpl.title.isEmpty()) {
            out.texts.append(makeBannerText(tpl.title, accent1, 13.5 * sc, diagramBounds.center().x(),
                                            diagramBounds.top() - 9.0 * sc, true));
        }
        if (!tpl.formula.isEmpty()) {
            out.texts.append(makeBannerText(tpl.formula, mainInk, 13.5 * sc, diagramBounds.center().x(),
                                            diagramBounds.bottom() + 9.0 * sc, false));
        }
    } else if (style == StickerStyle::Card) {
        double topY = diagramBounds.top();
        double bottomY = diagramBounds.bottom();
        double minX = diagramBounds.left();
        double maxX = diagramBounds.right();

        if (!tpl.title.isEmpty()) {
            const TextItem titleItem = makeBannerText(tpl.title, mainInk, 13.0 * sc,
                                                      diagramBounds.center().x(), topY - 9.0 * sc, true);
            out.texts.append(titleItem);
            topY = titleItem.bounds.top();
            minX = std::min(minX, titleItem.bounds.left());
            maxX = std::max(maxX, titleItem.bounds.right());
        }

        const bool hasFormula = !tpl.formula.isEmpty();
        const double sepY = bottomY + 9.0 * sc;
        if (hasFormula) {
            const TextItem formItem = makeBannerText(tpl.formula, accent1, 13.5 * sc,
                                                     diagramBounds.center().x(), sepY + 9.0 * sc, false);
            out.texts.append(formItem);
            bottomY = formItem.bounds.bottom();
            minX = std::min(minX, formItem.bounds.left());
            maxX = std::max(maxX, formItem.bounds.right());
        }

        const double padX = 18.0 * sc;
        const double padY = 14.0 * sc;
        const QRectF cardRect(QPointF(minX - padX, topY - padY),
                              QPointF(maxX + padX, bottomY + padY));

        const QColor fillColor = theme.isDark ? QColor(QStringLiteral("#94a3b8"))
                                              : QColor(QStringLiteral("#64748b"));
        const QColor borderColor = theme.isDark ? QColor(148, 163, 184, 130)
                                                : QColor(100, 116, 139, 130);

        ShapeItem bg = sticker_detail::makeRectShadedRegion(cardRect, fillColor, 0.72);
        bg.bounds = computeShapeBounds(bg);

        ShapeItem border;
        border.kind = ShapeKind::Rectangle;
        border.color = borderColor;
        border.size = std::max(1.0, 1.4 * sc);
        border.closed = true;
        border.vertices = {cardRect.topLeft(), cardRect.bottomRight()};
        border.bounds = computeShapeBounds(border);

        // Insertar fondo y borde al inicio para que se pinten detrás del diagrama
        out.shapes.prepend(border);
        out.shapes.prepend(bg);

        if (hasFormula) {
            ShapeItem sep = sticker_detail::makeLine(QPointF(cardRect.left() + 12.0 * sc, sepY),
                                                     QPointF(cardRect.right() - 12.0 * sc, sepY),
                                                     borderColor, std::max(0.9, 1.1 * sc));
            sep.bounds = computeShapeBounds(sep);
            out.shapes.insert(2, sep);
        }
    }

    // 3. Centrar exactamente en centerWorld y asignar UUIDs + groupId compartido
    const QRectF totalBounds = unionContentBounds(out.shapes, out.strokes, out.texts);
    const QPointF delta = centerWorld - totalBounds.center();

    for (ShapeItem& sh : out.shapes) {
        translateShape(sh, delta);
        if (!sh.shadeBoundary.isEmpty() || !sh.shadeProbe.isNull())
            sh.shadeProbe += delta;
        sh.bounds = computeShapeBounds(sh);
        sh.id = freshId();
        sh.groupId = out.groupId;
        sh.locked = false;
    }

    for (Stroke& st : out.strokes) {
        for (StrokePoint& pt : st.points) {
            pt.x += delta.x();
            pt.y += delta.y();
        }
        st.cachedPath = QPainterPath();
        st.cachedShapeHash = 0;
        st.bounds = computeBounds(st.points, st.size);
        st.id = freshId();
        st.groupId = out.groupId;
    }

    for (TextItem& tx : out.texts) {
        tx.pos += delta;
        tx.bounds = textBounds(tx);
        tx.id = freshId();
        tx.groupId = out.groupId;
        tx.locked = false;
    }

    return out;
}

QPixmap renderStickerThumbnail(const StickerTemplate& tpl,
                               const QSize& pixelSize,
                               const ThemeConfig& theme,
                               StickerStyle style) {
    constexpr double dpr = 2.0;
    const QSize safeSize(std::max(16, pixelSize.width()), std::max(16, pixelSize.height()));
    QPixmap pm(qRound(safeSize.width() * dpr), qRound(safeSize.height() * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);

    const InstantiatedSticker inst = instantiateSticker(tpl, QPointF(0.0, 0.0), theme, style, 1.0);
    const QRectF bounds = unionContentBounds(inst.shapes, inst.strokes, inst.texts);

    const double margin = 8.0;
    const double availW = std::max(4.0, safeSize.width() - 2.0 * margin);
    const double availH = std::max(4.0, safeSize.height() - 2.0 * margin);
    const double bw = std::max(1.0, bounds.width());
    const double bh = std::max(1.0, bounds.height());
    const double fitScale = std::min(availW / bw, availH / bh);

    QPainter painter(&pm);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    painter.translate(safeSize.width() * 0.5, safeSize.height() * 0.5);
    painter.scale(fitScale, fitScale);
    painter.translate(-bounds.center());

    for (const ShapeItem& sh : inst.shapes)
        drawShape(painter, sh);
    for (const Stroke& st : inst.strokes)
        drawStroke(painter, st, !theme.isDark);
    for (const TextItem& tx : inst.texts)
        drawTextItem(painter, tx);

    painter.end();
    return pm;
}

bool saveCustomSticker(const QString& title,
                       const QString& category,
                       const QVector<Stroke>& strokes,
                       const QVector<ShapeItem>& shapes,
                       const QVector<TextItem>& texts,
                       QString* outId) {
    if (strokes.isEmpty() && shapes.isEmpty() && texts.isEmpty())
        return false;

    StickerTemplate tpl;
    tpl.id = QStringLiteral("custom_") + freshId();
    tpl.title = title.trimmed().isEmpty() ? QStringLiteral("Mi sticker") : title.trimmed();
    const QString cat = category.trimmed().isEmpty() ? QStringLiteral("Mis ★") : category.trimmed();
    tpl.category = cat;
    tpl.isCustom = true;
    tpl.keywords = {tpl.title.toLower(), QStringLiteral("custom"), QStringLiteral("personalizado")};

    QVector<ShapeItem> normShapes = shapes;
    QVector<Stroke> normStrokes = strokes;
    QVector<TextItem> normTexts = texts;

    for (auto& sh : normShapes)
        sh.bounds = computeShapeBounds(sh);
    for (auto& st : normStrokes)
        st.bounds = computeBounds(st.points, st.size);
    for (auto& tx : normTexts)
        tx.bounds = textBounds(tx);

    const QRectF bounds = unionContentBounds(normShapes, normStrokes, normTexts);
    const QPointF offset = -bounds.center();

    for (auto& sh : normShapes) {
        translateShape(sh, offset);
        if (!sh.shadeBoundary.isEmpty() || !sh.shadeProbe.isNull())
            sh.shadeProbe += offset;
        sh.id.clear();
        sh.groupId.clear();
        sh.attachments.clear();
        sh.attachmentsKnown = false;
        sh.locked = false;
        sh.bounds = computeShapeBounds(sh);
    }
    for (auto& st : normStrokes) {
        for (auto& pt : st.points) {
            pt.x += offset.x();
            pt.y += offset.y();
        }
        st.id.clear();
        st.groupId.clear();
        st.cachedPath = QPainterPath();
        st.cachedShapeHash = 0;
        st.bounds = computeBounds(st.points, st.size);
    }
    for (auto& tx : normTexts) {
        tx.pos += offset;
        tx.id.clear();
        tx.groupId.clear();
        tx.locked = false;
        tx.bounds = textBounds(tx);
    }

    tpl.shapes = std::move(normShapes);
    tpl.strokes = std::move(normStrokes);
    tpl.texts = std::move(normTexts);
    tpl.baseSize = QSizeF(std::max(40.0, bounds.width()), std::max(40.0, bounds.height()));

    CustomStickerCache& cache = customStickerCache();
    QMutexLocker locker(&cache.mutex);
    const QString path = customStickersFilePath();
    QVector<StickerTemplate> customs = ensureCustomCacheLocked(cache, path);
    customs.append(tpl);
    if (!writeCustomTemplatesToFile(path, customs))
        return false;
    invalidateCustomCacheLocked(cache);
    if (outId)
        *outId = tpl.id;
    return true;
}

bool deleteCustomSticker(const QString& id) {
    if (id.isEmpty())
        return false;
    CustomStickerCache& cache = customStickerCache();
    QMutexLocker locker(&cache.mutex);
    const QString path = customStickersFilePath();
    QVector<StickerTemplate> customs = ensureCustomCacheLocked(cache, path);
    const qsizetype before = customs.size();
    customs.erase(std::remove_if(customs.begin(), customs.end(),
                                 [&](const StickerTemplate& t) {
                                     return t.id == id;
                                 }),
                  customs.end());
    if (customs.size() == before)
        return false;
    if (!writeCustomTemplatesToFile(path, customs))
        return false;
    invalidateCustomCacheLocked(cache);
    return true;
}

} // namespace finily
