#include "ui/home/NewBoardSheet.h"

#include "ui/DesignTokens.h"
#include "ui/home/BoardColorPopover.h"
#include "ui/home/LibraryObjects.h"
#include "ui/home/SheetWidgets.h"

#include <QApplication>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <array>

namespace finily {
namespace {

constexpr int kModalW = 720, kModalH = 440, kLeftW = 296;

const std::array<QColor, 6>& quickBoardColors() {
    static const std::array<QColor, 6> kColors = {
        QColor("#1A1B22"),
        QColor("#16302A"),
        QColor("#1E2A44"),
        QColor("#FFFFFF"),
        QColor("#FAF7EE"),
        QColor("#F7E6EC"),
    };
    return kColors;
}

const QStringList& quickBoardNames() {
    static const QStringList kNames = {
        QStringLiteral("Grafito"), QStringLiteral("Verde"), QStringLiteral("Azul"),
        QStringLiteral("Blanco"), QStringLiteral("Crema"), QStringLiteral("Rosa"),
    };
    return kNames;
}

ThemeId themeForColor(const QColor& c) {
    const QRgb rgb = c.rgb();
    if (rgb == getThemeConfig(ThemeId::DarkBoard).background.rgb())
        return ThemeId::DarkBoard;
    if (rgb == getThemeConfig(ThemeId::GreenBoard).background.rgb())
        return ThemeId::GreenBoard;
    if (rgb == getThemeConfig(ThemeId::PaperLight).background.rgb())
        return ThemeId::PaperLight;
    if (rgb == getThemeConfig(ThemeId::PaperCream).background.rgb())
        return ThemeId::PaperCream;
    const double lum = 0.299 * c.red() + 0.587 * c.green() + 0.114 * c.blue();
    return (lum < 140.0) ? ThemeId::DarkBoard : ThemeId::PaperLight;
}

} // namespace

// Vista previa limpia de pizarra infinita en la columna izquierda del modal.
class BoardPreview : public QWidget {
public:
    explicit BoardPreview(QWidget* parent) : QWidget(parent) { setFixedHeight(214); }
    void setLook(const ThemeConfig& tc, BackgroundTemplate tpl, bool dark) {
        m_theme = tc;
        m_tpl = tpl;
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

        const qreal bw = width() - 40.0;
        const qreal bh = bw * 86.0 / 118.0;
        const QRectF board((width() - bw) / 2.0, (height() - bh) / 2.0, bw, bh);
        paintSoftShadow(p, board, 11, 1.0, m_dark);

        QPainterPath clip;
        clip.addRoundedRect(board, 11, 11);
        p.save();
        p.setClipPath(clip);
        p.fillRect(board, m_theme.background);
        paintMiniTemplate(p, board, m_theme, m_tpl, 1.0);
        p.restore();

        p.setPen(QPen(m_theme.isDark || m_dark ? QColor(255, 255, 255, 34) : QColor(15, 23, 42, 22), 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(board.adjusted(0.5, 0.5, -0.5, -0.5), 11, 11);

        QFont inf = QApplication::font();
        inf.setPixelSize(15);
        inf.setWeight(QFont::DemiBold);
        p.setFont(inf);
        p.setPen(m_theme.isDark ? QColor(244, 244, 245, 165) : QColor(24, 24, 27, 150));
        p.drawText(QRectF(board.right() - 32, board.bottom() - 26, 22, 20),
                   Qt::AlignRight | Qt::AlignBottom, QStringLiteral("∞"));
    }

private:
    ThemeConfig m_theme = getThemeConfig(ThemeId::DarkBoard);
    BackgroundTemplate m_tpl = BackgroundTemplate::Grid;
    bool m_dark = false;
};

NewBoardSheet::NewBoardSheet(QWidget* parent)
    : GlassSheet(parent) {
    setSideCard(false);
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

    // ---- columna izquierda (preview + nombre + chips + CTA)
    m_left = new QWidget(shell);
    m_left->setAttribute(Qt::WA_StyledBackground, false);
    m_left->setAutoFillBackground(false);
    m_left->setFixedWidth(kLeftW);
    auto* lc = new QVBoxLayout(m_left);
    lc->setContentsMargins(18, 18, 18, 18);
    lc->setSpacing(14);

    m_preview = new BoardPreview(m_left);
    lc->addWidget(m_preview);

    m_nameEdit = new QLineEdit(m_left);
    m_nameEdit->setPlaceholderText(QStringLiteral("Nombre de la pizarra"));
    m_nameEdit->setFixedHeight(30);
    connect(m_nameEdit, &QLineEdit::returnPressed, this, [this] { onConfirmed(); });
    lc->addWidget(m_nameEdit);

    lc->addStretch(1);

    auto* chips = new QHBoxLayout();
    chips->setContentsMargins(0, 0, 0, 0);
    chips->setSpacing(6);
    m_folderChip = new InfoChip(QStringLiteral("folder"), QStringLiteral("Sin carpeta"), true, false, m_left);
    m_folderChip->setMaxWidth(148);
    m_tagChip = new InfoChip(QStringLiteral("hash"), QStringLiteral("Etiqueta"), false, true, m_left);
    m_tagChip->setMaxWidth(114);
    chips->addWidget(m_folderChip);
    chips->addWidget(m_tagChip);
    chips->addStretch(1);
    lc->addLayout(chips);

    m_cta = makeCtaButton(QStringLiteral("Crear pizarra"), m_left, false);
    connect(m_cta, &QPushButton::clicked, this, [this] { onConfirmed(); });
    lc->addWidget(m_cta);
    row->addWidget(m_left);

    m_divider = new QWidget(shell);
    m_divider->setFixedWidth(1);
    row->addWidget(m_divider);

    // ---- columna derecha (fondo + plantilla)
    m_right = new QWidget(shell);
    m_right->setAttribute(Qt::WA_StyledBackground, false);
    m_right->setAutoFillBackground(false);
    auto* rc = new QVBoxLayout(m_right);
    rc->setContentsMargins(24, 42, 24, 22);
    rc->setSpacing(24);

    auto* bgSec = new QVBoxLayout();
    bgSec->setSpacing(12);
    auto* rowBg = new QHBoxLayout();
    rowBg->setContentsMargins(0, 0, 0, 0);
    rowBg->addWidget(makeKicker(QStringLiteral("Fondo"), m_right, false));
    rowBg->addStretch(1);
    m_hex = new QLabel(m_right);
    QFont hf = QApplication::font();
    hf.setPixelSize(11);
    hf.setWeight(QFont::Medium);
    m_hex->setFont(hf);
    rowBg->addWidget(m_hex);
    bgSec->addLayout(rowBg);

    m_swatches = new SwatchRow(m_right);
    QVector<QColor> colors;
    for (const QColor& c : quickBoardColors())
        colors.append(c);
    m_swatches->setColors(colors, quickBoardNames());
    m_swatches->setShowMoreButton(true);
    connect(m_swatches, &SwatchRow::picked, this, [this](int i) {
        if (i >= 0 && i < int(quickBoardColors().size())) {
            if (m_colorPop && m_colorPop->isVisible())
                m_colorPop->closePopover();
            applyBackgroundColor(quickBoardColors().at(i));
        }
    });
    connect(m_swatches, &SwatchRow::moreClicked, this, &NewBoardSheet::openColorPopover);
    bgSec->addWidget(m_swatches);
    rc->addLayout(bgSec);

    auto* tplSec = new QVBoxLayout();
    tplSec->setSpacing(12);
    tplSec->addWidget(makeKicker(QStringLiteral("Plantilla"), m_right, false));
    m_templates = new TemplateRow(m_right);
    m_templates->setFixedHeight(78);
    connect(m_templates, &TemplateRow::picked, this, [this](BackgroundTemplate t) {
        m_spec.tpl = t;
        refreshState();
    });
    tplSec->addWidget(m_templates);
    rc->addLayout(tplSec);

    rc->addStretch(1);
    row->addWidget(m_right, 1);

    m_closeBtn = makeModalCloseButton(m_right);
    m_closeBtn->move(kModalW - kLeftW - 1 - 40, 12);
    connect(m_closeBtn, &QPushButton::clicked, this, [this] { closeSheet(false); });
    m_closeBtn->raise();

    // Popovers hermanos en HomeScreen (evita Qt::Popup y no deforma el modal).
    m_colorPop = new BoardColorPopover(parent);
    connect(m_colorPop, &BoardColorPopover::colorChanged, this, [this](const QColor& c) {
        applyBackgroundColor(c);
    });
    connect(m_colorPop, &BoardColorPopover::closed, this, [this] {
        m_swatches->setMoreActive(false);
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
        if (m_colorPop)
            m_colorPop->closePopover();
        if (m_tagPop)
            m_tagPop->closePopover();
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
    connect(m_tagPop, &TagPickPopover::newTagRequested, this, &NewBoardSheet::tagCreateRequested);
    connect(m_tagChip, &InfoChip::clicked, this, [this] {
        if (!m_tagPop)
            return;
        if (m_tagPop->isVisible() && !m_tagPop->isClosing()) {
            m_tagPop->closePopover();
            return;
        }
        if (m_colorPop)
            m_colorPop->closePopover();
        if (m_locPop)
            m_locPop->closePopover();
        m_tagPop->setTags(m_tags);
        m_tagPop->setSelectedTags(m_spec.tagIds);
        const QPoint tl = parentWidget() ? m_tagChip->mapTo(parentWidget(), QPoint(0, 0)) : QPoint();
        m_tagPop->popup(QRect(tl, m_tagChip->size()));
    });
}

void NewBoardSheet::setFolders(const QVector<FolderMeta>& folders) {
    m_folders = folders;
}

void NewBoardSheet::setTags(const QVector<TagInfo>& tags) {
    m_tags = tags;
    if (m_tagPop)
        m_tagPop->setTags(tags);
    refreshState();
}

void NewBoardSheet::closePopovers(bool immediate) {
    if (immediate) {
        if (m_colorPop) m_colorPop->hide();
        if (m_locPop) m_locPop->hide();
        if (m_tagPop) m_tagPop->hide();
        m_swatches->setMoreActive(false);
        return;
    }
    if (m_colorPop) m_colorPop->closePopover();
    if (m_locPop) m_locPop->closePopover();
    if (m_tagPop) m_tagPop->closePopover();
}

void NewBoardSheet::openColorPopover() {
    if (!m_colorPop)
        return;
    if (m_colorPop->isVisible() && !m_colorPop->isClosing()) {
        m_colorPop->closePopover();
        return;
    }
    if (m_locPop)
        m_locPop->closePopover();
    if (m_tagPop)
        m_tagPop->closePopover();
    const QColor currentBg = effectiveThemeConfig(m_spec.theme, m_spec.customBackground).background;
    if (m_swatches)
        m_swatches->setMoreActive(true);
    m_colorPop->openAnchoredTo(cardRect(), currentBg, isDarkMode());
}

void NewBoardSheet::onClosing() {
    closePopovers(false);
}

void NewBoardSheet::hideEvent(QHideEvent* event) {
    closePopovers(true);
    GlassSheet::hideEvent(event);
}

void NewBoardSheet::applyDark(bool dark) {
    const auto pal = tokens::paletteFor(dark);
    m_divider->setStyleSheet(QStringLiteral("background:%1;").arg(pal.border.name(QColor::HexArgb)));
    styleModalTitleEdit(m_nameEdit, dark, 15.0);
    styleModalCloseButton(m_closeBtn, dark);
    m_hex->setStyleSheet(QStringLiteral("QLabel { color: %1; background: transparent; }")
                             .arg(pal.muted.name()));
    m_swatches->setDark(dark);
    m_swatches->setMoreActive(false);
    m_templates->setDark(dark);
    m_folderChip->setDark(dark);
    m_tagChip->setDark(dark);
    if (m_colorPop) m_colorPop->setDark(dark);
    if (m_locPop) m_locPop->setDark(dark);
    if (m_tagPop) m_tagPop->setDark(dark);
}

void NewBoardSheet::openCreate(const QString& folderRelativePath, const HomePalette& pal, bool dark) {
    m_editStyleOnly = false;
    m_editFilePath.clear();

    m_spec = NewDocumentSpec{};
    m_spec.kind = DocumentKind::Board;
    m_spec.format = PageFormat::Infinite;
    m_spec.customBackground = quickBoardColors().at(0);
    m_spec.theme = themeForColor(m_spec.customBackground);
    m_spec.tpl = BackgroundTemplate::Grid;
    m_spec.templateScale = 1.0;
    m_spec.cover = CoverStyle{-1, 0};
    m_spec.folderRelativePath = folderRelativePath;

    applyDark(dark);
    m_nameEdit->setEnabled(true);
    m_nameEdit->setText(QStringLiteral("Pizarra sin título"));
    m_folderChip->show();
    m_tagChip->show();
    m_cta->setText(QStringLiteral("Crear pizarra"));
    openSheet(pal, dark);
    refreshState();
    m_nameEdit->setFocus();
    m_nameEdit->selectAll();
}

void NewBoardSheet::openEditStyle(const NotebookMeta& meta, const HomePalette& pal, bool dark) {
    m_editStyleOnly = true;
    m_editFilePath = meta.path;

    m_spec = NewDocumentSpec{};
    m_spec.kind = DocumentKind::Board;
    m_spec.format = meta.pageFormat;
    m_spec.theme = meta.theme;
    m_spec.customBackground = meta.customBackground;
    m_spec.tpl = meta.tpl;
    m_spec.templateScale = std::clamp(meta.templateScale, 0.5, 2.0);

    applyDark(dark);
    m_nameEdit->setText(meta.title.isEmpty() ? QFileInfo(meta.path).completeBaseName() : meta.title);
    m_nameEdit->setEnabled(false);
    m_folderChip->hide();
    m_tagChip->hide();
    m_cta->setText(QStringLiteral("Guardar estilo"));
    openSheet(pal, dark);
    refreshState();
}

void NewBoardSheet::applyBackgroundColor(const QColor& color) {
    if (!color.isValid())
        return;
    m_spec.customBackground = color;
    m_spec.theme = themeForColor(color);
    refreshState();
}

QString NewBoardSheet::tagChipLabel() const {
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

void NewBoardSheet::refreshState() {
    const ThemeConfig tc = effectiveThemeConfig(m_spec.theme, m_spec.customBackground);
    const QRgb bgRgb = tc.background.rgb();
    int idx = -1;
    const auto& quick = quickBoardColors();
    for (int i = 0; i < int(quick.size()); ++i) {
        if (quick.at(i).rgb() == bgRgb) {
            idx = i;
            break;
        }
    }
    m_swatches->setCurrent(idx);
    m_swatches->setMoreColor(idx < 0 ? tc.background : QColor());
    m_templates->setCurrent(m_spec.tpl);
    m_preview->setLook(tc, m_spec.tpl, isDarkMode());
    m_hex->setText(tc.background.name(QColor::HexRgb).toUpper());
    m_folderChip->setText(m_spec.folderRelativePath.isEmpty()
                              ? QStringLiteral("Sin carpeta")
                              : m_spec.folderRelativePath.section(QLatin1Char('/'), -1));
    m_tagChip->setDashed(m_spec.tagIds.isEmpty());
    m_tagChip->setText(tagChipLabel());
}

void NewBoardSheet::onConfirmed() {
    if (m_editStyleOnly) {
        const QString path = m_editFilePath;
        const ThemeId th = m_spec.theme;
        const QColor bg = m_spec.customBackground;
        const BackgroundTemplate tpl = m_spec.tpl;
        const double sc = m_spec.templateScale;
        closeSheet(true);
        emit styleEditSubmitted(path, th, bg, tpl, sc);
        return;
    }
    const QString clean = LibraryStore::sanitizeBaseName(m_nameEdit->text());
    m_spec.title = clean.isEmpty() ? QStringLiteral("Pizarra sin título") : clean;
    closeSheet(true);
    emit boardSubmitted(m_spec);
}

} // namespace finily
