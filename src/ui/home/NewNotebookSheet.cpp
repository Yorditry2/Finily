#include "ui/home/NewNotebookSheet.h"

#include "ui/DesignTokens.h"
#include "ui/UiIcons.h"
#include "ui/home/BoardColorPopover.h"
#include "ui/home/LibraryObjects.h"
#include "ui/home/SheetWidgets.h"

#include <QApplication>
#include <QFileInfo>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>

namespace finily {
namespace {

constexpr int kModalW = 880, kModalH = 600, kLeftW = 312;
const BackgroundTemplate kTpls[] = {BackgroundTemplate::Blank, BackgroundTemplate::Grid,
                                    BackgroundTemplate::Dots, BackgroundTemplate::Ruled};
constexpr double kSpacingScales[] = {0.8, 1.0, 1.4, 2.0};
const char* kSpacingLabels[] = {"4 mm", "5 mm", "7 mm", "10 mm"};

const std::array<QColor, 6>& quickPaperColors() {
    static const std::array<QColor, 6> kColors = {
        QColor("#FFFFFF"), // Blanco
        QColor("#FAF6EA"), // Crema
        QColor("#FCF6D6"), // Bloc amarillo
        QColor("#EFF1F5"), // Gris papel
        QColor("#1A1B22"), // Grafito
        QColor("#1E2A44"), // Azul noche
    };
    return kColors;
}

ThemeId themeForPaperColor(const QColor& c) {
    const QRgb rgb = c.rgb();
    if (rgb == getThemeConfig(ThemeId::PaperLight).background.rgb())
        return ThemeId::PaperLight;
    if (rgb == getThemeConfig(ThemeId::PaperCream).background.rgb())
        return ThemeId::PaperCream;
    if (rgb == getThemeConfig(ThemeId::DarkBoard).background.rgb())
        return ThemeId::DarkBoard;
    if (rgb == getThemeConfig(ThemeId::GreenBoard).background.rgb())
        return ThemeId::GreenBoard;
    const double lum = 0.299 * c.red() + 0.587 * c.green() + 0.114 * c.blue();
    return (lum < 140.0) ? ThemeId::DarkBoard : ThemeId::PaperLight;
}

QFont nbFont(qreal px, QFont::Weight weight) {
    QFont f = QApplication::font();
    f.setPixelSize(qRound(px));
    f.setWeight(weight);
    return f;
}

qreal formatRatio(PageFormat f) { // ancho / alto
    switch (f) {
        case PageFormat::A4Landscape: return 1123.0 / 794.0;
        case PageFormat::Slide16x9: return 16.0 / 9.0;
        case PageFormat::A5: return 560.0 / 794.0;
        case PageFormat::A5Landscape: return 794.0 / 560.0;
        case PageFormat::Letter: return 816.0 / 1056.0;
        case PageFormat::LetterLandscape: return 1056.0 / 816.0;
        case PageFormat::Legal: return 816.0 / 1344.0;
        case PageFormat::LegalLandscape: return 1344.0 / 816.0;
        case PageFormat::Square: return 1.0;
        default: return 794.0 / 1123.0;
    }
}

bool isFormatLandscape(PageFormat f) {
    return f == PageFormat::A4Landscape || f == PageFormat::A5Landscape
        || f == PageFormat::LetterLandscape || f == PageFormat::LegalLandscape
        || f == PageFormat::Slide16x9;
}

bool formatSupportsOrientation(PageFormat f) {
    return f != PageFormat::Square && f != PageFormat::Slide16x9;
}

int baseFormatIndex(PageFormat f) {
    switch (f) {
        case PageFormat::A5:
        case PageFormat::A5Landscape: return 1;
        case PageFormat::Letter:
        case PageFormat::LetterLandscape: return 2;
        case PageFormat::Legal:
        case PageFormat::LegalLandscape: return 3;
        case PageFormat::Square: return 4;
        case PageFormat::Slide16x9: return 5;
        default: return 0;
    }
}

PageFormat resolvePageFormat(int baseIdx, bool landscape) {
    switch (baseIdx) {
        case 1: return landscape ? PageFormat::A5Landscape : PageFormat::A5;
        case 2: return landscape ? PageFormat::LetterLandscape : PageFormat::Letter;
        case 3: return landscape ? PageFormat::LegalLandscape : PageFormat::Legal;
        case 4: return PageFormat::Square;
        case 5: return PageFormat::Slide16x9;
        default: return landscape ? PageFormat::A4Landscape : PageFormat::A4;
    }
}

QString baseFormatTitle(int baseIdx) {
    static const char* kNames[6] = {"A4", "A5", "Carta", "Legal", "1:1", "16:9"};
    return QString::fromUtf8(kNames[std::clamp(baseIdx, 0, 5)]);
}

QString formatDimensionsLabel(int baseIdx, bool landscape) {
    switch (baseIdx) {
        case 0: return landscape ? QStringLiteral("297 × 210 mm") : QStringLiteral("210 × 297 mm");
        case 1: return landscape ? QStringLiteral("210 × 148 mm") : QStringLiteral("148 × 210 mm");
        case 2: return landscape ? QStringLiteral("279 × 216 mm") : QStringLiteral("216 × 279 mm");
        case 3: return landscape ? QStringLiteral("356 × 216 mm") : QStringLiteral("216 × 356 mm");
        case 4: return QStringLiteral("Cuadrado");
        case 5: return QStringLiteral("Panorámica");
        default: return {};
    }
}

} // namespace

// ---------------------------------------------------------------- vista previa
class NotebookPreview : public QWidget {
public:
    explicit NotebookPreview(QWidget* parent) : QWidget(parent) { setFixedHeight(252); }
    void setState(const NewDocumentSpec& spec, bool showSheet, bool dark) {
        m_spec = spec;
        m_sheet = showSheet;
        m_dark = dark;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const auto pal = tokens::paletteFor(m_dark);
        p.setPen(Qt::NoPen);
        p.setBrush(pal.neutral);
        p.drawRoundedRect(QRectF(rect()), 14, 14);

        NotebookMeta meta;
        meta.coverColorIndex = std::max(0, m_spec.cover.colorIndex);
        meta.pageFormat = m_spec.format;
        const QRectF natural = libobj::bookRect(QPointF(0, 0), m_spec.format);
        const qreal r = formatRatio(m_spec.format);
        const qreal availW = width() - 36.0;
        const qreal availH = height() - 34.0;
        const qreal cx = width() / 2.0;
        const qreal cy = height() / 2.0;

        auto drawBook = [&](const QPointF& bc, qreal w, qreal h) {
            const qreal sy = h / natural.height();
            const qreal sx = w / natural.width();
            p.save();
            p.translate(bc);
            p.scale(sx, sy);
            libobj::paintBook(p, QPointF(0, 0), meta, m_dark);
            p.restore();
        };

        auto drawSheet = [&](const QRectF& sheetRect) {
            const ThemeConfig tc = effectiveThemeConfig(m_spec.theme, m_spec.customBackground);
            paintSoftShadow(p, sheetRect, 5, 1.0, m_dark);
            p.setPen(Qt::NoPen);
            p.setBrush(tc.background);
            p.drawRoundedRect(sheetRect, 5, 5);
            p.save();
            QPainterPath clip;
            clip.addRoundedRect(sheetRect, 5, 5);
            p.setClipPath(clip);
            paintMiniTemplate(p, sheetRect, tc, m_spec.tpl, m_spec.templateScale);
            p.restore();
            p.setPen(QPen(tc.isDark || m_dark ? QColor(255, 255, 255, 32) : QColor(15, 23, 42, 18), 1.0));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(sheetRect.adjusted(0.5, 0.5, -0.5, -0.5), 5, 5);
        };

        if (!m_sheet) {
            qreal h = std::min(availH, availW / r);
            qreal w = h * r;
            drawBook(QPointF(cx, cy + h / 2.0), w, h);
            return;
        }

        if (r > 1.05) {
            const qreal shiftX = 40.0;
            const qreal shiftY = 30.0;
            const qreal w = std::min(availW - shiftX, (availH - shiftY) * r);
            const qreal h = w / r;
            const qreal totalW = w + shiftX;
            const qreal totalH = h + shiftY;
            const qreal x0 = cx - totalW / 2.0;
            const qreal y0 = cy - totalH / 2.0;
            drawBook(QPointF(x0 + w / 2.0, y0 + h), w, h);
            drawSheet(QRectF(x0 + shiftX, y0 + shiftY, w, h));
        } else {
            const qreal gap = 14.0;
            const qreal h = std::min(availH, (availW - gap) / (2.0 * r));
            const qreal w = h * r;
            const qreal totalW = 2.0 * w + gap;
            drawBook(QPointF(cx - totalW / 2.0 + w / 2.0, cy + h / 2.0), w, h);
            drawSheet(QRectF(cx - totalW / 2.0 + w + gap, cy - h / 2.0, w, h));
        }
    }

private:
    NewDocumentSpec m_spec;
    bool m_sheet = true, m_dark = false;
};

// ---------------------------------------------------------------- cuadrícula de muestras
class TileGrid : public QWidget {
    Q_OBJECT
public:
    using Painter = std::function<void(QPainter&, const QRectF&, int index, bool selected, bool hover)>;
    TileGrid(int count, int cols, qreal tileH, Painter paint, QWidget* parent)
        : QWidget(parent), m_count(count), m_cols(cols), m_tileH(tileH), m_paint(std::move(paint)) {
        setMouseTracking(true);
        setCursor(Qt::PointingHandCursor);
        setFixedHeight(heightFor(width() > 0 ? width() : 520));
    }
    void setCurrent(int i) { m_current = i; update(); }
    void setSelectableCount(int n) { m_selectable = n; }
    void setDark(bool d) { m_dark = d; update(); }
    bool dark() const { return m_dark; }
    int heightFor(int) const {
        const int rows = (m_count + m_cols - 1) / m_cols;
        return int(rows * m_tileH + (rows - 1) * m_gapY);
    }
signals:
    void picked(int index);

protected:
    QRectF cell(int i) const {
        const qreal w = (width() - (m_cols - 1) * m_gapX) / m_cols;
        return QRectF((i % m_cols) * (w + m_gapX), (i / m_cols) * (m_tileH + m_gapY), w, m_tileH);
    }
    int hit(const QPointF& pos) const {
        for (int i = 0; i < m_count; ++i)
            if (cell(i).contains(pos))
                return i;
        return -1;
    }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        for (int i = 0; i < m_count; ++i)
            m_paint(p, cell(i), i, i == m_current, i == m_hover);
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        const int h = hit(e->position());
        if (h != m_hover) {
            m_hover = h;
            update();
        }
        setCursor((h >= 0 && h < m_selectable && isEnabled()) ? Qt::PointingHandCursor : Qt::ArrowCursor);
    }
    void leaveEvent(QEvent*) override { m_hover = -1; update(); }
    void mouseReleaseEvent(QMouseEvent* e) override {
        if (!isEnabled())
            return;
        const int h = hit(e->position());
        if (h >= 0 && h < m_selectable)
            emit picked(h);
    }

private:
    int m_count, m_cols;
    qreal m_tileH;
    Painter m_paint;
    int m_current = 0, m_hover = -1, m_selectable = 1 << 20;
    qreal m_gapX = 12, m_gapY = 12;
    bool m_dark = false;
};

// ---------------------------------------------------------------- puntos de color (Portada / Papel + '+')
class CoverColorDots : public QWidget {
    Q_OBJECT
public:
    explicit CoverColorDots(QWidget* parent) : QWidget(parent) {
        setFixedSize(8 * 22 + 8, 24);
        setCursor(Qt::PointingHandCursor);
    }
    void setCurrent(int i) { m_current = i; update(); }
    void setDark(bool d) { m_dark = d; update(); }
signals:
    void picked(int index);

protected:
    QRectF cell(int i) const { return QRectF(i * 22 + 4, 4, 16, 16); }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const auto pal = tokens::paletteFor(m_dark);
        const auto& cols = coverColors();
        for (int i = 0; i < cols.size(); ++i) {
            const QRectF c = cell(i);
            if (i == m_current) {
                p.setPen(QPen(pal.surface, 1.8));
                p.setBrush(Qt::NoBrush);
                p.drawEllipse(c.adjusted(-1.4, -1.4, 1.4, 1.4));
                p.setPen(QPen(pal.accent, 1.5));
                p.drawEllipse(c.adjusted(-3.0, -3.0, 3.0, 3.0));
            }
            p.setPen(QPen(m_dark ? QColor(255, 255, 255, 45) : QColor(15, 23, 42, 35), 1.0));
            p.setBrush(cols.at(i));
            p.drawEllipse(c);
        }
    }
    void mouseReleaseEvent(QMouseEvent* e) override {
        for (int i = 0; i < coverColors().size(); ++i) {
            if (cell(i).adjusted(-3, -3, 3, 3).contains(e->position())) {
                emit picked(i);
                return;
            }
        }
    }

private:
    int m_current = 0;
    bool m_dark = false;
};

class PaperColorRow : public QWidget {
    Q_OBJECT
public:
    explicit PaperColorRow(QWidget* parent) : QWidget(parent) {
        setFixedSize(7 * 24 + 8, 26);
        setCursor(Qt::PointingHandCursor);
    }
    void setCurrent(int i) { m_current = i; update(); }
    void setMoreActive(bool on) { m_moreActive = on; update(); }
    void setMoreColor(const QColor& c) { m_moreColor = c; update(); }
    void setDark(bool d) { m_dark = d; update(); }

signals:
    void picked(int index);
    void moreClicked();

protected:
    QRectF cell(int i) const { return QRectF(i * 24 + 5, 4, 18, 18); }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const auto pal = tokens::paletteFor(m_dark);
        const auto& cols = quickPaperColors();
        for (int i = 0; i < int(cols.size()); ++i) {
            const QRectF c = cell(i);
            if (i == m_current) {
                p.setPen(QPen(pal.surface, 1.8));
                p.setBrush(Qt::NoBrush);
                p.drawEllipse(c.adjusted(-1.3, -1.3, 1.3, 1.3));
                p.setPen(QPen(pal.accent, 1.5));
                p.drawEllipse(c.adjusted(-3.0, -3.0, 3.0, 3.0));
            }
            p.setPen(QPen(m_dark ? QColor(255, 255, 255, 48) : QColor(15, 23, 42, 36), 1.0));
            p.setBrush(cols.at(i));
            p.drawEllipse(c);
        }

        const QRectF r = cell(int(cols.size()));
        const bool ringOn = m_moreActive || (m_current < 0 && m_moreColor.isValid());
        if (ringOn) {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(pal.surface, 1.8));
            p.drawEllipse(r.adjusted(-1.3, -1.3, 1.3, 1.3));
            p.setPen(QPen(pal.accent, 1.5));
            p.drawEllipse(r.adjusted(-3.0, -3.0, 3.0, 3.0));
        }
        QConicalGradient cg(r.center(), 90.0);
        cg.setColorAt(0.0 / 6.0, QColor(0xE7, 0x8F, 0xA8));
        cg.setColorAt(1.0 / 6.0, QColor(0xE1, 0xC0, 0xA2));
        cg.setColorAt(2.0 / 6.0, QColor(0xED, 0xE0, 0x9A));
        cg.setColorAt(3.0 / 6.0, QColor(0x92, 0xBF, 0xA9));
        cg.setColorAt(4.0 / 6.0, QColor(0x92, 0xB5, 0xDD));
        cg.setColorAt(5.0 / 6.0, QColor(0xB3, 0xA2, 0xDC));
        cg.setColorAt(1.0, QColor(0xE7, 0x8F, 0xA8));
        p.setPen(QPen(m_dark ? QColor(255, 255, 255, 41) : QColor(15, 23, 42, 31), 1.0));
        p.setBrush(cg);
        p.drawEllipse(r);

        const QRectF inner = r.adjusted(3.0, 3.0, -3.0, -3.0);
        const bool customDot = (m_current < 0 && m_moreColor.isValid());
        p.setPen(Qt::NoPen);
        p.setBrush(customDot ? m_moreColor : pal.surface);
        p.drawEllipse(inner);

        QColor plusColor = pal.icon;
        if (customDot) {
            const double lum = 0.299 * m_moreColor.red() + 0.587 * m_moreColor.green() + 0.114 * m_moreColor.blue();
            plusColor = (lum < 140.0) ? QColor(0xF4, 0xF4, 0xF5) : QColor(0x18, 0x18, 0x1B);
        }
        paintUiIcon(&p, QRectF(r.center().x() - 4.5, r.center().y() - 4.5, 9.0, 9.0),
                    QStringLiteral("plus"), plusColor);
    }

    void mouseReleaseEvent(QMouseEvent* e) override {
        for (int i = 0; i < int(quickPaperColors().size()); ++i) {
            if (cell(i).adjusted(-3, -3, 3, 3).contains(e->position())) {
                emit picked(i);
                return;
            }
        }
        if (cell(int(quickPaperColors().size())).adjusted(-3, -3, 3, 3).contains(e->position()))
            emit moreClicked();
    }

private:
    int m_current = 0;
    bool m_moreActive = false;
    QColor m_moreColor;
    bool m_dark = false;
};

// ---------------------------------------------------------------- botón desplegable + popover de formatos
class FormatPickerButton : public QWidget {
    Q_OBJECT
public:
    explicit FormatPickerButton(QWidget* parent) : QWidget(parent) {
        setFixedHeight(38);
        setCursor(Qt::PointingHandCursor);
        setMouseTracking(true);
    }
    void setState(int baseIndex, bool landscape, bool dark) {
        m_baseIndex = std::clamp(baseIndex, 0, 5);
        m_landscape = landscape;
        m_dark = dark;
        update();
    }

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const auto pal = tokens::paletteFor(m_dark);
        p.setPen(QPen(m_hover ? pal.borderPop : pal.border, 1.0));
        p.setBrush(m_hover ? pal.hover : pal.field);
        p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 10, 10);

        const PageFormat fmt = resolvePageFormat(m_baseIndex, m_landscape);
        const qreal r = formatRatio(fmt);
        const qreal maxSide = 14.0;
        const qreal sw = (r >= 1.0) ? maxSide : maxSide * r;
        const qreal sh = (r >= 1.0) ? maxSide / r : maxSide;
        const QRectF page(12.0 + (14.0 - sw) / 2.0, (height() - sh) / 2.0, sw, sh);
        p.setPen(QPen(pal.text, 1.35));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(page, 2.0, 2.0);

        const QString title = baseFormatTitle(m_baseIndex);
        const QFont tf = nbFont(12.5, QFont::DemiBold);
        p.setFont(tf);
        p.setPen(pal.text);
        p.drawText(QRectF(34, 0, 64, height()), Qt::AlignVCenter | Qt::AlignLeft, title);

        const int dimX = 34 + QFontMetrics(tf).horizontalAdvance(title) + 8;
        p.setFont(nbFont(11, QFont::Normal));
        p.setPen(pal.muted);
        p.drawText(QRectF(dimX, 0, std::max(0, width() - dimX - 30), height()),
                   Qt::AlignVCenter | Qt::AlignLeft, formatDimensionsLabel(m_baseIndex, m_landscape));

        paintUiIcon(&p, QRectF(width() - 24, (height() - 14) / 2.0, 14, 14), QStringLiteral("chevD"), pal.muted);
    }
    void enterEvent(QEnterEvent*) override { m_hover = true; update(); }
    void leaveEvent(QEvent*) override { m_hover = false; update(); }
    void mouseReleaseEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton && rect().contains(e->pos()))
            emit clicked();
    }

private:
    int m_baseIndex = 0;
    bool m_landscape = false;
    bool m_hover = false;
    bool m_dark = false;
};

class FormatPopover : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal progress READ progress WRITE setProgress)
public:
    static constexpr int kMargin = 20;
    static constexpr int kRowH = 36;
    static constexpr int kPad = 8;

    explicit FormatPopover(QWidget* parent)
        : QWidget(parent) {
        setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);
        m_anim = new QPropertyAnimation(this, "progress", this);
        connect(m_anim, &QPropertyAnimation::finished, this, [this] {
            if (m_closing) {
                hide();
                m_closing = false;
            }
        });
        hide();
    }

    qreal progress() const { return m_progress; }
    void setProgress(qreal p) {
        m_progress = std::clamp(p, 0.0, 1.0);
        update();
    }
    bool isClosing() const { return m_closing; }
    void setDark(bool dark) { m_dark = dark; update(); }

    void popup(const QRect& anchorInParent, int currentBaseIdx, bool landscape) {
        if (!parentWidget())
            return;
        m_anim->stop();
        m_closing = false;
        setAttribute(Qt::WA_TransparentForMouseEvents, false);
        if (isVisible())
            hide();

        m_currentBase = std::clamp(currentBaseIdx, 0, 5);
        m_landscape = landscape;
        m_hover = -1;
        setGeometry(parentWidget()->rect());

        const int w = std::max(anchorInParent.width(), 276);
        const int h = kPad * 2 + 6 * kRowH;
        const int x = std::clamp(anchorInParent.left(), 12, std::max(12, width() - w - 12));
        int y = anchorInParent.bottom() + 6;
        if (y + h > height() - 12 && anchorInParent.top() - 6 - h >= 12)
            y = anchorInParent.top() - 6 - h;
        y = std::clamp(y, 12, std::max(12, height() - h - 12));
        m_cardRect = QRect(x, y, w, h);

        if (parentWidget()->isVisible() && !tokens::reducedMotion()) {
            m_grabbing = true;
            m_backdrop = blurBackdropImage(parentWidget()->grab(m_cardRect).toImage(), 11);
            m_grabbing = false;
        } else {
            m_backdrop = QImage();
        }

        setProgress(0.0);
        show();
        raise();
        setFocus(Qt::PopupFocusReason);

        m_anim->setDuration(tokens::motion::duration(tokens::motion::Base));
        m_anim->setEasingCurve(tokens::motion::curve(tokens::motion::out()));
        m_anim->setStartValue(0.0);
        m_anim->setEndValue(1.0);
        m_anim->start();
    }

    void closePopover() {
        if (!isVisible() || m_closing)
            return;
        m_closing = true;
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        m_anim->stop();
        m_anim->setDuration(tokens::motion::duration(tokens::motion::Fast));
        m_anim->setEasingCurve(tokens::motion::curve(tokens::motion::in()));
        m_anim->setStartValue(m_progress);
        m_anim->setEndValue(0.0);
        m_anim->start();
    }

signals:
    void picked(int baseIndex);

protected:
    int hitRow(const QPointF& pos) const {
        for (int i = 0; i < 6; ++i) {
            const QRectF r(m_cardRect.left() + kPad, m_cardRect.top() + kPad + i * kRowH,
                           m_cardRect.width() - 2 * kPad, kRowH);
            if (r.contains(pos))
                return i;
        }
        return -1;
    }

    void paintEvent(QPaintEvent*) override {
        if (m_grabbing)
            return;
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        if (!tokens::reducedMotion()) {
            const qreal sc = m_closing ? (0.98 + 0.02 * m_progress) : (0.96 + 0.04 * m_progress);
            const QPointF o(m_cardRect.center().x(), m_cardRect.top());
            p.translate(o);
            p.scale(sc, sc);
            p.translate(-o);
        }
        p.setOpacity(m_progress);
        const auto pal = tokens::paletteFor(m_dark);
        paintLiquidGlassPanel(p, QRectF(m_cardRect), 14.0, m_backdrop, homePaletteFor(m_dark), m_dark);

        for (int i = 0; i < 6; ++i) {
            const QRectF r(m_cardRect.left() + kPad, m_cardRect.top() + kPad + i * kRowH,
                           m_cardRect.width() - 2 * kPad, kRowH);
            const bool active = (i == m_currentBase);
            if (active || i == m_hover) {
                p.setPen(Qt::NoPen);
                p.setBrush(active ? pal.soft : pal.hover);
                p.drawRoundedRect(r.adjusted(0, 1, 0, -1), 8, 8);
            }

            const PageFormat fmt = resolvePageFormat(i, m_landscape);
            const qreal ratio = formatRatio(fmt);
            const qreal maxS = 14.0;
            const qreal sw = (ratio >= 1.0) ? maxS : maxS * ratio;
            const qreal sh = (ratio >= 1.0) ? maxS / ratio : maxS;
            const QRectF icon(r.left() + 10.0 + (14.0 - sw) / 2.0, r.center().y() - sh / 2.0, sw, sh);
            p.setPen(QPen(active ? pal.text : pal.icon, 1.3));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(icon, 2.0, 2.0);

            p.setFont(nbFont(12.5, active ? QFont::DemiBold : QFont::Medium));
            p.setPen(pal.text);
            p.drawText(QRectF(r.left() + 34, r.top(), 66, r.height()),
                       Qt::AlignVCenter | Qt::AlignLeft, baseFormatTitle(i));

            p.setFont(nbFont(11, QFont::Normal));
            p.setPen(pal.muted);
            p.drawText(QRectF(r.left() + 94, r.top(), r.width() - 122, r.height()),
                       Qt::AlignVCenter | Qt::AlignLeft, formatDimensionsLabel(i, m_landscape));

            if (active)
                paintUiIcon(&p, QRectF(r.right() - 22, r.center().y() - 7, 14, 14), QStringLiteral("check"), pal.accent);
        }
    }

    void mouseMoveEvent(QMouseEvent* e) override {
        if (m_closing)
            return;
        const int h = hitRow(e->position());
        if (h != m_hover) {
            m_hover = h;
            update();
        }
        setCursor(h >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
    }

    void mousePressEvent(QMouseEvent* e) override {
        if (m_closing)
            return;
        if (!m_cardRect.contains(e->pos()))
            closePopover();
    }

    void mouseReleaseEvent(QMouseEvent* e) override {
        if (m_closing || e->button() != Qt::LeftButton)
            return;
        const int h = hitRow(e->position());
        if (h >= 0) {
            closePopover();
            emit picked(h);
        }
    }

    void keyPressEvent(QKeyEvent* e) override {
        if (e->key() == Qt::Key_Escape) {
            closePopover();
            e->accept();
            return;
        }
        QWidget::keyPressEvent(e);
    }

private:
    QRect m_cardRect;
    QImage m_backdrop;
    QPropertyAnimation* m_anim = nullptr;
    qreal m_progress = 1.0;
    int m_currentBase = 0;
    int m_hover = -1;
    bool m_landscape = false;
    bool m_dark = false;
    bool m_closing = false;
    bool m_grabbing = false;
};

// ---------------------------------------------------------------- selector ancho Vertical | Horizontal
class OrientationToggle : public QWidget {
    Q_OBJECT
public:
    explicit OrientationToggle(QWidget* parent) : QWidget(parent) {
        setFixedHeight(32);
        setCursor(Qt::PointingHandCursor);
    }
    void setLandscape(bool landscape) { m_landscape = landscape; update(); }
    void setSupported(bool on) {
        if (m_supported != on) {
            m_supported = on;
            setCursor(on ? Qt::PointingHandCursor : Qt::ArrowCursor);
            update();
        }
    }
    void setDark(bool d) { m_dark = d; update(); }

signals:
    void orientationPicked(bool landscape);

protected:
    QRectF segRect(int idx) const {
        const qreal pad = 3.0;
        const qreal w = (width() - 2.0 * pad) / 2.0;
        return QRectF(pad + idx * w, pad, w, height() - 2.0 * pad);
    }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const auto pal = tokens::paletteFor(m_dark);
        p.setOpacity(m_supported ? 1.0 : 0.45);
        p.setPen(QPen(pal.border, 1.0));
        p.setBrush(pal.field);
        p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 9, 9);

        const int active = m_landscape ? 1 : 0;
        const QString labels[2] = {QStringLiteral("Vertical"), QStringLiteral("Horizontal")};
        const QFont lf(nbFont(11.5, QFont::DemiBold));
        const QFont mf(nbFont(11.5, QFont::Medium));
        for (int i = 0; i < 2; ++i) {
            const QRectF sr = segRect(i);
            const bool on = (i == active);
            if (on) {
                p.setPen(QPen(pal.border, 0.8));
                p.setBrush(pal.surface);
                p.drawRoundedRect(sr, 7, 7);
            }
            const QColor ink = on ? pal.text : pal.muted;
            const QFont& f = on ? lf : mf;
            p.setFont(f);
            const qreal pw = (i == 0) ? 8.0 : 12.0;
            const qreal ph = (i == 0) ? 11.5 : 8.0;
            const int tw = QFontMetrics(f).horizontalAdvance(labels[i]);
            const qreal contentW = pw + 7.0 + tw;
            const qreal x0 = sr.center().x() - contentW / 2.0;
            const QRectF pageIcon(x0, sr.center().y() - ph / 2.0, pw, ph);
            p.setPen(QPen(ink, 1.3));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(pageIcon, 1.8, 1.8);

            p.setPen(ink);
            p.drawText(QRectF(pageIcon.right() + 7.0, sr.top(), tw + 2.0, sr.height()),
                       Qt::AlignVCenter | Qt::AlignLeft, labels[i]);
        }
    }

    void mouseReleaseEvent(QMouseEvent* e) override {
        if (!m_supported || e->button() != Qt::LeftButton)
            return;
        for (int i = 0; i < 2; ++i) {
            if (segRect(i).contains(e->position())) {
                emit orientationPicked(i == 1);
                return;
            }
        }
    }

private:
    bool m_landscape = false;
    bool m_supported = true;
    bool m_dark = false;
};

// ---------------------------------------------------------------- hoja principal
NewNotebookSheet::NewNotebookSheet(QWidget* parent)
    : GlassSheet(parent) {
    setCardWidth(kModalW);
    setHeaderVisible(false);
    setBodyMargins(0, 0, 0, 0);
    bodyLayout()->setSpacing(0);

    auto* shell = new QWidget(this);
    shell->setAttribute(Qt::WA_StyledBackground, false);
    shell->setAutoFillBackground(false);
    shell->setFixedSize(kModalW, kModalH);
    auto* row = new QHBoxLayout(shell);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    bodyLayout()->addWidget(shell);

    // ---- izquierda
    m_left = new QWidget(shell);
    m_left->setAttribute(Qt::WA_StyledBackground, false);
    m_left->setAutoFillBackground(false);
    m_left->setFixedWidth(kLeftW);
    auto* lc = new QVBoxLayout(m_left);
    lc->setContentsMargins(18, 18, 18, 18);
    lc->setSpacing(14);
    m_preview = new NotebookPreview(m_left);
    lc->addWidget(m_preview);

    m_nameEdit = new QLineEdit(m_left);
    m_nameEdit->setPlaceholderText(QStringLiteral("Título del cuaderno"));
    m_nameEdit->setFixedHeight(30);
    lc->addWidget(m_nameEdit);
    connect(m_nameEdit, &QLineEdit::returnPressed, this, [this] { onConfirmed(); });

    m_formatBlock = new QWidget(m_left);
    m_formatBlock->setAttribute(Qt::WA_StyledBackground, false);
    m_formatBlock->setAutoFillBackground(false);
    auto* fc = new QVBoxLayout(m_formatBlock);
    fc->setContentsMargins(0, 0, 0, 0);
    fc->setSpacing(8);
    fc->addWidget(makeKicker(QStringLiteral("Formato"), m_formatBlock, false));

    m_formatBtn = new FormatPickerButton(m_formatBlock);
    fc->addWidget(m_formatBtn);

    m_orientToggle = new OrientationToggle(m_formatBlock);
    connect(m_orientToggle, &OrientationToggle::orientationPicked, this, &NewNotebookSheet::setLandscapeOrientation);
    fc->addWidget(m_orientToggle);
    lc->addWidget(m_formatBlock);
    lc->addStretch(1);

    m_folderChip = new InfoChip(QStringLiteral("folder"), QStringLiteral("Sin carpeta"), true, false, m_left);
    m_folderChip->setMaxWidth(150);
    m_tagChip = new InfoChip(QStringLiteral("hash"), QStringLiteral("Etiqueta"), false, true, m_left);
    m_tagChip->setMaxWidth(120);
    auto* chipRow = new QHBoxLayout();
    chipRow->setContentsMargins(0, 0, 0, 0);
    chipRow->setSpacing(6);
    chipRow->addWidget(m_folderChip);
    chipRow->addWidget(m_tagChip);
    chipRow->addStretch(1);
    lc->addLayout(chipRow);

    m_cta = makeCtaButton(QStringLiteral("Crear cuaderno"), m_left, false);
    connect(m_cta, &QPushButton::clicked, this, [this] { onConfirmed(); });
    lc->addWidget(m_cta);
    row->addWidget(m_left);

    m_divider = new QWidget(shell);
    m_divider->setFixedWidth(1);
    row->addWidget(m_divider);

    // ---- derecha
    m_right = new QWidget(shell);
    m_right->setAttribute(Qt::WA_StyledBackground, false);
    m_right->setAutoFillBackground(false);
    auto* rc = new QVBoxLayout(m_right);
    rc->setContentsMargins(0, 0, 0, 0);
    m_scroll = new QScrollArea(m_right);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->viewport()->setAutoFillBackground(false);
    m_scroll->viewport()->setAttribute(Qt::WA_StyledBackground, false);
    auto* inner = new QWidget();
    inner->setAttribute(Qt::WA_StyledBackground, false);
    inner->setAutoFillBackground(false);
    auto* ic = new QVBoxLayout(inner);
    ic->setContentsMargins(24, 20, 24, 18);
    ic->setSpacing(16);

    // Portada: 8 colores + fila única (clásica, subir imagen, más portadas).
    auto* coverSec = new QVBoxLayout();
    coverSec->setSpacing(8);
    auto* coverHdr = new QHBoxLayout();
    coverHdr->setContentsMargins(0, 0, 44, 0);
    coverHdr->addWidget(makeKicker(QStringLiteral("Portada"), inner, false));
    coverHdr->addStretch(1);
    m_coverDots = new CoverColorDots(inner);
    connect(m_coverDots, &CoverColorDots::picked, this, [this](int i) {
        m_spec.cover.colorIndex = i;
        refreshState();
    });
    coverHdr->addWidget(m_coverDots);
    coverSec->addLayout(coverHdr);

    m_covers = new TileGrid(
        3, 5, 110,
        [this](QPainter& p, const QRectF& c, int i, bool sel, bool hover) {
            const auto pal = tokens::paletteFor(m_covers->dark());
            const QRectF r = c.adjusted(0.75, 0.75, -0.75, -0.75);
            if (i == 0) {
                p.setPen(sel ? QPen(pal.accent, 1.5) : Qt::NoPen);
                p.setBrush(pal.neutral);
                p.drawRoundedRect(r, 11, 11);
                NotebookMeta meta;
                meta.coverColorIndex = std::max(0, m_spec.cover.colorIndex);
                const qreal s = 68.0 / std::max<qreal>(1.0, libobj::bookRect(QPointF(0, 0), meta.pageFormat).width());
                const QPointF bc(c.center().x(), c.bottom() - 11.0 - (hover && !sel ? 2.0 : 0.0));
                p.save();
                p.translate(bc);
                p.scale(s, s);
                libobj::paintBook(p, QPointF(0, 0), meta, m_covers->dark());
                p.restore();
            } else {
                p.setPen(QPen(pal.borderPop, 1.0, Qt::DashLine));
                p.setBrush(pal.neutral);
                p.drawRoundedRect(r, 11, 11);
                const QString icName = (i == 1) ? QStringLiteral("upload") : QStringLiteral("download");
                const QString title = (i == 1) ? QStringLiteral("Subir imagen") : QStringLiteral("Más portadas");
                paintUiIcon(&p, QRectF(c.center().x() - 9, c.center().y() - 21, 18, 18), icName, pal.muted);
                p.setFont(nbFont(10.5, QFont::DemiBold));
                p.setPen(pal.icon);
                p.drawText(QRectF(c.left() + 4, c.center().y() + 1, c.width() - 8, 14), Qt::AlignCenter, title);
                p.setFont(nbFont(9.5, QFont::Normal));
                p.setPen(pal.muted);
                p.drawText(QRectF(c.left() + 4, c.center().y() + 16, c.width() - 8, 13), Qt::AlignCenter,
                           QStringLiteral("Próximamente"));
            }
        },
        inner);
    m_covers->setSelectableCount(1);
    connect(m_covers, &TileGrid::picked, this, [this](int) {
        m_spec.cover.iconIndex = 0;
        refreshState();
    });
    coverSec->addWidget(m_covers);
    ic->addLayout(coverSec);

    // Hoja: 6 tonos de papel + '+' libre + 4 plantillas + «Más plantillas».
    m_pageBlock = new QWidget(inner);
    m_pageBlock->setAttribute(Qt::WA_StyledBackground, false);
    m_pageBlock->setAutoFillBackground(false);
    auto* pageSec = new QVBoxLayout(m_pageBlock);
    pageSec->setContentsMargins(0, 0, 0, 0);
    pageSec->setSpacing(8);
    auto* pageHdr = new QHBoxLayout();
    pageHdr->setContentsMargins(0, 0, 0, 0);
    pageHdr->addWidget(makeKicker(QStringLiteral("Hoja"), m_pageBlock, false));
    pageHdr->addStretch(1);
    m_paperColors = new PaperColorRow(m_pageBlock);
    connect(m_paperColors, &PaperColorRow::picked, this, [this](int i) {
        if (i >= 0 && i < int(quickPaperColors().size())) {
            if (m_colorPop && m_colorPop->isVisible())
                m_colorPop->closePopover();
            applyPaperColor(quickPaperColors().at(i));
        }
    });
    pageHdr->addWidget(m_paperColors);
    pageSec->addLayout(pageHdr);
    m_pages = new TileGrid(
        5, 5, 110,
        [this](QPainter& p, const QRectF& c, int i, bool sel, bool hover) {
            const auto pal = tokens::paletteFor(m_pages->dark());
            const ThemeConfig tc = effectiveThemeConfig(m_spec.theme, m_spec.customBackground);
            const QRectF r = c.adjusted(0.75, 0.75, -0.75, -0.75);
            if (i < 4) {
                p.save();
                p.translate(0, hover && !sel ? -2 : 0);
                p.setPen(QPen(sel ? pal.accent : pal.borderPop, sel ? 1.5 : 1.0));
                p.setBrush(tc.background);
                p.drawRoundedRect(r, 11, 11);
                QPainterPath clip;
                clip.addRoundedRect(r.adjusted(1, 1, -1, -1), 10, 10);
                p.setClipPath(clip);
                paintMiniTemplate(p, r, tc, kTpls[i], m_spec.templateScale);
                p.restore();
            } else {
                p.setPen(QPen(pal.borderPop, 1.0, Qt::DashLine));
                p.setBrush(pal.neutral);
                p.drawRoundedRect(r, 11, 11);
                paintUiIcon(&p, QRectF(c.center().x() - 9, c.center().y() - 21, 18, 18),
                            QStringLiteral("download"), pal.muted);
                p.setFont(nbFont(10.5, QFont::DemiBold));
                p.setPen(pal.icon);
                p.drawText(QRectF(c.left() + 4, c.center().y() + 1, c.width() - 8, 14), Qt::AlignCenter,
                           QStringLiteral("Más plantillas"));
                p.setFont(nbFont(9.5, QFont::Normal));
                p.setPen(pal.muted);
                p.drawText(QRectF(c.left() + 4, c.center().y() + 16, c.width() - 8, 13), Qt::AlignCenter,
                           QStringLiteral("Próximamente"));
            }
        },
        m_pageBlock);
    m_pages->setSelectableCount(4);
    connect(m_pages, &TileGrid::picked, this, [this](int i) {
        m_spec.tpl = kTpls[i];
        refreshState();
    });
    pageSec->addWidget(m_pages);
    ic->addWidget(m_pageBlock);

    // Trama en cuaderno: 4 muestras visuales (4 / 5 / 7 / 10 mm).
    m_spacingBlock = new QWidget(inner);
    m_spacingBlock->setAttribute(Qt::WA_StyledBackground, false);
    m_spacingBlock->setAutoFillBackground(false);
    auto* spSec = new QVBoxLayout(m_spacingBlock);
    spSec->setContentsMargins(0, 0, 0, 0);
    spSec->setSpacing(8);
    auto* spHdr = new QHBoxLayout();
    spHdr->setContentsMargins(0, 0, 0, 0);
    spHdr->addWidget(makeKicker(QStringLiteral("Trama"), m_spacingBlock, false));
    spHdr->addStretch(1);
    m_spacingLabel = new QLabel(QStringLiteral("5 mm"), m_spacingBlock);
    m_spacingLabel->setFont(nbFont(11, QFont::Medium));
    spHdr->addWidget(m_spacingLabel);
    spSec->addLayout(spHdr);

    m_spacings = new TileGrid(
        4, 5, 92,
        [this](QPainter& p, const QRectF& c, int i, bool sel, bool hover) {
            const bool active = m_spec.tpl != BackgroundTemplate::Blank;
            const auto pal = tokens::paletteFor(m_spacings->dark());
            const ThemeConfig tc = effectiveThemeConfig(m_spec.theme, m_spec.customBackground);
            const QRectF r = c.adjusted(0.75, 0.75, -0.75, -21.0);
            p.save();
            p.setOpacity(active ? 1.0 : 0.42);
            p.translate(0, (active && hover && !sel) ? -1.5 : 0.0);
            p.setPen(QPen((sel && active) ? pal.accent : pal.borderPop, (sel && active) ? 1.5 : 1.0));
            p.setBrush(tc.background);
            p.drawRoundedRect(r, 10, 10);
            QPainterPath clip;
            clip.addRoundedRect(r.adjusted(1, 1, -1, -1), 9, 9);
            p.setClipPath(clip);
            const BackgroundTemplate previewTpl = active ? m_spec.tpl : BackgroundTemplate::Dots;
            paintMiniTemplate(p, r, tc, previewTpl, kSpacingScales[i]);
            p.restore();

            p.save();
            p.setOpacity(active ? 1.0 : 0.50);
            p.setFont(nbFont(10.5, (sel && active) ? QFont::DemiBold : QFont::Medium));
            p.setPen((sel && active) ? pal.text : pal.muted);
            p.drawText(QRectF(c.left(), c.bottom() - 16.0, c.width(), 15.0), Qt::AlignCenter,
                       QString::fromUtf8(kSpacingLabels[i]));
            p.restore();
        },
        m_spacingBlock);
    m_spacings->setSelectableCount(4);
    connect(m_spacings, &TileGrid::picked, this, [this](int i) {
        m_spec.templateScale = kSpacingScales[i];
        refreshState();
    });
    spSec->addWidget(m_spacings);
    ic->addWidget(m_spacingBlock);
    ic->addStretch(1);

    m_scroll->setWidget(inner);
    rc->addWidget(m_scroll);
    row->addWidget(m_right, 1);

    m_closeBtn = makeModalCloseButton(m_right);
    m_closeBtn->move(kModalW - kLeftW - 1 - 40, 12);
    connect(m_closeBtn, &QPushButton::clicked, this, [this] { closeSheet(false); });
    m_closeBtn->raise();

    // Popovers hermanos en HomeScreen (evita Qt::Popup).
    m_formatPop = new FormatPopover(parent);
    connect(m_formatPop, &FormatPopover::picked, this, &NewNotebookSheet::selectBaseFormat);
    connect(m_formatBtn, &FormatPickerButton::clicked, this, [this] {
        if (!m_formatPop)
            return;
        if (m_formatPop->isVisible() && !m_formatPop->isClosing()) {
            m_formatPop->closePopover();
            return;
        }
        if (m_colorPop) m_colorPop->closePopover();
        if (m_locPop) m_locPop->closePopover();
        if (m_tagPop) m_tagPop->closePopover();
        const QPoint tl = parentWidget() ? m_formatBtn->mapTo(parentWidget(), QPoint(0, 0)) : QPoint();
        m_formatPop->popup(QRect(tl, m_formatBtn->size()), baseFormatIndex(m_spec.format), m_landscape);
    });

    m_colorPop = new BoardColorPopover(parent);
    connect(m_colorPop, &BoardColorPopover::colorChanged, this, [this](const QColor& c) {
        applyPaperColor(c);
    });
    connect(m_colorPop, &BoardColorPopover::closed, this, [this] {
        if (m_paperColors)
            m_paperColors->setMoreActive(false);
    });
    connect(m_paperColors, &PaperColorRow::moreClicked, this, [this] {
        if (!m_colorPop)
            return;
        if (m_colorPop->isVisible() && !m_colorPop->isClosing()) {
            m_colorPop->closePopover();
            return;
        }
        if (m_formatPop) m_formatPop->closePopover();
        if (m_locPop) m_locPop->closePopover();
        if (m_tagPop) m_tagPop->closePopover();
        const QColor currentBg = effectiveThemeConfig(m_spec.theme, m_spec.customBackground).background;
        m_paperColors->setMoreActive(true);
        m_colorPop->openAnchoredTo(cardRect(), currentBg, isDarkMode());
    });

    m_locPop = new LocationPopover(parent);
    connect(m_locPop, &LocationPopover::picked, this, [this](const QString& rel) {
        m_spec.folderRelativePath = rel;
        refreshState();
    });
    connect(m_folderChip, &InfoChip::clicked, this, [this] {
        if (!m_locPop)
            return;
        if (m_locPop->isVisible() && !m_locPop->isClosing()) {
            m_locPop->closePopover();
            return;
        }
        if (m_formatPop) m_formatPop->closePopover();
        if (m_colorPop) m_colorPop->closePopover();
        if (m_tagPop) m_tagPop->closePopover();
        QVector<LocationEntry> entries;
        entries.append({QString(), QStringLiteral("Sin carpeta"), 0, 0});
        for (const FolderMeta& f : m_folders)
            entries.append({f.relativePath, f.name, f.depth + 1, f.colorIndex});
        m_locPop->setEntries(entries);
        m_locPop->setCurrent(m_spec.folderRelativePath);
        const QPoint tl = parentWidget() ? m_folderChip->mapTo(parentWidget(), QPoint(0, 0)) : QPoint();
        m_locPop->popup(QRect(tl, m_folderChip->size()));
    });

    m_tagPop = new TagPickPopover(parent);
    connect(m_tagPop, &TagPickPopover::selectionChanged, this, [this](const QStringList& ids) {
        m_spec.tagIds = ids;
        refreshState();
    });
    connect(m_tagPop, &TagPickPopover::newTagRequested, this, &NewNotebookSheet::tagCreateRequested);
    connect(m_tagChip, &InfoChip::clicked, this, [this] {
        if (!m_tagPop)
            return;
        if (m_tagPop->isVisible() && !m_tagPop->isClosing()) {
            m_tagPop->closePopover();
            return;
        }
        if (m_formatPop) m_formatPop->closePopover();
        if (m_colorPop) m_colorPop->closePopover();
        if (m_locPop) m_locPop->closePopover();
        m_tagPop->setTags(m_tags);
        m_tagPop->setSelectedTags(m_spec.tagIds);
        const QPoint tl = parentWidget() ? m_tagChip->mapTo(parentWidget(), QPoint(0, 0)) : QPoint();
        m_tagPop->popup(QRect(tl, m_tagChip->size()));
    });
}

void NewNotebookSheet::setFolders(const QVector<FolderMeta>& folders) {
    m_folders = folders;
}

void NewNotebookSheet::setTags(const QVector<TagInfo>& tags) {
    m_tags = tags;
    if (m_tagPop)
        m_tagPop->setTags(tags);
    refreshState();
}

void NewNotebookSheet::closePopovers(bool immediate) {
    if (immediate) {
        if (m_formatPop) m_formatPop->hide();
        if (m_colorPop) m_colorPop->hide();
        if (m_locPop) m_locPop->hide();
        if (m_tagPop) m_tagPop->hide();
        if (m_paperColors) m_paperColors->setMoreActive(false);
        return;
    }
    if (m_formatPop) m_formatPop->closePopover();
    if (m_colorPop) m_colorPop->closePopover();
    if (m_locPop) m_locPop->closePopover();
    if (m_tagPop) m_tagPop->closePopover();
}

void NewNotebookSheet::onClosing() {
    closePopovers(false);
}

void NewNotebookSheet::hideEvent(QHideEvent* event) {
    closePopovers(true);
    GlassSheet::hideEvent(event);
}

void NewNotebookSheet::applyDark(bool dark) {
    const auto pal = tokens::paletteFor(dark);
    m_divider->setStyleSheet(QStringLiteral("background:%1;").arg(pal.border.name(QColor::HexArgb)));
    styleModalTitleEdit(m_nameEdit, dark, 15.0);
    styleModalCloseButton(m_closeBtn, dark);
    m_scroll->setStyleSheet(QStringLiteral(
        "QScrollArea{background:transparent;border:none;}"
        "QScrollBar:vertical{background:transparent;width:8px;margin:6px 2px 6px 0;}"
        "QScrollBar::handle:vertical{background:%1;border-radius:4px;min-height:24px;}"
        "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0px;}"
        "QScrollBar::add-page:vertical,QScrollBar::sub-page:vertical{background:transparent;}")
                                .arg(pal.borderPop.name(QColor::HexArgb)));
    m_spacingLabel->setStyleSheet(QStringLiteral("QLabel{color:%1;background:transparent;}")
                                      .arg(pal.muted.name()));
    m_formatBtn->setState(baseFormatIndex(m_spec.format), m_landscape, dark);
    m_orientToggle->setDark(dark);
    m_coverDots->setDark(dark);
    m_covers->setDark(dark);
    m_pages->setDark(dark);
    m_spacings->setDark(dark);
    m_paperColors->setDark(dark);
    m_paperColors->setMoreActive(false);
    m_folderChip->setDark(dark);
    m_tagChip->setDark(dark);
    if (m_formatPop) m_formatPop->setDark(dark);
    if (m_colorPop) m_colorPop->setDark(dark);
    if (m_locPop) m_locPop->setDark(dark);
    if (m_tagPop) m_tagPop->setDark(dark);
}

void NewNotebookSheet::applyPaperColor(const QColor& color) {
    if (!color.isValid())
        return;
    m_spec.customBackground = color;
    m_spec.theme = themeForPaperColor(color);
    refreshState();
}

void NewNotebookSheet::selectBaseFormat(int baseIndex) {
    m_spec.format = resolvePageFormat(baseIndex, m_landscape);
    refreshState();
}

void NewNotebookSheet::setLandscapeOrientation(bool landscape) {
    m_landscape = landscape;
    m_spec.format = resolvePageFormat(baseFormatIndex(m_spec.format), landscape);
    refreshState();
}

void NewNotebookSheet::openCreate(const QString& folderRelativePath, const HomePalette& pal, bool dark) {
    m_editCoverOnly = false;
    m_landscape = false;
    m_editFilePath.clear();
    m_spec = NewDocumentSpec{};
    m_spec.kind = DocumentKind::Notebook;
    m_spec.format = PageFormat::A4;
    m_spec.customBackground = quickPaperColors().at(0);
    m_spec.theme = themeForPaperColor(m_spec.customBackground);
    m_spec.tpl = BackgroundTemplate::Grid;
    m_spec.templateScale = 1.0;
    m_spec.cover = CoverStyle{2, 0};
    m_spec.folderRelativePath = folderRelativePath;

    applyDark(dark);
    m_formatBlock->show();
    m_pageBlock->show();
    m_spacingBlock->show();
    m_folderChip->show();
    m_tagChip->show();
    m_cta->setText(QStringLiteral("Crear cuaderno"));
    m_nameEdit->setText(QStringLiteral("Cuaderno sin título"));
    openSheet(pal, dark);
    refreshState();
    m_nameEdit->setFocus();
    m_nameEdit->selectAll();
}

void NewNotebookSheet::openEditCover(const NotebookMeta& meta, const HomePalette& pal, bool dark) {
    m_editCoverOnly = true;
    m_editFilePath = meta.path;
    m_spec = NewDocumentSpec{};
    m_spec.theme = meta.theme;
    m_spec.customBackground = meta.customBackground;
    m_spec.tpl = meta.tpl;
    m_spec.format = meta.pageFormat;
    m_landscape = isFormatLandscape(meta.pageFormat);
    m_spec.templateScale = meta.templateScale;
    m_spec.cover = CoverStyle{std::clamp(meta.coverColorIndex, 0, LibraryStore::kCoverColorCount - 1),
                              std::clamp(meta.coverIconIndex, 0, 7)};

    applyDark(dark);
    m_formatBlock->hide();
    m_pageBlock->hide();
    m_spacingBlock->hide();
    m_folderChip->hide();
    m_tagChip->hide();
    m_cta->setText(QStringLiteral("Guardar portada"));
    m_nameEdit->setText(QFileInfo(meta.path).completeBaseName());
    m_nameEdit->setEnabled(false);
    openSheet(pal, dark);
    refreshState();
}

QString NewNotebookSheet::tagChipLabel() const {
    if (m_spec.tagIds.isEmpty())
        return QStringLiteral("Etiqueta");
    if (m_spec.tagIds.size() == 1) {
        for (const TagInfo& t : m_tags) {
            if (t.id == m_spec.tagIds.first())
                return t.name;
        }
        return QStringLiteral("1 etiqueta");
    }
    return QStringLiteral("%1 etiquetas").arg(m_spec.tagIds.size());
}

void NewNotebookSheet::refreshState() {
    m_nameEdit->setEnabled(!m_editCoverOnly);
    const int coverIdx = std::clamp(m_spec.cover.colorIndex, 0, LibraryStore::kCoverColorCount - 1);
    m_coverDots->setCurrent(coverIdx);
    m_covers->setCurrent(0);
    m_covers->update();

    const int baseIdx = baseFormatIndex(m_spec.format);
    m_formatBtn->setState(baseIdx, m_landscape, isDarkMode());
    m_orientToggle->setSupported(formatSupportsOrientation(m_spec.format));
    m_orientToggle->setLandscape(m_spec.format == PageFormat::Slide16x9 ? true : m_landscape);

    const ThemeConfig paperTc = effectiveThemeConfig(m_spec.theme, m_spec.customBackground);
    const QRgb bgRgb = paperTc.background.rgb();
    int pi = -1;
    const auto& papers = quickPaperColors();
    for (int i = 0; i < int(papers.size()); ++i) {
        if (papers.at(i).rgb() == bgRgb) {
            pi = i;
            break;
        }
    }
    m_paperColors->setCurrent(pi);
    m_paperColors->setMoreColor(pi < 0 ? paperTc.background : QColor());

    int ti = 1;
    for (int i = 0; i < 4; ++i)
        if (kTpls[i] == m_spec.tpl)
            ti = i;
    m_pages->setCurrent(ti);
    m_pages->update();

    int si = 1;
    double bestDiff = 1e9;
    for (int i = 0; i < 4; ++i) {
        const double d = std::abs(m_spec.templateScale - kSpacingScales[i]);
        if (d < bestDiff) {
            bestDiff = d;
            si = i;
        }
    }
    const bool hasPattern = (m_spec.tpl != BackgroundTemplate::Blank);
    m_spacings->setEnabled(hasPattern);
    m_spacings->setCurrent(si);
    m_spacings->update();
    m_spacingLabel->setText(hasPattern ? QString::fromUtf8(kSpacingLabels[si])
                                       : QStringLiteral("No aplica en hoja lisa"));

    m_folderChip->setText(m_spec.folderRelativePath.isEmpty()
                              ? QStringLiteral("Sin carpeta")
                              : m_spec.folderRelativePath.section(QLatin1Char('/'), -1));
    m_tagChip->setDashed(m_spec.tagIds.isEmpty());
    m_tagChip->setText(tagChipLabel());
    m_preview->setState(m_spec, !m_editCoverOnly, isDarkMode());
}

void NewNotebookSheet::onConfirmed() {
    if (m_editCoverOnly) {
        const QString path = m_editFilePath;
        const CoverStyle style = m_spec.cover;
        closeSheet(true);
        emit coverEditSubmitted(path, style);
        return;
    }
    const QString clean = LibraryStore::sanitizeBaseName(m_nameEdit->text());
    m_spec.title = clean.isEmpty() ? QStringLiteral("Cuaderno sin título") : clean;
    closeSheet(true);
    emit notebookSubmitted(m_spec);
}

} // namespace finily

#include "NewNotebookSheet.moc"
