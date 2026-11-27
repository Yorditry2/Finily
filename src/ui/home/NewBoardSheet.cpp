#include "ui/home/NewBoardSheet.h"

#include "engine/LibraryStore.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QToolButton>
#include <QVBoxLayout>

namespace finily {

class BoardHeroPreview : public QWidget {
public:
    explicit BoardHeroPreview(QWidget* parent = nullptr)
        : QWidget(parent) {
        setFixedSize(164, 102);
    }

    void setSpec(const NewDocumentSpec& spec, bool dark) {
        m_spec = spec;
        m_dark = dark;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRectF r = QRectF(rect()).adjusted(6.0, 4.0, -6.0, -8.0);
        paintSoftShadow(painter, r, 12.0, 2.0, m_dark);

        const ThemeConfig tc = getThemeConfig(m_spec.theme);
        QPainterPath clip;
        clip.addRoundedRect(r, 12.0, 12.0);
        painter.save();
        painter.setClipPath(clip);
        painter.fillRect(r, tc.background);
        paintMiniTemplate(painter, r, tc, m_spec.tpl, m_spec.templateScale);
        painter.restore();

        painter.setPen(QPen(tc.isDark ? QColor(255, 255, 255, 38) : QColor(15, 23, 42, 28), 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(r, 12.0, 12.0);

        const QRectF badge(r.left() + 8.0, r.top() + 8.0, 22.0, 17.0);
        painter.setPen(Qt::NoPen);
        painter.setBrush(tc.isDark ? QColor(255, 255, 255, 26) : QColor(15, 23, 42, 16));
        painter.drawRoundedRect(badge, 5.0, 5.0);
        QFont bf = painter.font();
        bf.setPixelSize(11);
        bf.setWeight(QFont::DemiBold);
        painter.setFont(bf);
        painter.setPen(tc.isDark ? QColor(255, 255, 255, 200) : QColor(15, 23, 42, 165));
        painter.drawText(badge, Qt::AlignCenter, QStringLiteral("∞"));
    }

private:
    NewDocumentSpec m_spec;
    bool m_dark = false;
};

NewBoardSheet::NewBoardSheet(QWidget* parent)
    : GlassSheet(parent) {
    setCardWidth(432);
    configureHeader(QStringLiteral("Nueva pizarra"), QStringLiteral("Crear"));

    m_preview = new BoardHeroPreview(this);
    bodyLayout()->addWidget(m_preview, 0, Qt::AlignHCenter);

    m_nameEdit = makeNameField(QStringLiteral("Ej: Geometría — Tema 3"),
                               QStringLiteral("Pizarra sin título"));
    bodyLayout()->addWidget(m_nameEdit);

    bodyLayout()->addWidget(makeSectionLabel(QStringLiteral("FONDO")));
    bodyLayout()->addWidget(makeThemeCardRow(
        true, ThemeId::DarkBoard,
        [this](ThemeId th) {
            m_spec.theme = th;
            m_preview->setSpec(m_spec, isDarkMode());
        },
        &m_themeGroup));

    bodyLayout()->addWidget(makeSectionLabel(QStringLiteral("PLANTILLA")));
    const QVector<QPair<QString, int>> templates = {
        {QStringLiteral("Liso"), int(BackgroundTemplate::Blank)},
        {QStringLiteral("Cuadrícula"), int(BackgroundTemplate::Grid)},
        {QStringLiteral("Puntos"), int(BackgroundTemplate::Dots)},
        {QStringLiteral("Rayado"), int(BackgroundTemplate::Ruled)},
    };
    bodyLayout()->addWidget(makePillRow(
        templates, int(BackgroundTemplate::Grid),
        [this](int v) {
            m_spec.tpl = static_cast<BackgroundTemplate>(v);
            refreshState();
        },
        &m_tplGroup));

    bodyLayout()->addWidget(makeSectionLabel(QStringLiteral("TAMAÑO DE TRAMA")));
    const QVector<QPair<QString, int>> scales = {
        {QStringLiteral("Chica"), 70},
        {QStringLiteral("Normal"), 100},
        {QStringLiteral("Grande"), 145},
    };
    bodyLayout()->addWidget(makePillRow(
        scales, 100,
        [this](int v) {
            m_spec.templateScale = double(v) / 100.0;
            m_preview->setSpec(m_spec, isDarkMode());
        },
        &m_scaleGroup, &m_scaleBtns));
}

void NewBoardSheet::openCreate(const QString& folderRelativePath, const HomePalette& pal, bool dark) {
    m_spec = NewDocumentSpec{};
    m_spec.kind = DocumentKind::Board;
    m_spec.format = PageFormat::Infinite;
    m_spec.theme = ThemeId::DarkBoard;
    m_spec.tpl = BackgroundTemplate::Grid;
    m_spec.templateScale = 1.0;
    m_spec.cover = CoverStyle{-1, 0};
    m_spec.folderRelativePath = folderRelativePath;
    m_nameEdit->setText(QStringLiteral("Pizarra sin título"));
    m_nameEdit->selectAll();
    openSheet(pal, dark);
    refreshState();
    m_nameEdit->setFocus();
}

void NewBoardSheet::refreshState() {
    if (m_themeGroup) {
        if (auto* b = m_themeGroup->button(int(m_spec.theme)))
            b->setChecked(true);
    }
    if (m_tplGroup) {
        if (auto* b = m_tplGroup->button(int(m_spec.tpl)))
            b->setChecked(true);
    }
    if (m_scaleGroup) {
        if (auto* b = m_scaleGroup->button(qRound(m_spec.templateScale * 100.0)))
            b->setChecked(true);
    }
    const bool hasPattern = (m_spec.tpl != BackgroundTemplate::Blank);
    for (QToolButton* sb : m_scaleBtns)
        sb->setEnabled(hasPattern);
    m_preview->setSpec(m_spec, isDarkMode());
}

void NewBoardSheet::onConfirmed() {
    const QString clean = LibraryStore::sanitizeBaseName(m_nameEdit->text());
    m_spec.title = clean.isEmpty() ? QStringLiteral("Pizarra sin título") : clean;
    closeSheet(true);
    emit boardSubmitted(m_spec);
}

} // namespace finily
