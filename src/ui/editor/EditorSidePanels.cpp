#include "ui/editor/EditorSidePanels.h"

#include "core/Utils.h"
#include "ui/CanvasWidget.h"
#include "ui/UiIcons.h"

#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QScrollArea>
#include <QVBoxLayout>

#include <algorithm>

namespace finily {

namespace {

class ElementTileWidget : public QWidget {
    Q_OBJECT

public:
    ElementTileWidget(const QString& title, const QPixmap& thumb, bool custom, QWidget* parent)
        : QWidget(parent),
          m_title(title),
          m_thumb(thumb),
          m_custom(custom) {
        setFixedSize(124, 78);
        setCursor(Qt::PointingHandCursor);
        setMouseTracking(true);
        m_pal = editorPaletteFor(false);
    }

    void applyEditorTheme(bool dark) {
        m_pal = editorPaletteFor(dark);
        update();
    }

signals:
    void clicked();
    void contextMenuRequested(const QPoint& globalPos);

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF r(rect());
        p.setPen(QPen(m_hovered ? m_pal.border : m_pal.borderSoft, 1.0));
        p.setBrush(m_hovered ? m_pal.hover : m_pal.subtle);
        p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 10.0, 10.0);

        if (!m_thumb.isNull()) {
            const QRectF target(8.0, 6.0, width() - 16.0, 44.0);
            const QSizeF scaled = QSizeF(m_thumb.size()).scaled(target.size(), Qt::KeepAspectRatio);
            const QRectF dst(target.center().x() - scaled.width() * 0.5,
                             target.center().y() - scaled.height() * 0.5,
                             scaled.width(), scaled.height());
            p.drawPixmap(dst.toRect(), m_thumb);
        }

        p.setFont(editorFont(10.5, QFont::Medium));
        p.setPen(m_pal.text);
        const QRectF textR(6.0, height() - 23.0, width() - 12.0, 18.0);
        const QString elided = QFontMetricsF(p.font()).elidedText(m_title, Qt::ElideRight, textR.width());
        p.drawText(textR, Qt::AlignCenter, elided);
    }

    void enterEvent(QEnterEvent*) override { m_hovered = true; update(); }
    void leaveEvent(QEvent*) override { m_hovered = false; update(); }
    void mouseReleaseEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton && rect().contains(e->pos()))
            emit clicked();
        else if (e->button() == Qt::RightButton && m_custom)
            emit contextMenuRequested(e->globalPosition().toPoint());
    }
    void tabletEvent(QTabletEvent* e) override { e->ignore(); }

private:
    QString m_title;
    QPixmap m_thumb;
    bool m_custom = false;
    bool m_hovered = false;
    EditorPalette m_pal;
};

class PageThumbWidget : public QWidget {
    Q_OBJECT

public:
    PageThumbWidget(int pageIdx, const QImage& thumb, bool active, QWidget* parent)
        : QWidget(parent),
          m_index(pageIdx),
          m_thumb(thumb),
          m_active(active) {
        setFixedSize(124, 144);
        setCursor(Qt::PointingHandCursor);
        setMouseTracking(true);
        m_pal = editorPaletteFor(false);
    }

    void applyEditorTheme(bool dark) {
        m_pal = editorPaletteFor(dark);
        update();
    }

signals:
    void clicked(int index);
    void contextMenuRequested(int index, const QPoint& globalPos);

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRectF card(16.0, 4.0, 92.0, 118.0);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(15, 23, 42, 22));
        p.drawRoundedRect(card.translated(0.0, 2.0), 6.0, 6.0);

        QPainterPath clip;
        clip.addRoundedRect(card, 6.0, 6.0);
        p.save();
        p.setClipPath(clip);
        if (!m_thumb.isNull())
            p.drawImage(card, m_thumb);
        else
            p.fillRect(card, m_pal.surfaceSolid);
        p.restore();

        p.setPen(QPen(m_active ? m_pal.accent : m_pal.border, m_active ? 2.0 : 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(card, 6.0, 6.0);

        p.setFont(editorFont(11.0, m_active ? QFont::Bold : QFont::Medium));
        p.setPen(m_active ? m_pal.text : m_pal.muted);
        p.drawText(QRectF(0.0, 125.0, width(), 18.0), Qt::AlignCenter, QString::number(m_index + 1));
    }

    void enterEvent(QEnterEvent*) override { m_hovered = true; update(); }
    void leaveEvent(QEvent*) override { m_hovered = false; update(); }
    void mouseReleaseEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton && rect().contains(e->pos()))
            emit clicked(m_index);
        else if (e->button() == Qt::RightButton)
            emit contextMenuRequested(m_index, e->globalPosition().toPoint());
    }
    void tabletEvent(QTabletEvent* e) override { e->ignore(); }

private:
    int m_index = 0;
    QImage m_thumb;
    bool m_active = false;
    bool m_hovered = false;
    EditorPalette m_pal;
};

} // namespace

// ---------------- EditorElementsPanel ----------------

EditorElementsPanel::EditorElementsPanel(CanvasWidget* canvas, QWidget* parent)
    : EditorGlassCard(parent),
      m_canvas(canvas) {
    setRole(Role::SidePanel);
    setCornerRadius(16.0);
    setFixedWidth(286);
    hide();

    m_closeBtn = new EditorIconButton(QStringLiteral("x"), QStringLiteral("ui"), this);
    m_closeBtn->setFixedSize(26, 26);
    m_closeBtn->setIconSizePx(14.0);
    connect(m_closeBtn, &EditorIconButton::clicked, this, &EditorElementsPanel::closeRequested);

    m_styleSeg = new EditorSegControl(this);
    m_styleSeg->setOptions({QStringLiteral("Limpio"), QStringLiteral("Ficha"), QStringLiteral("Solo dibujo")});
    const int savedStyle = std::clamp(readAppSetting(QStringLiteral("panels/defaultStickerStyle"), 0).toInt(), 0, 2);
    m_style = StickerStyle(savedStyle);
    m_styleSeg->setCurrentIndex(savedStyle);
    connect(m_styleSeg, &EditorSegControl::currentIndexChanged, this, [this](int idx) {
        m_style = StickerStyle(std::clamp(idx, 0, 2));
        writeAppSetting(QStringLiteral("panels/defaultStickerStyle"), idx);
        rebuildGrid();
    });

    m_imgBtn = new EditorChipButton(QStringLiteral("Imagen"), this);
    m_imgBtn->setIcon(QStringLiteral("img"), QStringLiteral("ui"));
    connect(m_imgBtn, &EditorChipButton::clicked, this, &EditorElementsPanel::insertImageRequested);

    m_pdfBtn = new EditorChipButton(QStringLiteral("PDF"), this);
    m_pdfBtn->setIcon(QStringLiteral("pdf"), QStringLiteral("ui"));
    connect(m_pdfBtn, &EditorChipButton::clicked, this, &EditorElementsPanel::importPdfRequested);

    m_tableBtn = new EditorChipButton(QStringLiteral("Tabla"), this);
    m_tableBtn->setIcon(QStringLiteral("table"), QStringLiteral("ui"));
    connect(m_tableBtn, &EditorChipButton::clicked, this, [this] {
        m_tablePickerOpen = !m_tablePickerOpen;
        layoutChildren();
    });

    // Selector compacto N x M para tabla (cero QInputDialog)
    m_tableRow = new QWidget(this);
    m_rowsMinus = new EditorIconButton(QStringLiteral("minus"), QStringLiteral("ui"), m_tableRow);
    m_rowsMinus->setFixedSize(22, 22);
    m_rowsMinus->setIconSizePx(12.0);
    m_rowsPlus = new EditorIconButton(QStringLiteral("plus"), QStringLiteral("ui"), m_tableRow);
    m_rowsPlus->setFixedSize(22, 22);
    m_rowsPlus->setIconSizePx(12.0);
    m_colsMinus = new EditorIconButton(QStringLiteral("minus"), QStringLiteral("ui"), m_tableRow);
    m_colsMinus->setFixedSize(22, 22);
    m_colsMinus->setIconSizePx(12.0);
    m_colsPlus = new EditorIconButton(QStringLiteral("plus"), QStringLiteral("ui"), m_tableRow);
    m_colsPlus->setFixedSize(22, 22);
    m_colsPlus->setIconSizePx(12.0);
    m_insertTableBtn = new EditorChipButton(QStringLiteral("Crear 3×4"), m_tableRow);
    m_insertTableBtn->setChecked(true);

    auto updateTableBtnText = [this] {
        m_insertTableBtn->setText(QStringLiteral("Crear %1×%2").arg(m_tableRows).arg(m_tableCols));
    };
    connect(m_rowsMinus, &EditorIconButton::clicked, this, [this, updateTableBtnText] {
        m_tableRows = std::max(1, m_tableRows - 1);
        updateTableBtnText();
    });
    connect(m_rowsPlus, &EditorIconButton::clicked, this, [this, updateTableBtnText] {
        m_tableRows = std::min(12, m_tableRows + 1);
        updateTableBtnText();
    });
    connect(m_colsMinus, &EditorIconButton::clicked, this, [this, updateTableBtnText] {
        m_tableCols = std::max(1, m_tableCols - 1);
        updateTableBtnText();
    });
    connect(m_colsPlus, &EditorIconButton::clicked, this, [this, updateTableBtnText] {
        m_tableCols = std::min(12, m_tableCols + 1);
        updateTableBtnText();
    });
    connect(m_insertTableBtn, &EditorChipButton::clicked, this, [this] {
        if (m_canvas)
            m_canvas->createTableShape(m_tableRows, m_tableCols);
        m_tablePickerOpen = false;
        layoutChildren();
    });
    m_tableRow->hide();

    m_searchEdit = new EditorLineEdit(this);
    m_searchEdit->setPlaceholderText(QStringLiteral("Buscar fórmula, bloque…"));
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this] { rebuildGrid(); });

    m_catRow = new QWidget(this);
    rebuildCategories();

    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; border: none; }"));
    m_gridHost = new QWidget();
    m_gridHost->setAttribute(Qt::WA_StyledBackground, false);
    m_gridHost->setStyleSheet(QStringLiteral("background: transparent;"));
    m_scroll->setWidget(m_gridHost);

    m_exploreBtn = new EditorChipButton(QStringLiteral("+ Explorar biblioteca y paquetes"), this);
    connect(m_exploreBtn, &EditorChipButton::clicked, this, [this] {
        emit libraryRequested(QStringLiteral("elementos"));
    });

    layoutChildren();
    rebuildGrid();
}

void EditorElementsPanel::setDropdownOpener(DropdownRequestFn fn) {
    m_dropdownOpener = std::move(fn);
}

void EditorElementsPanel::setStickerStyle(StickerStyle style) {
    m_style = style;
    m_styleSeg->setCurrentIndex(int(style));
    rebuildGrid();
}

void EditorElementsPanel::applyEditorTheme(bool dark) {
    EditorGlassCard::applyEditorTheme(dark);
    m_closeBtn->applyEditorTheme(dark);
    m_styleSeg->applyEditorTheme(dark);
    m_imgBtn->applyEditorTheme(dark);
    m_pdfBtn->applyEditorTheme(dark);
    m_tableBtn->applyEditorTheme(dark);
    m_rowsMinus->applyEditorTheme(dark);
    m_rowsPlus->applyEditorTheme(dark);
    m_colsMinus->applyEditorTheme(dark);
    m_colsPlus->applyEditorTheme(dark);
    m_insertTableBtn->applyEditorTheme(dark);
    m_searchEdit->applyEditorTheme(dark);
    for (auto* c : m_catChips)
        c->applyEditorTheme(dark);
    m_exploreBtn->applyEditorTheme(dark);
    rebuildGrid();
}

void EditorElementsPanel::syncFromCanvas() {
    rebuildGrid();
}

void EditorElementsPanel::rebuildCategories() {
    for (auto* c : m_catChips) {
        c->hide();
        c->deleteLater();
    }
    m_catChips.clear();

    const QStringList cats = {
        QStringLiteral("Todo"), QStringLiteral("Fórmulas"), QStringLiteral("Geometría"),
        QStringLiteral("Física"), QStringLiteral("Química"), QStringLiteral("Recientes"),
        QStringLiteral("Mis elementos")
    };
    int x = 0;
    int y = 0;
    for (const QString& cat : cats) {
        auto* chip = new EditorChipButton(cat, m_catRow);
        chip->setChecked(cat == m_activeCategory);
        chip->applyEditorTheme(m_dark);
        const QSize sh = chip->sizeHint();
        if (x > 0 && x + sh.width() > 262) {
            x = 0;
            y += 30;
        }
        chip->setGeometry(x, y, sh.width(), 26);
        chip->show();
        connect(chip, &EditorChipButton::clicked, this, [this, cat] {
            m_activeCategory = cat;
            for (auto* b : m_catChips)
                b->setChecked(b->text() == cat);
            rebuildGrid();
        });
        m_catChips.append(chip);
        x += sh.width() + 5;
    }
    m_catRow->setFixedSize(262, y + 28);
}

void EditorElementsPanel::resizeEvent(QResizeEvent* event) {
    EditorGlassCard::resizeEvent(event);
    layoutChildren();
}

void EditorElementsPanel::layoutChildren() {
    const int innerW = width() - 24;
    m_closeBtn->move(width() - 38, 10);
    m_styleSeg->setGeometry(12, 42, innerW, 28);

    const int qy = 78;
    const int btnW = (innerW - 12) / 3;
    m_imgBtn->setGeometry(12, qy, btnW, 27);
    m_pdfBtn->setGeometry(12 + btnW + 6, qy, btnW, 27);
    m_tableBtn->setGeometry(12 + (btnW + 6) * 2, qy, btnW, 27);

    int y = qy + 34;
    if (m_tablePickerOpen) {
        m_tableRow->setGeometry(12, y, innerW, 30);
        m_rowsMinus->move(0, 4);
        m_rowsPlus->move(26, 4);
        m_colsMinus->move(60, 4);
        m_colsPlus->move(86, 4);
        m_insertTableBtn->setGeometry(118, 2, innerW - 118, 26);
        m_tableRow->show();
        y += 36;
    } else {
        m_tableRow->hide();
    }

    m_searchEdit->setGeometry(12, y, innerW, 30);
    y += 38;
    m_catRow->move(12, y);
    y += m_catRow->height() + 6;

    const int footH = 38;
    m_exploreBtn->setGeometry(12, height() - footH, innerW, 28);
    m_scroll->setGeometry(12, y, innerW, std::max(60, height() - y - footH - 8));
}

bool EditorElementsPanel::matchesCategory(const StickerTemplate& tpl) const {
    if (m_activeCategory == QStringLiteral("Todo"))
        return true;
    if (m_activeCategory == QStringLiteral("Mis elementos"))
        return tpl.isCustom;
    if (m_activeCategory == QStringLiteral("Geometría"))
        return !tpl.isCustom && (tpl.category.contains(QStringLiteral("Triáng"), Qt::CaseInsensitive) ||
                                 tpl.category.contains(QStringLiteral("Circ"), Qt::CaseInsensitive) ||
                                 tpl.category.contains(QStringLiteral("Congr"), Qt::CaseInsensitive) ||
                                 tpl.category.contains(QStringLiteral("Áng"), Qt::CaseInsensitive) ||
                                 tpl.category.contains(QStringLiteral("Áreas"), Qt::CaseInsensitive));
    if (m_activeCategory == QStringLiteral("Fórmulas"))
        return !tpl.formula.isEmpty() || tpl.category.contains(QStringLiteral("Métric"), Qt::CaseInsensitive) ||
               tpl.category.contains(QStringLiteral("Áreas"), Qt::CaseInsensitive) ||
               tpl.category.contains(QStringLiteral("Notabl"), Qt::CaseInsensitive);
    if (m_activeCategory == QStringLiteral("Física"))
        return tpl.category.contains(QStringLiteral("Físic"), Qt::CaseInsensitive) ||
               tpl.keywords.contains(QStringLiteral("fisica"), Qt::CaseInsensitive);
    if (m_activeCategory == QStringLiteral("Química"))
        return tpl.category.contains(QStringLiteral("Quím"), Qt::CaseInsensitive) ||
               tpl.keywords.contains(QStringLiteral("quimica"), Qt::CaseInsensitive);
    return tpl.category.compare(m_activeCategory, Qt::CaseInsensitive) == 0;
}

void EditorElementsPanel::rebuildGrid() {
    auto* oldLayout = m_gridHost->layout();
    if (oldLayout) {
        QLayoutItem* it = nullptr;
        while ((it = oldLayout->takeAt(0)) != nullptr) {
            if (it->widget())
                it->widget()->deleteLater();
            delete it;
        }
        delete oldLayout;
    }

    auto* grid = new QGridLayout(m_gridHost);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(8);

    const QString q = m_searchEdit ? m_searchEdit->text().trimmed().toLower() : QString();
    const ThemeConfig theme = m_canvas
        ? effectiveThemeConfig(m_canvas->themeId(), m_canvas->customBackground())
        : getThemeConfig(ThemeId::PaperLight);

    int cell = 0;
    if (m_activeCategory == QStringLiteral("Recientes")) {
        const QStringList paths = readAppSetting(QStringLiteral("editor/recentImages")).toStringList();
        for (const QString& path : paths) {
            const QFileInfo fi(path);
            if (!fi.exists())
                continue;
            if (!q.isEmpty() && !fi.fileName().toLower().contains(q))
                continue;
            QPixmap px(path);
            auto* tile = new ElementTileWidget(fi.fileName(), px, false, m_gridHost);
            tile->applyEditorTheme(m_dark);
            connect(tile, &ElementTileWidget::clicked, this, [this, path] {
                if (m_canvas)
                    m_canvas->importImageFile(path);
            });
            grid->addWidget(tile, cell / 2, cell % 2);
            ++cell;
        }
        grid->setRowStretch(cell / 2 + 1, 1);
        return;
    }

    const QVector<StickerTemplate> all = allStickerTemplates();
    for (const auto& tpl : all) {
        if (!matchesCategory(tpl))
            continue;
        if (!q.isEmpty()) {
            const bool hit = tpl.title.toLower().contains(q) ||
                             tpl.formula.toLower().contains(q) ||
                             tpl.keywords.join(QLatin1Char(' ')).toLower().contains(q);
            if (!hit)
                continue;
        }
        const QPixmap px = renderStickerThumbnail(tpl, QSize(180, 96), theme, m_style);
        auto* tile = new ElementTileWidget(tpl.title, px, tpl.isCustom, m_gridHost);
        tile->applyEditorTheme(m_dark);
        const QString id = tpl.id;
        connect(tile, &ElementTileWidget::clicked, this, [this, id] {
            if (m_canvas)
                m_canvas->insertStickerTemplate(id, m_style);
        });
        connect(tile, &ElementTileWidget::contextMenuRequested, this, [this, id, tile](const QPoint&) {
            if (!m_dropdownOpener)
                return;
            QVector<EditorDropdownItem> items = {
                {1, QStringLiteral("Insertar en el lienzo"), {}, QStringLiteral("plus"), QStringLiteral("ui")},
                {2, QStringLiteral("Eliminar de Mis elementos"), {}, QStringLiteral("trash"), QStringLiteral("ui"), true, true}
            };
            m_dropdownOpener(tile, items, [this, id](int action) {
                if (action == 1 && m_canvas)
                    m_canvas->insertStickerTemplate(id, m_style);
                else if (action == 2) {
                    deleteCustomSticker(id);
                    rebuildGrid();
                }
            });
        });
        grid->addWidget(tile, cell / 2, cell % 2);
        ++cell;
    }
    grid->setRowStretch(cell / 2 + 1, 1);
}

void EditorElementsPanel::paintEvent(QPaintEvent* event) {
    EditorGlassCard::paintEvent(event);
    QPainter p(this);
    p.setFont(editorFont(13.0, QFont::Bold));
    p.setPen(m_pal.text);
    p.drawText(QRectF(14, 12, width() - 56, 22), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("Elementos e inserción"));
}

// ---------------- EditorPagesPanel ----------------

EditorPagesPanel::EditorPagesPanel(CanvasWidget* canvas, QWidget* parent)
    : EditorGlassCard(parent),
      m_canvas(canvas) {
    setRole(Role::SidePanel);
    setCornerRadius(16.0);
    setFixedWidth(164);
    hide();

    m_closeBtn = new EditorIconButton(QStringLiteral("x"), QStringLiteral("ui"), this);
    m_closeBtn->setFixedSize(26, 26);
    m_closeBtn->setIconSizePx(14.0);
    connect(m_closeBtn, &EditorIconButton::clicked, this, &EditorPagesPanel::closeRequested);

    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; border: none; }"));
    m_listHost = new QWidget();
    m_listHost->setStyleSheet(QStringLiteral("background: transparent;"));
    m_scroll->setWidget(m_listHost);

    m_addPageBtn = new EditorChipButton(QStringLiteral("+ Página"), this);
    connect(m_addPageBtn, &EditorChipButton::clicked, this, [this] {
        if (m_canvas)
            m_canvas->addPage();
    });
}

void EditorPagesPanel::setDropdownOpener(DropdownRequestFn fn) {
    m_dropdownOpener = std::move(fn);
}

void EditorPagesPanel::applyEditorTheme(bool dark) {
    EditorGlassCard::applyEditorTheme(dark);
    m_closeBtn->applyEditorTheme(dark);
    m_addPageBtn->applyEditorTheme(dark);
    rebuildThumbnails();
}

void EditorPagesPanel::resizeEvent(QResizeEvent* event) {
    EditorGlassCard::resizeEvent(event);
    m_closeBtn->move(width() - 36, 10);
    m_addPageBtn->setGeometry(14, height() - 40, width() - 28, 28);
    m_scroll->setGeometry(10, 42, width() - 20, std::max(60, height() - 90));
}

void EditorPagesPanel::syncFromCanvas() {
    rebuildThumbnails();
}

void EditorPagesPanel::rebuildThumbnails() {
    if (!m_canvas)
        return;
    auto* old = m_listHost->layout();
    if (old) {
        QLayoutItem* it = nullptr;
        while ((it = old->takeAt(0)) != nullptr) {
            if (it->widget())
                it->widget()->deleteLater();
            delete it;
        }
        delete old;
    }

    auto* vbox = new QVBoxLayout(m_listHost);
    vbox->setContentsMargins(4, 4, 4, 4);
    vbox->setSpacing(8);

    const int count = m_canvas->pageCount();
    const int cur = m_canvas->currentPageIndex();
    for (int i = 0; i < count; ++i) {
        const QImage thumb = m_canvas->pageThumbnail(i, QSize(184, 236));
        auto* card = new PageThumbWidget(i, thumb, i == cur, m_listHost);
        card->applyEditorTheme(m_dark);
        connect(card, &PageThumbWidget::clicked, this, [this](int idx) {
            if (m_canvas)
                m_canvas->goToPage(idx);
        });
        connect(card, &PageThumbWidget::contextMenuRequested, this, [this, card, count](int idx, const QPoint&) {
            if (!m_dropdownOpener)
                return;
            QVector<EditorDropdownItem> items = {
                {1, QStringLiteral("Nueva página debajo"), {}, QStringLiteral("plus"), QStringLiteral("ui")},
                {2, QStringLiteral("Duplicar página"), {}, QStringLiteral("copy"), QStringLiteral("ui")},
                {3, QStringLiteral("Subir"), {}, QStringLiteral("chevL"), QStringLiteral("ui"), true, false, false, idx > 0},
                {4, QStringLiteral("Bajar"), {}, QStringLiteral("chevR"), QStringLiteral("ui"), false, false, false, idx + 1 < count},
                {5, QStringLiteral("Eliminar página"), {}, QStringLiteral("trash"), QStringLiteral("ui"), true, true, false, count > 1}
            };
            m_dropdownOpener(card, items, [this, idx](int act) {
                if (!m_canvas)
                    return;
                if (act == 1) m_canvas->insertPage(idx + 1);
                else if (act == 2) m_canvas->duplicatePages({idx});
                else if (act == 3) m_canvas->movePages({idx}, idx - 1);
                else if (act == 4) m_canvas->movePages({idx}, idx + 2);
                else if (act == 5) {
                    m_canvas->deletePages({idx});
                    emit pageDeletedToastRequested();
                }
            });
        });
        vbox->addWidget(card, 0, Qt::AlignHCenter);
    }
    vbox->addStretch(1);
}

void EditorPagesPanel::paintEvent(QPaintEvent* event) {
    EditorGlassCard::paintEvent(event);
    QPainter p(this);
    p.setFont(editorFont(13.0, QFont::Bold));
    p.setPen(m_pal.text);
    p.drawText(QRectF(14, 12, width() - 50, 22), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Páginas"));
}

} // namespace finily

#include "EditorSidePanels.moc"
