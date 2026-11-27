#include "ui/home/NewNotebookSheet.h"

#include "ui/DesignTokens.h"
#include "ui/UiIcons.h"
#include "ui/home/LibraryObjects.h"
#include "ui/home/SheetWidgets.h"

#include <QHBoxLayout>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QFileInfo>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace finily {
namespace {

constexpr int kModalW = 880, kModalH = 600, kLeftW = 312;
const ThemeId kPapers[] = {ThemeId::PaperLight, ThemeId::PaperCream, ThemeId::DarkBoard};
const BackgroundTemplate kTpls[] = {BackgroundTemplate::Blank, BackgroundTemplate::Grid,
                                    BackgroundTemplate::Dots, BackgroundTemplate::Ruled};

qreal formatRatio(PageFormat f) { // ancho / alto
    switch (f) {
        case PageFormat::A4Landscape: return 1123.0 / 794.0;
        case PageFormat::Slide16x9: return 16.0 / 9.0;
        default: return 794.0 / 1123.0;
    }
}

} // namespace

// ---------------------------------------------------------------- vista previa
class NotebookPreview : public QWidget {
public:
    explicit NotebookPreview(QWidget* parent) : QWidget(parent) { setFixedHeight(268); }
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
        const qreal gap = 14.0;
        const qreal availW = width() - 40.0, availH = height() - 40.0;
        qreal h = std::min(availH, (availW - gap) / (2.0 * r));
        qreal w = h * r;
        const qreal cx = width() / 2.0, cy = height() / 2.0;
        const qreal totalW = 2 * w + gap;

        // Cuaderno: se escala hasta igualar el alto de la hoja.
        const qreal s = h / natural.height();
        p.save();
        p.translate(cx - totalW / 2.0 + (w / 2.0), cy + h / 2.0);
        p.scale(s * std::min(1.0, w / (natural.width() * s)), s);
        libobj::paintBook(p, QPointF(0, 0), meta, m_dark);
        p.restore();

        const QRectF sheet(cx - totalW / 2.0 + w + gap, cy - h / 2.0, w, h);
        const ThemeConfig tc = getThemeConfig(m_spec.theme);
        paintSoftShadow(p, sheet, 5, 1.0, m_dark);
        p.setPen(Qt::NoPen);
        p.setBrush(tc.background);
        p.drawRoundedRect(sheet, 5, 5);
        p.save();
        QPainterPath clip;
        clip.addRoundedRect(sheet, 5, 5);
        p.setClipPath(clip);
        paintMiniTemplate(p, sheet, tc, m_spec.tpl, m_spec.templateScale);
        p.restore();
    }

private:
    NewDocumentSpec m_spec;
    bool m_sheet = true, m_dark = false;
};

// ---------------------------------------------------------------- cuadrícula de muestras
// Cada muestra se pinta con una función; admite una muestra final «más» no seleccionable.
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
    }
    void leaveEvent(QEvent*) override { m_hover = -1; update(); }
    void mouseReleaseEvent(QMouseEvent* e) override {
        const int h = hit(e->position());
        if (h >= 0 && h < m_selectable)
            emit picked(h);
    }

private:
    int m_count, m_cols;
    qreal m_tileH;
    Painter m_paint;
    int m_current = 0, m_hover = -1, m_selectable = 1 << 20;
    qreal m_gapX = 14, m_gapY = 16;
    bool m_dark = false;
};

// ---------------------------------------------------------------- puntos de papel
class PaperDots : public QWidget {
    Q_OBJECT
public:
    explicit PaperDots(QWidget* parent) : QWidget(parent) {
        setFixedSize(3 * 26 + 6, 26);
        setCursor(Qt::PointingHandCursor);
    }
    void setCurrent(int i) { m_current = i; update(); }
    void setDark(bool d) { m_dark = d; update(); }
signals:
    void picked(int);

protected:
    QRectF cell(int i) const { return QRectF(i * 26 + 5, 4, 18, 18); }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const auto pal = tokens::paletteFor(m_dark);
        for (int i = 0; i < 3; ++i) {
            const QRectF c = cell(i);
            if (i == m_current) {
                p.setPen(QPen(pal.accent, 1.5));
                p.setBrush(Qt::NoBrush);
                p.drawEllipse(c.adjusted(-3.5, -3.5, 3.5, 3.5));
            }
            p.setPen(QPen(QColor(15, 23, 42, 41), 1.0));
            p.setBrush(getThemeConfig(kPapers[i]).background);
            p.drawEllipse(c);
        }
    }
    void mouseReleaseEvent(QMouseEvent* e) override {
        for (int i = 0; i < 3; ++i)
            if (cell(i).adjusted(-4, -4, 4, 4).contains(e->position()))
                emit picked(i);
    }

private:
    int m_current = 0;
    bool m_dark = false;
};

// ---------------------------------------------------------------- formatos
class FormatRow : public QWidget {
    Q_OBJECT
public:
    explicit FormatRow(QWidget* parent) : QWidget(parent) {
        setFixedHeight(58);
        setCursor(Qt::PointingHandCursor);
    }
    void setCurrent(PageFormat f) { m_current = f; update(); }
    void setDark(bool d) { m_dark = d; update(); }
signals:
    void picked(PageFormat f);

protected:
    static constexpr PageFormat kFmts[3] = {PageFormat::A4, PageFormat::A4Landscape, PageFormat::Slide16x9};
    static const char* label(int i) {
        static const char* l[] = {"A4", "A4 ↔", "16:9"};
        return l[i];
    }
    QRectF cell(int i) const {
        const qreal g = 6, w = (width() - 2 * g) / 3.0;
        return QRectF(i * (w + g), 0, w, height());
    }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const auto pal = tokens::paletteFor(m_dark);
        for (int i = 0; i < 3; ++i) {
            const QRectF c = cell(i).adjusted(0.5, 0.5, -0.5, -0.5);
            const bool on = kFmts[i] == m_current;
            p.setPen(QPen(on ? pal.accent : pal.border, on ? 1.5 : 1.0));
            p.setBrush(on ? pal.surface : pal.neutral);
            p.drawRoundedRect(c, 10, 10);
            const qreal r = formatRatio(kFmts[i]);
            const qreal sh = r >= 1 ? 26.0 / r * 0.82 : 26.0 * 0.82;
            const qreal sw = r >= 1 ? 26.0 * 0.82 : 26.0 * r * 0.82;
            QRectF shape(c.center().x() - sw / 2, c.top() + 7 + (21.3 - sh) / 2, sw, sh);
            p.setPen(QPen(on ? pal.text : pal.muted, 1.4));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(shape, 2.5, 2.5);
            QFont f = font();
            f.setPixelSize(10);
            f.setWeight(QFont::DemiBold);
            p.setFont(f);
            p.setPen(on ? pal.text : pal.muted);
            p.drawText(QRectF(c.left(), c.bottom() - 17, c.width(), 14), Qt::AlignCenter,
                       QString::fromUtf8(label(i)));
        }
    }
    void mouseReleaseEvent(QMouseEvent* e) override {
        for (int i = 0; i < 3; ++i)
            if (cell(i).contains(e->position()))
                emit picked(kFmts[i]);
    }

private:
    PageFormat m_current = PageFormat::A4;
    bool m_dark = false;
};
constexpr PageFormat FormatRow::kFmts[3];

// ---------------------------------------------------------------- hoja principal
NewNotebookSheet::NewNotebookSheet(QWidget* parent)
    : GlassSheet(parent) {
    setCardWidth(kModalW);
    setHeaderVisible(false);
    setBodyMargins(0, 0, 0, 0);
    bodyLayout()->setSpacing(0);

    auto* shell = new QWidget(this);
    shell->setAttribute(Qt::WA_StyledBackground, false);
    shell->setFixedSize(kModalW, kModalH);
    auto* row = new QHBoxLayout(shell);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    bodyLayout()->addWidget(shell);

    // ---- izquierda
    m_left = new QWidget(shell);
    m_left->setFixedWidth(kLeftW);
    auto* lc = new QVBoxLayout(m_left);
    lc->setContentsMargins(18, 18, 18, 18);
    lc->setSpacing(16);
    m_preview = new NotebookPreview(m_left);
    lc->addWidget(m_preview);

    m_nameEdit = new QLineEdit(m_left);
    m_nameEdit->setPlaceholderText(QStringLiteral("Título del cuaderno"));
    m_nameEdit->setFixedHeight(30);
    lc->addWidget(m_nameEdit);
    connect(m_nameEdit, &QLineEdit::returnPressed, this, [this] { onConfirmed(); });

    m_formatBlock = new QWidget(m_left);
    auto* fc = new QVBoxLayout(m_formatBlock);
    fc->setContentsMargins(0, 0, 0, 0);
    fc->setSpacing(11);
    fc->addWidget(makeKicker(QStringLiteral("Formato"), m_formatBlock, false));
    m_formats = new FormatRow(m_formatBlock);
    connect(m_formats, &FormatRow::picked, this, [this](PageFormat f) {
        m_spec.format = f;
        refreshState();
    });
    fc->addWidget(m_formats);
    lc->addWidget(m_formatBlock);
    lc->addStretch(1);

    m_folderChip = new InfoChip(QStringLiteral("folder"), QStringLiteral("Sin carpeta"), false, false, m_left);
    m_folderChip->setCursor(Qt::ArrowCursor);
    auto* chipRow = new QHBoxLayout();
    chipRow->setContentsMargins(0, 0, 0, 0);
    chipRow->addWidget(m_folderChip);
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
    auto* rc = new QVBoxLayout(m_right);
    rc->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(m_right);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->viewport()->setAutoFillBackground(false);
    scroll->setStyleSheet(QStringLiteral("QScrollArea{background:transparent;}"));
    auto* inner = new QWidget();
    inner->setAttribute(Qt::WA_StyledBackground, false);
    auto* ic = new QVBoxLayout(inner);
    ic->setContentsMargins(24, 22, 24, 24);
    ic->setSpacing(24);

    // Portadas: 8 colores + muestra «desde imagen» (próximamente).
    auto* coverSec = new QVBoxLayout();
    coverSec->setSpacing(11);
    coverSec->addWidget(makeKicker(QStringLiteral("Portada"), inner, false));
    m_covers = new TileGrid(
        9, 5, 120,
        [this](QPainter& p, const QRectF& c, int i, bool sel, bool hover) {
            const auto pal = tokens::paletteFor(m_covers->dark());
            if (sel || hover) {
                p.setPen(sel ? QPen(pal.accent, 1.5) : Qt::NoPen);
                p.setBrush(pal.neutral);
                p.drawRoundedRect(c.adjusted(0.75, 0.75, -0.75, -0.75), 12, 12);
            }
            const qreal s = 72.0 / 104.0;
            const QPointF bc(c.center().x(), c.bottom() - 12 - (hover && !sel ? 2 : 0));
            if (i < 8) {
                NotebookMeta meta;
                meta.coverColorIndex = i;
                p.save();
                p.translate(bc);
                p.scale(s, s);
                libobj::paintBook(p, QPointF(0, 0), meta, m_covers->dark());
                p.restore();
            } else {
                const QRectF b(bc.x() - 36, bc.y() - 96, 72, 96);
                p.setPen(QPen(pal.borderPop, 1.5, Qt::DashLine));
                p.setBrush(pal.surface);
                p.drawRoundedRect(b, 8, 8);
                paintUiIcon(&p, QRectF(b.center().x() - 10, b.center().y() - 10, 20, 20),
                            QStringLiteral("upload"), pal.muted);
            }
        },
        inner);
    m_covers->setSelectableCount(8);
    connect(m_covers, &TileGrid::picked, this, [this](int i) {
        m_spec.cover.colorIndex = i;
        refreshState();
    });
    coverSec->addWidget(m_covers);
    ic->addLayout(coverSec);

    // Hojas: 4 plantillas + «Más plantillas».
    m_pageBlock = new QWidget(inner);
    auto* pageSec = new QVBoxLayout(m_pageBlock);
    pageSec->setContentsMargins(0, 0, 0, 0);
    pageSec->setSpacing(11);
    auto* pageHdr = new QHBoxLayout();
    pageHdr->setContentsMargins(0, 0, 0, 0);
    pageHdr->addWidget(makeKicker(QStringLiteral("Hoja"), m_pageBlock, false));
    pageHdr->addStretch(1);
    m_papers = new PaperDots(m_pageBlock);
    connect(m_papers, &PaperDots::picked, this, [this](int i) {
        m_spec.theme = kPapers[i];
        refreshState();
    });
    pageHdr->addWidget(m_papers);
    pageSec->addLayout(pageHdr);
    m_pages = new TileGrid(
        5, 5, 124,
        [this](QPainter& p, const QRectF& c, int i, bool sel, bool hover) {
            const auto pal = tokens::paletteFor(m_pages->dark());
            const ThemeConfig tc = getThemeConfig(m_spec.theme);
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
                paintUiIcon(&p, QRectF(c.center().x() - 10, c.center().y() - 22, 20, 20),
                            QStringLiteral("download"), pal.muted);
                QFont f = font();
                f.setPixelSize(11);
                f.setWeight(QFont::DemiBold);
                p.setFont(f);
                p.setPen(pal.icon);
                p.drawText(QRectF(c.left(), c.center().y() + 2, c.width(), 15), Qt::AlignCenter,
                           QStringLiteral("Más plantillas"));
                f.setPixelSize(9);
                f.setWeight(QFont::Normal);
                p.setFont(f);
                p.setPen(pal.muted);
                p.drawText(QRectF(c.left(), c.center().y() + 18, c.width(), 13), Qt::AlignCenter,
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
    ic->addStretch(1);

    scroll->setWidget(inner);
    rc->addWidget(scroll);
    row->addWidget(m_right, 1);

    // Cierre redondo en la esquina superior derecha de la galería.
    auto* close = new QPushButton(m_right);
    close->setFixedSize(28, 28);
    close->setCursor(Qt::PointingHandCursor);
    close->setFocusPolicy(Qt::NoFocus);
    close->setObjectName(QStringLiteral("nbClose"));
    close->move(kModalW - kLeftW - 1 - 40, 12);
    connect(close, &QPushButton::clicked, this, [this] { closeSheet(false); });
    close->raise();
}

void NewNotebookSheet::applyDark(bool dark) {
    const auto pal = tokens::paletteFor(dark);
    m_divider->setStyleSheet(QStringLiteral("background:%1;").arg(pal.border.name(QColor::HexArgb)));
    m_nameEdit->setStyleSheet(QStringLiteral(
        "QLineEdit{background:transparent;border:0;border-bottom:1px solid %1;padding:0 0 6px 0;"
        "font-size:15px;font-weight:600;color:%2;}QLineEdit:focus{border-bottom:1px solid %3;}")
                                  .arg(pal.borderPop.name(QColor::HexArgb), pal.text.name(), pal.accent.name()));
    if (auto* close = m_right->findChild<QPushButton*>(QStringLiteral("nbClose"))) {
        close->setIcon(uiIcon(QStringLiteral("x"), pal.icon, 14));
        close->setIconSize(QSize(14, 14));
        close->setStyleSheet(QStringLiteral(
            "QPushButton{background:%1;border:0;border-radius:14px;}QPushButton:hover{background:%2;}")
                                 .arg(pal.field.name(), pal.hover.name(QColor::HexArgb)));
    }
    m_formats->setDark(dark);
    m_covers->setDark(dark);
    m_pages->setDark(dark);
    m_papers->setDark(dark);
    m_folderChip->setDark(dark);
}

void NewNotebookSheet::openCreate(const QString& folderRelativePath, const HomePalette& pal, bool dark) {
    m_editCoverOnly = false;
    m_editFilePath.clear();
    m_spec = NewDocumentSpec{};
    m_spec.kind = DocumentKind::Notebook;
    m_spec.format = PageFormat::A4;
    m_spec.theme = ThemeId::PaperLight;
    m_spec.tpl = BackgroundTemplate::Grid;
    m_spec.templateScale = 1.0;
    m_spec.cover = CoverStyle{2, 0}; // Salvia, como en el mockup
    m_spec.folderRelativePath = folderRelativePath;

    applyDark(dark);
    m_formatBlock->show();
    m_pageBlock->show();
    m_cta->setText(QStringLiteral("Crear cuaderno"));
    m_nameEdit->setText(QStringLiteral("Cuaderno sin título"));
    m_folderChip->setText(folderRelativePath.isEmpty() ? QStringLiteral("Sin carpeta")
                                                       : folderRelativePath.section('/', -1));
    m_folderChip->show();
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
    m_spec.tpl = meta.tpl;
    m_spec.format = meta.pageFormat;
    m_spec.templateScale = meta.templateScale;
    m_spec.cover = CoverStyle{std::clamp(meta.coverColorIndex, 0, LibraryStore::kCoverColorCount - 1),
                              std::clamp(meta.coverIconIndex, 0, 7)};

    applyDark(dark);
    m_formatBlock->hide();
    m_pageBlock->hide();
    m_folderChip->hide();
    m_cta->setText(QStringLiteral("Guardar portada"));
    m_nameEdit->setText(QFileInfo(meta.path).completeBaseName());
    m_nameEdit->setEnabled(false);
    openSheet(pal, dark);
    refreshState();
}

void NewNotebookSheet::refreshState() {
    m_nameEdit->setEnabled(!m_editCoverOnly);
    m_covers->setCurrent(std::clamp(m_spec.cover.colorIndex, 0, 7));
    m_formats->setCurrent(m_spec.format);
    int pi = 0;
    for (int i = 0; i < 3; ++i)
        if (kPapers[i] == m_spec.theme)
            pi = i;
    m_papers->setCurrent(pi);
    int ti = 1;
    for (int i = 0; i < 4; ++i)
        if (kTpls[i] == m_spec.tpl)
            ti = i;
    m_pages->setCurrent(ti);
    m_pages->update();
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
