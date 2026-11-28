#include "PdfEngine.h"

#include "GeometryEngine.h"
#include "InkEngine.h"

#include <QFileInfo>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfDocument>
#include <QPdfWriter>
#include <QSaveFile>

#include <algorithm>
#include <cmath>
#include <limits>

namespace finily {

QVector<ImageItem> importPdfPages(const QString& path, double startY) {
    QVector<ImageItem> pages;
    if (!isFinite(startY))
        return pages;
    QPdfDocument document;
    if (document.load(path) != QPdfDocument::Error::None)
        return pages;
    for (int i = 0; i < document.pageCount(); ++i) {
        const QSizeF points = document.pagePointSize(i);
        if (points.width() <= 0 || points.height() <= 0
            || !isFinite(points.width()) || !isFinite(points.height())) {
            continue;
        }
        const double worldScale = 1000.0 / points.width();
        const QSizeF world = points * worldScale;
        // Bound both a single raster and the aggregate raster storage of long PDFs.
        const double pixelBudget = 512.0 * 1024.0 * 1024.0 / (4.0 * std::max(1, document.pageCount()));
        const double rasterScale = std::min({
            3.0 * worldScale,
            3200.0 / std::max(points.width(), points.height()),
            std::sqrt(pixelBudget / (points.width() * points.height())),
        });
        if (!isFinite(rasterScale) || rasterScale <= 0.0)
            continue;
        const QSize raster(std::max(1, int(std::ceil(points.width() * rasterScale))),
                           std::max(1, int(std::ceil(points.height() * rasterScale))));
        QImage rendered = document.render(i, raster);
        if (rendered.isNull())
            continue;
        // Componer sobre fondo blanco opaco para que PDFs con transparencia no se vean oscuros en DarkBoard.
        QImage image(rendered.size(), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        {
            QPainter bg(&image);
            bg.drawImage(0, 0, rendered);
        }
        ImageItem page;
        page.id = freshId();
        // Cada hoja del PDF vive centrada en su propia página del cuaderno (estilo PPT/GoodNotes).
        page.rect = QRectF(-world.width() / 2.0, -world.height() / 2.0, world.width(), world.height());
        page.image = image;
        page.isPdfPage = true;
        page.pdfPageIndex = i;
        page.pdfSourcePath = QFileInfo(path).absoluteFilePath();
        pages.append(page);
    }
    return pages;
}

namespace {

bool isValidRect(const QRectF& rect) {
    return !rect.isEmpty()
        && isFinite(rect.left())
        && isFinite(rect.top())
        && isFinite(rect.width())
        && isFinite(rect.height())
        && rect.width() > 0.0
        && rect.height() > 0.0;
}

QRectF strokeBounds(const Stroke& s) {
    QVector<StrokePoint> validPoints;
    validPoints.reserve(s.points.size());
    for (const auto& pt : s.points) {
        if (isFinite(pt.x) && isFinite(pt.y))
            validPoints.append(pt);
    }
    if (validPoints.isEmpty())
        return {};

    auto controls = validPoints;
    // Catmull-Rom segments live inside their cubic control hull, including
    // overshoot outside the input-point rectangle on sharp handwriting turns.
    for (qsizetype i = 0; i + 1 < validPoints.size(); ++i) {
        const auto& a = validPoints[std::max<qsizetype>(0, i - 1)];
        const auto& b = validPoints[i];
        const auto& c = validPoints[i + 1];
        const auto& d = validPoints[std::min(i + 2, validPoints.size() - 1)];
        controls.append({b.x + (c.x - a.x) / 6.0, b.y + (c.y - a.y) / 6.0});
        controls.append({c.x - (d.x - b.x) / 6.0, c.y - (d.y - b.y) / 6.0});
    }
    const double strokeSize = (isFinite(s.size) && s.size > 0.0) ? s.size : 0.0;
    const double margin = std::max(strokeSize * 1.4, 4.0);
    QRectF bounds = computeBounds(controls, margin);
    if (s.linePattern == LinePattern::Arrow || s.linePattern == LinePattern::DoubleArrow) {
        ShapeItem arrows;
        arrows.kind = ShapeKind::Polyline;
        arrows.size = strokeSize;
        arrows.linePattern = s.linePattern;
        for (const auto& point : validPoints)
            arrows.vertices.append({point.x, point.y});
        const QRectF arrowBounds = computeShapeBounds(arrows);
        if (isValidRect(arrowBounds))
            bounds = bounds.united(arrowBounds);
    }
    return bounds;
}

QVector<QRectF> pdfPageBounds(const PageData& document, PageFormat pageFormat) {
    const auto& strokes = document.strokes;
    const auto& shapes = document.shapes;
    const auto& images = document.images;
    const auto& texts = document.texts;
    QVector<QRectF> pages;
    QVector<const ImageItem*> pdfPages;
    for (const auto& image : images) {
        if (image.isPdfPage && isValidRect(image.rect))
            pdfPages.append(&image);
    }
    if (pdfPages.isEmpty()) {
        QRectF bounds;
        bool first = true;
        auto add = [&](const QRectF& r) {
            if (!isValidRect(r))
                return;
            if (first) {
                bounds = r;
                first = false;
            } else {
                bounds = bounds.united(r);
            }
        };
        for (const auto& s : strokes) {
            if (!s.points.isEmpty())
                add(strokeBounds(s));
        }
        for (const auto& s : shapes)
            add(computeShapeBounds(s));
        for (const auto& image : images)
            add(image.rect);
        for (const auto& item : texts)
            add(textBounds(item));

        const auto sheet = sheetRectForPageFormat(pageFormat);
        if (isValidRect(sheet)) {
            QRectF extent = sheet;
            if (!first && !sheet.contains(bounds))
                extent = extent.united(bounds.adjusted(-20.0, -20.0, 20.0, 20.0));
            pages.append(extent);
        } else {
            if (first)
                bounds = QRectF(0.0, 0.0, 1000.0, 700.0);
            pages.append(bounds.adjusted(-40.0, -40.0, 40.0, 40.0));
        }
    } else {
        for (const auto* page : pdfPages)
            pages.append(page->rect);
        // Each annotation belongs to its closest slide. Include marginal notes
        // without exploding every slide to the complete infinite-board extent.
        auto assign = [&](const QRectF& bounds) {
            if (!isValidRect(bounds))
                return;
            qsizetype best = 0;
            double distance = std::numeric_limits<double>::max();
            const QPointF p = bounds.center();
            for (qsizetype i = 0; i < pdfPages.size(); ++i) {
                const QRectF rect = pdfPages[i]->rect;
                const double dx = std::max({rect.left() - p.x(), 0.0, p.x() - rect.right()});
                const double dy = std::max({rect.top() - p.y(), 0.0, p.y() - rect.bottom()});
                const double d = dx * dx + dy * dy;
                if (isFinite(d) && d < distance) {
                    distance = d;
                    best = i;
                }
            }
            pages[best] = pages[best].united(bounds);
        };
        for (const auto& s : strokes) {
            if (!s.points.isEmpty())
                assign(strokeBounds(s));
        }
        for (const auto& s : shapes)
            assign(computeShapeBounds(s));
        for (const auto& image : images) {
            if (!image.isPdfPage)
                assign(image.rect);
        }
        for (const auto& item : texts)
            assign(textBounds(item));
        for (auto& rect : pages)
            rect.adjust(-20.0, -20.0, 20.0, 20.0);
    }
    return pages;
}

} // namespace

bool exportDocumentToPdf(const QString& path, const QVector<Stroke>& strokes,
                         const QVector<ShapeItem>& shapes, const QVector<ImageItem>& images,
                         const ThemeConfig& theme, BackgroundTemplate tpl,
                         const QVector<TextItem>& texts) {
    return exportPagesToPdf(path, {PageData{strokes, shapes, images, texts, {}, {}}}, theme, tpl);
}

bool exportPagesToPdf(const QString& path, const QVector<PageData>& notebook,
                      const ThemeConfig& theme, BackgroundTemplate tpl, PageFormat pageFormat,
                      double templateScale) {
    // Keep the logical page attached to every PDF sheet: equal world coordinates
    // on different notebook pages must never merge their annotations.
    const QVector<PageData> documents = notebook.isEmpty() ? QVector<PageData>{PageData{}} : notebook;
    struct Sheet {
        qsizetype document;
        QRectF rect;
    };
    QVector<Sheet> pages;
    for (qsizetype i = 0; i < documents.size(); ++i) {
        for (const auto& rect : pdfPageBounds(documents[i], pageFormat)) {
            if (isValidRect(rect))
                pages.append({i, rect});
        }
    }
    if (pages.isEmpty())
        return false;

    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly))
        return false;
    bool success = true;
    {
        QPdfWriter writer(&output);
        writer.setResolution(144);
        writer.setTitle(QStringLiteral("Finily"));
        writer.setCreator(QStringLiteral("Finily"));
        auto layout = [&](const QRectF& rect) {
            if (!isValidRect(rect))
                return false;
            const double maxDim = std::max(rect.width(), rect.height());
            if (!isFinite(maxDim) || maxDim <= 0.0)
                return false;
            // One world pixel is 0.75pt; coordinates remain vector paths.
            const double scale = std::min(25.4 / 96.0, 4800.0 / maxDim);
            const QSizeF millimeters = rect.size() * scale;
            if (!isFinite(millimeters.width()) || !isFinite(millimeters.height())
                || millimeters.width() <= 0.0 || millimeters.height() <= 0.0) {
                return false;
            }
            return writer.setPageLayout(QPageLayout(
                QPageSize(millimeters, QPageSize::Millimeter, QString(), QPageSize::ExactMatch),
                QPageLayout::Portrait, QMarginsF(), QPageLayout::Millimeter));
        };
        if (!layout(pages.front().rect))
            return false;
        QPainter painter;
        if (!painter.begin(&writer))
            return false;
        for (qsizetype page = 0; page < pages.size(); ++page) {
            const auto rect = pages[page].rect;
            const auto& document = documents[pages[page].document];
            const auto& strokes = document.strokes;
            const auto& shapes = document.shapes;
            const auto& images = document.images;
            const auto& texts = document.texts;
            const auto pageTemplate = document.background.value_or(tpl);
            if (page > 0) {
                if (!layout(rect) || !writer.newPage()) {
                    success = false;
                    break;
                }
            }
            const double sx = double(writer.width()) / rect.width();
            const double sy = double(writer.height()) / rect.height();
            if (!isFinite(sx) || !isFinite(sy) || sx <= 0.0 || sy <= 0.0) {
                success = false;
                break;
            }
            painter.save();
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            painter.scale(sx, sy);
            painter.translate(-rect.left(), -rect.top());
            painter.setClipRect(rect);

            const bool imported = std::any_of(images.cbegin(), images.cend(), [](const ImageItem& image) {
                return image.isPdfPage && isValidRect(image.rect);
            });
            const bool lightHlBg = !theme.isDark || imported;
            const auto sheet = imported ? QRectF() : sheetRectForPageFormat(pageFormat);
            const double tplScale = clampTemplateScale(templateScale);
            if (sheet.isEmpty()) {
                painter.fillRect(rect, theme.background);
                drawBackgroundTemplate(painter, pageTemplate, theme, Viewport{}, rect, tplScale);
            } else {
                painter.fillRect(rect, theme.isDark ? QColor("#0c0e13") : QColor("#e6e7ec"));
                painter.save();
                painter.setClipRect(sheet, Qt::IntersectClip);
                painter.fillRect(sheet, theme.background);
                drawBackgroundTemplate(painter, pageTemplate, theme, Viewport{}, sheet, tplScale, sheet);
                painter.restore();
            }
            for (int pass = 0; pass < 2; ++pass) {
                for (const auto& image : images) {
                    if (image.isPdfPage == (pass == 0) && image.rect.intersects(rect)
                        && !image.image.isNull()) {
                        if (image.opacity < 0.999) {
                            painter.save();
                            painter.setOpacity(painter.opacity() * image.opacity);
                            painter.drawImage(image.rect, image.image);
                            painter.restore();
                        } else {
                            painter.drawImage(image.rect, image.image);
                        }
                    }
                }
            }
            for (const auto& s : shapes) {
                if (s.kind == ShapeKind::ShadedRegion && computeShapeBounds(s).intersects(rect))
                    drawShape(painter, s);
            }
            for (const auto& s : strokes) {
                if (s.tool == ToolType::Highlighter && strokeBounds(s).intersects(rect))
                    drawStroke(painter, s, lightHlBg);
            }
            for (const auto& s : shapes) {
                if (s.kind != ShapeKind::ShadedRegion && computeShapeBounds(s).intersects(rect))
                    drawShape(painter, s);
            }
            for (const auto& s : strokes) {
                if (s.tool != ToolType::Highlighter && strokeBounds(s).intersects(rect))
                    drawStroke(painter, s, lightHlBg);
            }
            painter.setRenderHint(QPainter::TextAntialiasing);
            for (const auto& item : texts) {
                if (textBounds(item).intersects(rect))
                    drawTextItem(painter, item);
            }
            painter.restore();
        }
        if (!painter.end())
            success = false;
    }
    if (!success) {
        output.cancelWriting();
        return false;
    }
    return output.commit();
}

} // namespace finily
