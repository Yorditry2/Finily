#include "ui/home/LibraryObjects.h"

#include "ui/DesignTokens.h"
#include "ui/UiIcons.h"
#include "ui/home/HomeTheme.h"

#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>

namespace finily::libobj {
namespace {

constexpr qreal kFolderW = 148.0;
constexpr qreal kFolderScale = kFolderW / 122.0; // viewBox 122×96 del mockup
constexpr qreal kBoardW = 148.0, kBoardH = 108.0;
constexpr qreal kBookW = 104.0, kBookH = 136.0;
constexpr qreal kBookWideW = 148.0, kBookWideH = 104.0;
constexpr qreal kPdfW = 100.0, kPdfH = 130.0;

QRectF anchored(const QPointF& bc, qreal w, qreal h) {
    return QRectF(bc.x() - w / 2.0, bc.y() - h, w, h);
}

// Dibuja la imagen cubriendo `target` (recorta lo que sobra, sin deformar).
void drawImageCover(QPainter& p, const QRectF& target, const QImage& img) {
    if (img.isNull() || target.isEmpty())
        return;
    const QSizeF src = img.size();
    const qreal scale = std::max(target.width() / src.width(), target.height() / src.height());
    const QSizeF cut(target.width() / scale, target.height() / scale);
    const QRectF source((src.width() - cut.width()) / 2.0, 0.0, cut.width(), cut.height());
    p.drawImage(target, img, source);
}

bool isWideFormat(PageFormat fmt) {
    return fmt == PageFormat::A4Landscape || fmt == PageFormat::Slide16x9;
}

QString formatLabel(PageFormat fmt) {
    switch (fmt) {
        case PageFormat::A4Landscape: return QStringLiteral("A4 ↔");
        case PageFormat::Slide16x9: return QStringLiteral("16:9");
        case PageFormat::A4:
        case PageFormat::Infinite: break;
    }
    return QStringLiteral("A4");
}

// Ícono de carpeta según el índice guardado (ver symbolNames()).
void folderIcon(int index, QString* name, QString* pack) {
    static const char* names[] = {"", "book", "hash", "flask", "shape", "pencil", "star", "sun"};
    static const char* packs[] = {"", "folder", "ui", "folder", "folder", "ui", "ui", "folder"};
    const int i = std::clamp(index, 0, 7);
    *name = QString::fromLatin1(names[i]);
    *pack = QString::fromLatin1(packs[i]);
}

} // namespace

void paintObjectShadow(QPainter& p, const QRectF& r, qreal radius, qreal lift, bool dark) {
    p.save();
    p.setPen(Qt::NoPen);
    const int base = dark ? 34 : 10;
    for (int i = 0; i < 8; ++i) {
        const qreal s = i * 1.7;
        p.setBrush(QColor(8, 10, 20, std::max(1, int(base * (1.0 - i / 8.0)))));
        p.drawRoundedRect(r.adjusted(5.0 - s * 0.5, 6.0 + i * 0.9 + lift, -5.0 + s * 0.5, 3.0 + s * 0.7 + lift),
                          radius + s * 0.4, radius + s * 0.4);
    }
    p.restore();
}

// ---------------------------------------------------------------- carpeta
QRectF folderRect(const QPointF& bc) {
    return anchored(bc, kFolderW, 96.0 * kFolderScale);
}

void paintFolder(QPainter& p, const QPointF& bc, int colorIndex, int iconIndex, qreal open, bool dark) {
    const tokens::FolderColor& c = tokens::folderColor(colorIndex);
    const QRectF box = folderRect(bc);

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.translate(box.topLeft());
    p.scale(kFolderScale, kFolderScale);

    paintObjectShadow(p, QRectF(4, 21, 114, 71), 11, open * 2.0, dark);

    QPainterPath back;
    back.moveTo(4, 16);
    back.cubicTo(4, 9, 9, 4, 16, 4);
    back.lineTo(40, 4);
    back.cubicTo(45, 4, 48, 6, 52, 10);
    back.lineTo(56, 14);
    back.lineTo(108, 14);
    back.cubicTo(114, 14, 118, 18, 118, 24);
    back.lineTo(118, 82);
    back.cubicTo(118, 88, 113, 92, 107, 92);
    back.lineTo(15, 92);
    back.cubicTo(9, 92, 4, 88, 4, 82);
    back.closeSubpath();
    p.setPen(Qt::NoPen);
    p.setBrush(c.flap);
    p.drawPath(back);

    if (open > 0.01) {
        p.setBrush(QColor(255, 255, 255, int(242 * std::min(1.0, open * 1.6))));
        p.drawRoundedRect(QRectF(14, 10 + (1.0 - open) * 8.0, 92, 60), 5, 5);
    }

    // Frente: se inclina ligeramente al abrir (rotate -4° sobre 61,92).
    p.translate(61, 92);
    p.rotate(-4.0 * open);
    p.translate(-61, -92 - 3.0 * open);
    QLinearGradient grad(0, 21, 0, 92);
    grad.setColorAt(0.0, c.body1);
    grad.setColorAt(1.0, c.body2);
    p.setBrush(grad);
    p.drawRoundedRect(QRectF(4, 21, 114, 71), 11, 11);
    p.setPen(QPen(QColor(255, 255, 255, 122), 1.0));
    p.drawLine(QPointF(15, 22), QPointF(107, 22));

    QString name, pack;
    folderIcon(iconIndex, &name, &pack);
    if (!name.isEmpty())
        paintUiIcon(&p, QRectF(49, 45, 24, 24), name, c.ink, pack);
    p.restore();
}

// ---------------------------------------------------------------- pizarra
QRectF boardRect(const QPointF& bc) {
    return anchored(bc, kBoardW, kBoardH);
}

void paintBoard(QPainter& p, const QPointF& bc, const NotebookMeta& meta, bool dark) {
    const QRectF r = boardRect(bc);
    const ThemeConfig tc = getThemeConfig(meta.theme);
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    paintObjectShadow(p, r, 10, 0, dark);

    QPainterPath clip;
    clip.addRoundedRect(r, 10, 10);
    p.setClipPath(clip);
    p.fillRect(r, tc.background);
    if (!meta.preview.isNull())
        drawImageCover(p, r, meta.preview);
    else
        paintMiniTemplate(p, r, tc, meta.tpl, meta.templateScale * 0.7);
    p.setClipping(false);

    const bool ink = tc.isDark;
    p.setPen(QPen(ink || dark ? QColor(255, 255, 255, 26) : QColor(15, 23, 42, 18), 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 10, 10);

    QFont f = p.font();
    f.setPixelSize(13);
    f.setWeight(QFont::DemiBold);
    p.setFont(f);
    p.setPen(ink ? QColor(244, 244, 245, 140) : QColor(24, 24, 27, 140));
    p.drawText(QRectF(r.right() - 26, r.bottom() - 22, 20, 18), Qt::AlignRight | Qt::AlignBottom,
               QStringLiteral("∞"));
    p.restore();
}

// ---------------------------------------------------------------- cuaderno
QRectF bookRect(const QPointF& bc, PageFormat fmt) {
    return isWideFormat(fmt) ? anchored(bc, kBookWideW, kBookWideH) : anchored(bc, kBookW, kBookH);
}

void paintBook(QPainter& p, const QPointF& bc, const NotebookMeta& meta, bool dark) {
    const QRectF r = bookRect(bc, meta.pageFormat);
    const QColor base = coverColors().at(std::clamp(meta.coverColorIndex, 0, int(coverColors().size()) - 1));
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    paintObjectShadow(p, r, 8, 0, dark);

    // Lomo con radio 4 a la izquierda y 10 a la derecha.
    QPainterPath shape;
    const qreal rl = 4.0, rr = 10.0;
    shape.moveTo(r.left() + rl, r.top());
    shape.lineTo(r.right() - rr, r.top());
    shape.quadTo(r.right(), r.top(), r.right(), r.top() + rr);
    shape.lineTo(r.right(), r.bottom() - rr);
    shape.quadTo(r.right(), r.bottom(), r.right() - rr, r.bottom());
    shape.lineTo(r.left() + rl, r.bottom());
    shape.quadTo(r.left(), r.bottom(), r.left(), r.bottom() - rl);
    shape.lineTo(r.left(), r.top() + rl);
    shape.quadTo(r.left(), r.top(), r.left() + rl, r.top());

    QLinearGradient fill(r.topLeft(), r.bottomRight());
    fill.setColorAt(0.0, base);
    fill.setColorAt(1.0, base.darker(108));
    p.setPen(Qt::NoPen);
    p.setBrush(fill);
    p.drawPath(shape);

    p.setClipPath(shape);
    p.fillRect(QRectF(r.left(), r.top(), 9, r.height()), QColor(0, 0, 0, 31));
    QLinearGradient sheen(r.topLeft(), QPointF(r.left() + r.width() * 0.55, r.top() + r.height() * 0.35));
    sheen.setColorAt(0.0, QColor(255, 255, 255, 56));
    sheen.setColorAt(1.0, QColor(255, 255, 255, 0));
    p.fillRect(r, sheen);
    p.setClipping(false);

    // Etiqueta blanca y formato.
    const QRectF label(r.left() + r.width() * 0.16, r.top() + r.height() * 0.16, r.width() * 0.74, 18);
    p.setBrush(QColor(255, 255, 255, 230));
    p.drawRoundedRect(label, 3, 3);

    if (meta.coverIconIndex > 0) {
        const QRectF ic(r.center().x() - 11 + 4, r.top() + r.height() * 0.52 - 11, 22, 22);
        symbolGlyph(meta.coverIconIndex, QColor(255, 255, 255, 215)).paint(&p, ic.toRect());
    }

    QFont f = p.font();
    f.setPixelSize(9);
    f.setWeight(QFont::Bold);
    f.setLetterSpacing(QFont::PercentageSpacing, 104);
    p.setFont(f);
    p.setPen(QColor(255, 255, 255, 205));
    p.drawText(QRectF(r.left() + 16, r.bottom() - 18, r.width() - 20, 14), Qt::AlignLeft | Qt::AlignBottom,
               formatLabel(meta.pageFormat));
    p.restore();
}

// ---------------------------------------------------------------- PDF
QRectF pdfRect(const QPointF& bc) {
    return anchored(bc, kPdfW, kPdfH);
}

void paintPdf(QPainter& p, const QPointF& bc, const NotebookMeta& meta, bool dark) {
    const QRectF r = pdfRect(bc);
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    paintObjectShadow(p, r, 4, 0, dark);

    QPainterPath clip;
    clip.addRoundedRect(r, 4, 4);
    p.setClipPath(clip);
    p.fillRect(r, dark ? QColor(0xEC, 0xEC, 0xEF) : QColor(Qt::white));
    if (!meta.preview.isNull()) {
        drawImageCover(p, r, meta.preview);
    } else {
        // Renglones de texto con dos subrayados, como en el mockup.
        const qreal tops[] = {16, 24, 32, 40, 52, 60, 68};
        for (int i = 0; i < 7; ++i) {
            const qreal y = r.top() + tops[i] * r.height() / 104.0;
            const qreal right = (i % 3 == 2) ? 36.0 : 14.0;
            p.setPen(Qt::NoPen);
            p.setBrush((i == 2 || i == 5) ? QColor(250, 204, 21, 115) : QColor(0xE4, 0xE4, 0xE7));
            p.drawRoundedRect(QRectF(r.left() + 12, y, r.width() - 12 - right, 3.5), 2, 2);
        }
    }
    p.setClipping(false);

    QFont f = p.font();
    f.setPixelSize(9);
    f.setWeight(QFont::ExtraBold);
    p.setFont(f);
    const QRectF tag(r.left() + 9, r.bottom() - 25, 28, 15);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0xDC, 0x26, 0x26));
    p.drawRoundedRect(tag, 3, 3);
    p.setPen(Qt::white);
    p.drawText(tag, Qt::AlignCenter, QStringLiteral("PDF"));
    p.restore();
}

QRectF documentRect(const QPointF& bc, const NotebookMeta& meta) {
    if (meta.hasPdf)
        return pdfRect(bc);
    if (meta.pageFormat == PageFormat::Infinite)
        return boardRect(bc);
    return bookRect(bc, meta.pageFormat);
}

void paintDocument(QPainter& p, const QPointF& bc, const NotebookMeta& meta, bool dark) {
    if (meta.hasPdf)
        paintPdf(p, bc, meta, dark);
    else if (meta.pageFormat == PageFormat::Infinite)
        paintBoard(p, bc, meta, dark);
    else
        paintBook(p, bc, meta, dark);
}

} // namespace finily::libobj
