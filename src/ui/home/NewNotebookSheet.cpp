#include "ui/home/NewNotebookSheet.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace finily {

class NotebookHeroPreview : public QWidget {
public:
    explicit NotebookHeroPreview(QWidget* parent = nullptr)
        : QWidget(parent) {
        setFixedSize(118, 146);
    }

    void setPreview(const NewDocumentSpec& spec, bool showPaperWindow, bool dark) {
        m_spec = spec;
        m_showPaper = showPaperWindow;
        m_dark = dark;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRectF cover(10.0, 6.0, width() - 22.0, height() - 14.0);
        paintSoftShadow(painter, cover, 11.0, 2.0, m_dark);

        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor("#e7e5df"));
        painter.drawRoundedRect(cover.adjusted(4.0, 3.0, 3.0, 2.0), 9.0, 9.0);
        painter.setBrush(QColor("#faf9f5"));
        painter.drawRoundedRect(cover.adjusted(3.0, 1.5, 1.5, 0.8), 9.5, 9.5);

        const int idx = std::clamp(m_spec.cover.colorIndex, 0, int(coverColors().size()) - 1);
        QColor base = coverColors().at(idx);
        if (m_dark && idx != coverColors().size() - 1)
            base = base.darker(115);

        QLinearGradient fill(cover.topLeft(), cover.bottomRight());
        fill.setColorAt(0.0, base.lighter(108));
        fill.setColorAt(1.0, base.darker(106));
        painter.setBrush(fill);
        painter.drawRoundedRect(cover, 10.0, 10.0);

        painter.fillRect(QRectF(cover.left(), cover.top() + 3.0, 9.0, cover.height() - 6.0),
                         QColor(0, 0, 0, 28));
        painter.fillRect(QRectF(cover.left() + 9.0, cover.top() + 2.0, 1.0, cover.height() - 4.0),
                         QColor(255, 255, 255, 65));

        if (!m_showPaper) {
            if (m_spec.cover.iconIndex > 0) {
                const QRectF iconBox(cover.center().x() - 14.0, cover.center().y() - 16.0, 32.0, 32.0);
                symbolGlyph(m_spec.cover.iconIndex, QColor(255, 255, 255, 236))
                    .paint(&painter, iconBox.toRect());
            } else {
                const QRectF badge(cover.left() + 22.0, cover.top() + 22.0, cover.width() - 34.0, 24.0);
                painter.setBrush(QColor(255, 255, 255, 210));
                painter.drawRoundedRect(badge, 5.0, 5.0);
            }
            return;
        }

        const QRectF slot = cover.adjusted(15.0, 11.0, -7.0, -11.0);
        double ratio = 794.0 / 1123.0;
        if (m_spec.format == PageFormat::A4Landscape)
            ratio = 1123.0 / 794.0;
        else if (m_spec.format == PageFormat::Slide16x9)
            ratio = 16.0 / 9.0;
        double sw = slot.width();
        double sh = sw / ratio;
        if (sh > slot.height()) {
            sh = slot.height();
            sw = sh * ratio;
        }
        const QRectF sheet(slot.center().x() - sw / 2.0, slot.center().y() - sh / 2.0, sw, sh);
        painter.setBrush(QColor(0, 0, 0, 28));
        painter.drawRoundedRect(sheet.translated(0.0, 1.5), 4.0, 4.0);
        const ThemeConfig tc = getThemeConfig(m_spec.theme);
        painter.setBrush(tc.background);
        painter.drawRoundedRect(sheet, 4.0, 4.0);
        paintMiniTemplate(painter, sheet, tc, m_spec.tpl, m_spec.templateScale);
        painter.setPen(QPen(tc.isDark ? QColor(255, 255, 255, 36) : QColor(0, 0, 0, 26), 0.9));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(sheet, 4.0, 4.0);
    }

private:
    NewDocumentSpec m_spec;
    bool m_showPaper = false;
    bool m_dark = false;
};

NewNotebookSheet::NewNotebookSheet(QWidget* parent)
    : GlassSheet(parent) {
    setCardWidth(452);

    m_preview = new NotebookHeroPreview(this);
    bodyLayout()->addWidget(m_preview, 0, Qt::AlignHCenter);

    m_nameEdit = makeNameField(QStringLiteral("Ej: Álgebra — Semana 4"),
                               QStringLiteral("Cuaderno sin título"));
    bodyLayout()->addWidget(m_nameEdit);

    m_tabBar = new QWidget(this);
    auto* tabRow = new QHBoxLayout(m_tabBar);
    tabRow->setContentsMargins(0, 2, 0, 2);
    tabRow->setSpacing(6);
    auto* tabGroup = new QButtonGroup(m_tabBar);
    tabGroup->setExclusive(true);

    m_coverTabBtn = new QToolButton(m_tabBar);
    m_coverTabBtn->setObjectName(QStringLiteral("finilySheetPill"));
    m_coverTabBtn->setText(QStringLiteral("Portada"));
    m_coverTabBtn->setFixedHeight(32);
    m_coverTabBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_coverTabBtn->setCheckable(true);
    m_coverTabBtn->setChecked(true);
    m_coverTabBtn->setCursor(Qt::PointingHandCursor);
    m_coverTabBtn->setFocusPolicy(Qt::NoFocus);
    tabGroup->addButton(m_coverTabBtn, 0);
    tabRow->addWidget(m_coverTabBtn);
    connect(m_coverTabBtn, &QToolButton::clicked, this, [this] { setActiveTab(0); });

    m_paperTabBtn = new QToolButton(m_tabBar);
    m_paperTabBtn->setObjectName(QStringLiteral("finilySheetPill"));
    m_paperTabBtn->setText(QStringLiteral("Papel"));
    m_paperTabBtn->setFixedHeight(32);
    m_paperTabBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_paperTabBtn->setCheckable(true);
    m_paperTabBtn->setCursor(Qt::PointingHandCursor);
    m_paperTabBtn->setFocusPolicy(Qt::NoFocus);
    tabGroup->addButton(m_paperTabBtn, 1);
    tabRow->addWidget(m_paperTabBtn);
    connect(m_paperTabBtn, &QToolButton::clicked, this, [this] { setActiveTab(1); });
    bodyLayout()->addWidget(m_tabBar);

    m_stack = new QStackedWidget(this);

    auto* coverPage = new QWidget(m_stack);
    auto* coverCol = new QVBoxLayout(coverPage);
    coverCol->setContentsMargins(0, 4, 0, 0);
    coverCol->setSpacing(9);
    coverCol->addWidget(makeSectionLabel(QStringLiteral("COLOR DE TAPA")));
    coverCol->addWidget(makeColorSwatchRow(
        0,
        [this](int idx) {
            m_spec.cover.colorIndex = idx;
            refreshState();
        },
        &m_colorGroup));
    coverCol->addWidget(makeSectionLabel(QStringLiteral("ÍCONO DE PORTADA")));
    coverCol->addWidget(makeSymbolIconRow(
        0,
        [this](int idx) {
            m_spec.cover.iconIndex = idx;
            refreshState();
        },
        &m_iconGroup));
    coverCol->addStretch(1);
    m_stack->addWidget(coverPage);

    auto* paperPage = new QWidget(m_stack);
    auto* paperCol = new QVBoxLayout(paperPage);
    paperCol->setContentsMargins(0, 4, 0, 0);
    paperCol->setSpacing(8);
    paperCol->addWidget(makeSectionLabel(QStringLiteral("FORMATO Y ORIENTACIÓN")));
    const QVector<QPair<QString, int>> formats = {
        {QStringLiteral("A4 Vertical"), int(PageFormat::A4)},
        {QStringLiteral("A4 Horizontal"), int(PageFormat::A4Landscape)},
        {QStringLiteral("Diapo 16:9"), int(PageFormat::Slide16x9)},
    };
    paperCol->addWidget(makePillRow(
        formats, int(PageFormat::A4),
        [this](int v) {
            m_spec.format = static_cast<PageFormat>(v);
            refreshState();
        },
        &m_formatGroup));

    paperCol->addWidget(makeSectionLabel(QStringLiteral("FONDO")));
    paperCol->addWidget(makeThemeCardRow(
        false, ThemeId::PaperLight,
        [this](ThemeId th) {
            m_spec.theme = th;
            refreshState();
        },
        &m_themeGroup));

    paperCol->addWidget(makeSectionLabel(QStringLiteral("PLANTILLA")));
    const QVector<QPair<QString, int>> templates = {
        {QStringLiteral("Liso"), int(BackgroundTemplate::Blank)},
        {QStringLiteral("Cuadrícula"), int(BackgroundTemplate::Grid)},
        {QStringLiteral("Puntos"), int(BackgroundTemplate::Dots)},
        {QStringLiteral("Rayado"), int(BackgroundTemplate::Ruled)},
    };
    paperCol->addWidget(makePillRow(
        templates, int(BackgroundTemplate::Grid),
        [this](int v) {
            m_spec.tpl = static_cast<BackgroundTemplate>(v);
            refreshState();
        },
        &m_tplGroup));

    paperCol->addWidget(makeSectionLabel(QStringLiteral("TAMAÑO DE TRAMA")));
    const QVector<QPair<QString, int>> scales = {
        {QStringLiteral("Chica"), 70},
        {QStringLiteral("Normal"), 100},
        {QStringLiteral("Grande"), 145},
    };
    paperCol->addWidget(makePillRow(
        scales, 100,
        [this](int v) {
            m_spec.templateScale = double(v) / 100.0;
            refreshState();
        },
        &m_scaleGroup, &m_scaleBtns));
    m_stack->addWidget(paperPage);

    bodyLayout()->addWidget(m_stack);
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
    m_spec.cover = CoverStyle{0, 0};
    m_spec.folderRelativePath = folderRelativePath;

    configureHeader(QStringLiteral("Nuevo cuaderno"), QStringLiteral("Crear"));
    m_nameEdit->show();
    m_nameEdit->setText(QStringLiteral("Cuaderno sin título"));
    m_nameEdit->selectAll();
    m_tabBar->show();
    m_stack->setMaximumHeight(16777215);
    setActiveTab(0);
    openSheet(pal, dark);
    refreshState();
    m_nameEdit->setFocus();
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
                              std::clamp(meta.coverIconIndex, 0, LibraryStore::kIconCount - 1)};

    configureHeader(QStringLiteral("Editar portada"), QStringLiteral("Listo"));
    m_nameEdit->hide();
    m_tabBar->hide();
    m_stack->setMaximumHeight(152);
    setActiveTab(0);
    openSheet(pal, dark);
    refreshState();
}

void NewNotebookSheet::setActiveTab(int index) {
    m_activeTab = (index == 1 && !m_editCoverOnly) ? 1 : 0;
    if (m_coverTabBtn)
        m_coverTabBtn->setChecked(m_activeTab == 0);
    if (m_paperTabBtn)
        m_paperTabBtn->setChecked(m_activeTab == 1);
    if (m_stack)
        m_stack->setCurrentIndex(m_activeTab);
    refreshState();
}

void NewNotebookSheet::refreshState() {
    auto select = [](QButtonGroup* group, int id) {
        if (group) {
            if (auto* b = group->button(id))
                b->setChecked(true);
        }
    };
    select(m_colorGroup, std::clamp(m_spec.cover.colorIndex, 0, LibraryStore::kCoverColorCount - 1));
    select(m_iconGroup, std::clamp(m_spec.cover.iconIndex, 0, LibraryStore::kIconCount - 1));
    select(m_formatGroup, int(m_spec.format));
    select(m_themeGroup, int(m_spec.theme));
    select(m_tplGroup, int(m_spec.tpl));
    select(m_scaleGroup, qRound(m_spec.templateScale * 100.0));

    const bool hasPattern = (m_spec.tpl != BackgroundTemplate::Blank);
    for (QToolButton* sb : m_scaleBtns)
        sb->setEnabled(hasPattern);
    m_preview->setPreview(m_spec, !m_editCoverOnly && m_activeTab == 1, isDarkMode());
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
