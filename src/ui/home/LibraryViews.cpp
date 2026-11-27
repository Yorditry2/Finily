#include "ui/home/LibraryViews.h"

#include "ui/DesignTokens.h"
#include "ui/UiIcons.h"

#include <QFontMetrics>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPropertyAnimation>
#include <QSvgRenderer>
#include <QFile>

#include <algorithm>

namespace finily {
namespace {

QFont pxFont(const QFont& base, qreal px, int weight) {
    QFont f = base;
    f.setPixelSize(int(px + 0.5));
    f.setWeight(QFont::Weight(weight));
    return f;
}

// .text-btn del mockup: píldora con borde fino.
class PillButton : public QWidget {
public:
    PillButton(const QString& text, std::function<void()> run, QWidget* parent)
        : QWidget(parent), m_text(text), m_run(std::move(run)) {
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover, true);
        const QFont f = pxFont(font(), 12, QFont::DemiBold);
        setFixedSize(QFontMetrics(f).horizontalAdvance(text) + 26, 30);
    }
    HomePalette pal = homePaletteFor(false);
    bool dark = false;

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const tokens::Palette tk = tokens::paletteFor(dark);
        p.setPen(QPen(dark ? QColor(255, 255, 255, 26) : QColor(15, 23, 42, 26), 1));
        p.setBrush(underMouse() ? tk.hover : tk.surface);
        p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 15, 15);
        p.setFont(pxFont(font(), 12, QFont::DemiBold));
        p.setPen(tk.icon);
        p.drawText(rect(), Qt::AlignCenter, m_text);
    }
    void enterEvent(QEnterEvent*) override { update(); }
    void leaveEvent(QEvent*) override { update(); }
    void mouseReleaseEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton && rect().contains(e->pos()) && m_run)
            m_run();
    }

private:
    QString m_text;
    std::function<void()> m_run;
};

QSize svgNaturalSize(const QString& pack, const QString& name) {
    QFile f(QStringLiteral(":/resources/icons/%1/%2.svg").arg(pack, name));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QSvgRenderer r(f.readAll());
    return r.defaultSize();
}

} // namespace

// ============================================================== LibraryFilter
bool LibraryFilter::acceptsDocument(const NotebookMeta& meta) const {
    switch (kind) {
    case Boards:
        if (meta.pageFormat != PageFormat::Infinite || meta.hasPdf) return false;
        break;
    case Notebooks:
        if (meta.pageFormat == PageFormat::Infinite || meta.hasPdf) return false;
        break;
    case Pdfs:
        if (!meta.hasPdf) return false;
        break;
    case Folders:
        return false;
    default:
        break;
    }
    const QDate today = QDate::currentDate();
    const QDate d = meta.modified.toLocalTime().date();
    switch (date) {
    case Today: return d == today;
    case LastWeek: return d.daysTo(today) <= 7;
    case ThisMonth: return d.year() == today.year() && d.month() == today.month();
    default: return true;
    }
}

QString LibraryFilter::kindLabel(int k) {
    switch (k) {
    case Boards: return QStringLiteral("Pizarras");
    case Notebooks: return QStringLiteral("Cuadernos");
    case Pdfs: return QStringLiteral("PDF");
    case Folders: return QStringLiteral("Carpetas");
    default: return QStringLiteral("Todo");
    }
}

QString LibraryFilter::kindIcon(int k) {
    switch (k) {
    case Boards: return QStringLiteral("board");
    case Notebooks: return QStringLiteral("book");
    case Pdfs: return QStringLiteral("pdf");
    case Folders: return QStringLiteral("folder");
    default: return {};
    }
}

QString LibraryFilter::dateLabel(int d) {
    switch (d) {
    case Today: return QStringLiteral("Hoy");
    case LastWeek: return QStringLiteral("Últimos 7 días");
    case ThisMonth: return QStringLiteral("Este mes");
    default: return QStringLiteral("Cualquier fecha");
    }
}

QString LibraryFilter::sortLabel(int s) {
    return s == Name ? QStringLiteral("Nombre") : QStringLiteral("Recientes");
}

void sortDocuments(QVector<NotebookMeta>& docs, int sort) {
    if (sort == LibraryFilter::Name) {
        std::stable_sort(docs.begin(), docs.end(), [](const NotebookMeta& a, const NotebookMeta& b) {
            return QString::localeAwareCompare(a.title, b.title) < 0;
        });
    } else {
        std::stable_sort(docs.begin(), docs.end(),
                         [](const NotebookMeta& a, const NotebookMeta& b) { return a.modified > b.modified; });
    }
}

// ============================================================== EmptyStateView
EmptyStateView::EmptyStateView(QWidget* parent)
    : QWidget(parent) {
    setAttribute(Qt::WA_StyledBackground, false);
    m_title = new QLabel(this);
    m_title->setAlignment(Qt::AlignCenter);
    m_title->setFont(pxFont(font(), 15, QFont::DemiBold));
    m_bodyLabel = new QLabel(this);
    m_bodyLabel->setAlignment(Qt::AlignCenter);
    m_bodyLabel->setWordWrap(true);
    m_bodyLabel->setTextFormat(Qt::RichText);
    m_bodyLabel->setFont(pxFont(font(), 12.5, QFont::Normal));
    m_actionsBox = new QWidget(this);
    m_actionsRow = new QHBoxLayout(m_actionsBox);
    m_actionsRow->setContentsMargins(0, 0, 0, 0);
    m_actionsRow->setSpacing(8);
}

void EmptyStateView::applyTheme(const HomePalette& pal, bool dark) {
    m_pal = pal;
    m_dark = dark;
    m_title->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(pal.text.name()));
    m_bodyLabel->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(pal.muted.name()));
    m_bodyLabel->setText(bodyHtml());
    for (QWidget* w : m_actionsBox->findChildren<QWidget*>()) {
        auto* b = dynamic_cast<PillButton*>(w);
        if (!b) continue;
        b->pal = pal;
        b->dark = dark;
        b->update();
    }
    update();
}

QString EmptyStateView::bodyHtml() const {
    const tokens::Palette tk = tokens::paletteFor(m_dark);
    QString html = m_body;
    html.replace(QStringLiteral("<b>"), QStringLiteral("<span style=\"color:%1; font-weight:600;\">").arg(m_pal.text.name()));
    html.replace(QStringLiteral("</b>"), QStringLiteral("</span>"));
    html.replace(QStringLiteral("<kbd>"),
                 QStringLiteral("<span style=\"background:%1; color:%2; font-weight:600; font-size:11px;\">&nbsp;")
                     .arg(tk.field.name(), tk.icon.name()));
    html.replace(QStringLiteral("</kbd>"), QStringLiteral("&nbsp;</span>"));
    return QStringLiteral("<div style=\"line-height:150%;\">%1</div>").arg(html);
}

void EmptyStateView::setContent(const QString& art, const QString& title, const QString& body,
                                const QVector<Action>& actions) {
    m_art = art;
    m_artSize = svgNaturalSize(QStringLiteral("empty"), art);
    m_title->setText(title);
    m_body = body;
    m_bodyLabel->setText(bodyHtml());
    while (QLayoutItem* it = m_actionsRow->takeAt(0)) {
        if (it->widget()) it->widget()->deleteLater();
        delete it;
    }
    for (const Action& a : actions) {
        auto* b = new PillButton(a.label, a.run, m_actionsBox);
        b->pal = m_pal;
        b->dark = m_dark;
        m_actionsRow->addWidget(b);
        b->show();
    }
    m_actionsBox->setVisible(!actions.isEmpty());
    relayout();
    update();
}

int EmptyStateView::preferredHeight() const {
    return m_artSize.height() + 8 + 10 + 22 + 10 + m_bodyLabel->heightForWidth(300) +
           (m_actionsBox->isVisible() ? 16 + 30 : 0) + 8;
}

void EmptyStateView::relayout() {
    const int w = width();
    int y = m_artSize.height() + 8 + 10;
    m_title->setGeometry(0, y, w, 22);
    y += 22 + 10;
    const int bw = std::min(300, w);
    const int bh = m_bodyLabel->heightForWidth(bw);
    m_bodyLabel->setGeometry((w - bw) / 2, y, bw, bh);
    y += bh + 16;
    m_actionsBox->adjustSize();
    m_actionsBox->setGeometry((w - m_actionsBox->width()) / 2, y, m_actionsBox->width(), 30);
}

void EmptyStateView::resizeEvent(QResizeEvent*) { relayout(); }

void EmptyStateView::paintEvent(QPaintEvent*) {
    if (m_art.isEmpty())
        return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QColor line = m_dark ? QColor(0x5A, 0x5B, 0x66) : QColor(0xC4, 0xC4, 0xCB);
    paintUiIcon(&p, QRectF((width() - m_artSize.width()) / 2.0, 0, m_artSize.width(), m_artSize.height()), m_art,
                line, QStringLiteral("empty"));
}

// ============================================================== FirstRunHint
FirstRunHint::FirstRunHint(QWidget* parent)
    : QWidget(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setFixedSize(64, 46);
}

void FirstRunHint::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    QColor c = m_pal.faint;
    paintUiIcon(&p, QRectF(rect()), QStringLiteral("arrow"), c, QStringLiteral("empty"));
}

// ============================================================== ResultGroupLabel
ResultGroupLabel::ResultGroupLabel(const QString& title, int count, QWidget* parent)
    : QWidget(parent), m_title(title.toUpper()), m_count(count) {
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setFixedHeight(24);
}

void ResultGroupLabel::paintEvent(QPaintEvent*) {
    QPainter p(this);
    QFont f = pxFont(font(), 10.5, QFont::Bold);
    f.setLetterSpacing(QFont::PercentageSpacing, 106);
    p.setFont(f);
    p.setPen(m_pal.faint);
    const int tw = QFontMetrics(f).horizontalAdvance(m_title);
    p.drawText(QRect(0, 0, tw + 2, height()), Qt::AlignLeft | Qt::AlignVCenter, m_title);
    p.setFont(pxFont(font(), 10.5, QFont::DemiBold));
    p.drawText(QRect(tw + 8, 0, 60, height()), Qt::AlignLeft | Qt::AlignVCenter, QString::number(m_count));
}

// ============================================================== FilterChipsBar
FilterChipsBar::FilterChipsBar(QWidget* parent)
    : QWidget(parent) {
    setMouseTracking(true);
    setFixedHeight(26);
}

void FilterChipsBar::applyTheme(const HomePalette& pal, bool dark) {
    m_pal = pal;
    m_dark = dark;
    update();
}

void FilterChipsBar::setFilter(const LibraryFilter& filter) {
    m_filter = filter;
    rebuild();
}

void FilterChipsBar::rebuild() {
    m_chips.clear();
    if (m_filter.kind != LibraryFilter::AllKinds)
        m_chips.append({LibraryFilter::kindIcon(m_filter.kind), LibraryFilter::kindLabel(m_filter.kind), 0, {}});
    if (m_filter.date != LibraryFilter::AnyDate)
        m_chips.append({QStringLiteral("clock"), LibraryFilter::dateLabel(m_filter.date), 1, {}});
    if (m_filter.sort != LibraryFilter::Recent)
        m_chips.append({QStringLiteral("sort"), QStringLiteral("Por nombre"), 2, {}});
    const QFontMetrics fm(pxFont(font(), 11.5, QFont::DemiBold));
    qreal x = 0;
    for (Chip& c : m_chips) {
        const qreal w = 10 + (c.icon.isEmpty() ? 0 : 17) + fm.horizontalAdvance(c.label) + 5 + 12 + 8;
        c.rect = QRectF(x, 0, w, 26);
        x += w + 6;
    }
    m_hover = -1;
    update();
}

void FilterChipsBar::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const tokens::Palette tk = tokens::paletteFor(m_dark);
    p.setFont(pxFont(font(), 11.5, QFont::DemiBold));
    for (int i = 0; i < m_chips.size(); ++i) {
        const Chip& c = m_chips.at(i);
        p.setPen(Qt::NoPen);
        p.setBrush(i == m_hover ? m_pal.accentHover : tk.accent);
        p.drawRoundedRect(c.rect, 13, 13);
        qreal x = c.rect.left() + 10;
        if (!c.icon.isEmpty()) {
            paintUiIcon(&p, QRectF(x, 7, 12, 12), c.icon, tk.accentFg);
            x += 17;
        }
        p.setPen(tk.accentFg);
        p.drawText(QRectF(x, 0, c.rect.right() - x, 26), Qt::AlignLeft | Qt::AlignVCenter, c.label);
        QColor xc = tk.accentFg;
        xc.setAlphaF(0.6);
        paintUiIcon(&p, QRectF(c.rect.right() - 20, 7, 12, 12), QStringLiteral("x"), xc);
    }
}

void FilterChipsBar::mouseMoveEvent(QMouseEvent* e) {
    int h = -1;
    for (int i = 0; i < m_chips.size(); ++i)
        if (m_chips.at(i).rect.contains(e->position())) h = i;
    if (h != m_hover) {
        m_hover = h;
        setCursor(h >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void FilterChipsBar::leaveEvent(QEvent*) {
    m_hover = -1;
    update();
}

void FilterChipsBar::mouseReleaseEvent(QMouseEvent* e) {
    for (const Chip& c : m_chips) {
        if (!c.rect.contains(e->position()))
            continue;
        LibraryFilter f = m_filter;
        if (c.field == 0) f.kind = LibraryFilter::AllKinds;
        else if (c.field == 1) f.date = LibraryFilter::AnyDate;
        else f.sort = LibraryFilter::Recent;
        emit filterChanged(f);
        return;
    }
}

// ============================================================== FilterPanel
namespace {
constexpr int kPanelW = 300;
constexpr int kChipH = 26;
} // namespace

FilterPanel::FilterPanel(QWidget* parent)
    : QWidget(parent) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    m_anim = new QPropertyAnimation(this, "progress", this);
    m_anim->setDuration(tokens::motion::Fast);
    m_anim->setEasingCurve(tokens::motion::out());
    hide();
}

void FilterPanel::applyTheme(const HomePalette& pal, bool dark) {
    m_pal = pal;
    m_dark = dark;
    update();
}

void FilterPanel::setProgress(qreal p) {
    m_progress = std::clamp(p, 0.0, 1.0);
    update();
}

void FilterPanel::popup(const QRect& anchor, const LibraryFilter& filter, bool) {
    if (!parentWidget())
        return;
    m_anchor = anchor;
    m_filter = filter;
    setGeometry(parentWidget()->rect());
    layoutPanel();
    show();
    raise();
    setFocus(Qt::PopupFocusReason);
    m_anim->stop();
    m_anim->setStartValue(0.0);
    m_anim->setEndValue(1.0);
    m_anim->start();
}

void FilterPanel::closePanel() {
    hide();
}

void FilterPanel::layoutPanel() {
    m_hits.clear();
    const QFontMetrics fm(pxFont(font(), 11.5, QFont::DemiBold));
    const qreal left = std::max(12, std::min(m_anchor.right() - kPanelW + 34, width() - kPanelW - 12));
    const qreal top = m_anchor.bottom() + 8;
    const qreal innerL = left + 14;
    const qreal innerR = left + kPanelW - 14;
    qreal y = top + 12;

    auto chipRow = [&](int field, const QVector<int>& values, auto labelFn, auto iconFn) {
        y += 16 + 6; // rótulo
        qreal x = innerL;
        for (int v : values) {
            const QString icon = iconFn(v);
            const qreal w = 20 + (icon.isEmpty() ? 0 : 17) + fm.horizontalAdvance(labelFn(v));
            if (x + w > innerR) {
                x = innerL;
                y += kChipH + 5;
            }
            m_hits.append({QRectF(x, y, w, kChipH), field, v});
            x += w + 5;
        }
        y += kChipH + 12;
    };
    chipRow(0, {0, 1, 2, 3, 4}, LibraryFilter::kindLabel, LibraryFilter::kindIcon);
    chipRow(1, {0, 1, 2, 3}, LibraryFilter::dateLabel, [](int) { return QString(); });
    // Ordenar por: control segmentado.
    y += 16 + 6;
    qreal x = innerL + 2;
    for (int v : {0, 1}) {
        const qreal w = 22 + fm.horizontalAdvance(LibraryFilter::sortLabel(v));
        m_hits.append({QRectF(x, y + 2, w, kChipH), 2, v});
        x += w + 2;
    }
    y += kChipH + 4 + 12;
    // Pie: línea, «Restablecer» y cuenta.
    y += 10;
    m_hits.append({QRectF(innerL - 4, y - 4, fm.horizontalAdvance(QStringLiteral("Restablecer")) + 8, 22), 3, 0});
    y += 14 + 14;
    m_panel = QRectF(left, top, kPanelW, y - top);
}

void FilterPanel::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const tokens::Palette tk = tokens::paletteFor(m_dark);
    p.setOpacity(m_progress);
    const QPointF origin(m_panel.right() - 30, m_panel.top());
    const qreal sc = 0.97 + 0.03 * m_progress;
    p.translate(origin);
    p.scale(sc, sc);
    p.translate(-origin);

    paintSoftShadow(p, m_panel, 14, 4, m_dark);
    p.setPen(QPen(tk.borderPop, 1));
    p.setBrush(tk.surface);
    p.drawRoundedRect(m_panel.adjusted(0.5, 0.5, -0.5, -0.5), 14, 14);

    const qreal innerL = m_panel.left() + 14;
    QFont head = pxFont(font(), 10.5, QFont::Bold);
    head.setLetterSpacing(QFont::PercentageSpacing, 106);
    auto drawHead = [&](const QString& t, qreal yTop) {
        p.setFont(head);
        p.setPen(tk.faint);
        p.drawText(QRectF(innerL, yTop, 200, 16), Qt::AlignLeft | Qt::AlignVCenter, t.toUpper());
    };

    const QFont chipFont = pxFont(font(), 11.5, QFont::DemiBold);
    // Rótulos: justo encima de la primera ficha de cada sección.
    const QString heads[] = {QStringLiteral("Tipo"), QStringLiteral("Modificado"), QStringLiteral("Ordenar por")};
    for (int field = 0; field < 3; ++field) {
        for (const Hit& h : m_hits) {
            if (h.field == field) {
                drawHead(heads[field], h.rect.top() - (field == 2 ? 24 : 22));
                break;
            }
        }
    }
    // Fondo del segmentado.
    QRectF segBg;
    for (const Hit& h : m_hits)
        if (h.field == 2) segBg = segBg.isNull() ? h.rect : segBg.united(h.rect);
    p.setPen(Qt::NoPen);
    p.setBrush(tk.field);
    p.drawRoundedRect(segBg.adjusted(-2, -2, 2, 2), 9, 9);

    p.setFont(chipFont);
    for (int i = 0; i < m_hits.size(); ++i) {
        const Hit& h = m_hits.at(i);
        if (h.field == 3) {
            p.setPen(QPen(tk.border, 1));
            p.drawLine(QPointF(innerL, h.rect.top() - 6), QPointF(m_panel.right() - 14, h.rect.top() - 6));
            p.setFont(pxFont(font(), 12, QFont::DemiBold));
            p.setPen(i == m_hover ? tk.text : tk.muted);
            p.drawText(h.rect, Qt::AlignCenter, QStringLiteral("Restablecer"));
            const int n = m_filter.activeCount();
            p.setFont(pxFont(font(), 11, QFont::Normal));
            p.setPen(tk.faint);
            p.drawText(QRectF(m_panel.right() - 114, h.rect.top(), 100, h.rect.height()), Qt::AlignRight | Qt::AlignVCenter,
                       n == 0 ? QStringLiteral("Sin filtros") : n == 1 ? QStringLiteral("1 activo")
                                                                     : QStringLiteral("%1 activos").arg(n));
            p.setFont(chipFont);
            continue;
        }
        const int current = h.field == 0 ? m_filter.kind : h.field == 1 ? m_filter.date : m_filter.sort;
        const bool on = current == h.value;
        const QString label = h.field == 0 ? LibraryFilter::kindLabel(h.value)
                              : h.field == 1 ? LibraryFilter::dateLabel(h.value)
                                             : LibraryFilter::sortLabel(h.value);
        QColor fg;
        if (h.field == 2) {
            if (on) {
                p.setPen(Qt::NoPen);
                p.setBrush(tk.surface);
                p.drawRoundedRect(h.rect, 7, 7);
            }
            fg = on ? tk.text : (i == m_hover ? tk.icon : tk.muted);
        } else {
            p.setPen(Qt::NoPen);
            p.setBrush(on ? tk.accent : (i == m_hover ? tk.hover : tk.field));
            p.drawRoundedRect(h.rect, h.rect.height() / 2, h.rect.height() / 2);
            fg = on ? tk.accentFg : tk.icon;
        }
        qreal x = h.rect.left() + (h.field == 2 ? 11 : 10);
        const QString icon = h.field == 0 ? LibraryFilter::kindIcon(h.value) : QString();
        if (!icon.isEmpty()) {
            paintUiIcon(&p, QRectF(x, h.rect.top() + 7, 12, 12), icon, fg);
            x += 17;
        }
        p.setPen(fg);
        p.drawText(QRectF(x, h.rect.top(), h.rect.right() - x, h.rect.height()), Qt::AlignLeft | Qt::AlignVCenter,
                   label);
    }
}

void FilterPanel::mouseMoveEvent(QMouseEvent* e) {
    int h = -1;
    for (int i = 0; i < m_hits.size(); ++i)
        if (m_hits.at(i).rect.contains(e->position())) h = i;
    if (h != m_hover) {
        m_hover = h;
        setCursor(h >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void FilterPanel::mousePressEvent(QMouseEvent* e) {
    if (!m_panel.contains(e->position())) {
        closePanel();
        return;
    }
    for (const Hit& h : m_hits) {
        if (!h.rect.contains(e->position()))
            continue;
        if (h.field == 0) m_filter.kind = h.value;
        else if (h.field == 1) m_filter.date = h.value;
        else if (h.field == 2) m_filter.sort = h.value;
        else m_filter = LibraryFilter{};
        update();
        emit filterChanged(m_filter);
        return;
    }
}

void FilterPanel::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape) {
        closePanel();
        return;
    }
    QWidget::keyPressEvent(e);
}

} // namespace finily
