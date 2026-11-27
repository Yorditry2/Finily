#include "ui/home/NewBoardSheet.h"

#include "engine/LibraryStore.h"
#include "ui/DesignTokens.h"
#include "ui/home/SheetWidgets.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace finily {
namespace {
// Los cuatro fondos que admite el documento (muestras de 26 px del mockup).
const ThemeId kThemes[] = {ThemeId::DarkBoard, ThemeId::GreenBoard, ThemeId::PaperLight, ThemeId::PaperCream};
const char* kThemeNames[] = {"Oscura", "Academia", "Blanco", "Crema"};
} // namespace

NewBoardSheet::NewBoardSheet(QWidget* parent)
    : GlassSheet(parent) {
    setSideCard(true);
    setBodyMargins(16, 14, 16, 16);
    bodyLayout()->setSpacing(12);
    bodyLayout()->setContentsMargins(0, 0, 0, 0);

    // El héroe va pegado a los bordes: se inserta antes del cuerpo con márgenes.
    m_hero = new SheetHero(QStringLiteral("Pizarra"), QStringLiteral("board"), this);
    connect(m_hero, &SheetHero::closeClicked, this, [this] { closeSheet(false); });
    connect(m_hero->titleEdit(), &QLineEdit::returnPressed, this, [this] {
        if (validateBeforeAccept())
            onConfirmed();
    });
    bodyLayout()->addWidget(m_hero);

    auto* body = new QWidget(this);
    body->setAttribute(Qt::WA_StyledBackground, false);
    auto* col = new QVBoxLayout(body);
    col->setContentsMargins(16, 2, 16, 16);
    col->setSpacing(12);
    bodyLayout()->addWidget(body);

    // Fondo + hex
    auto* rowBg = new QHBoxLayout();
    rowBg->setContentsMargins(0, 0, 0, 0);
    rowBg->addWidget(makeKicker(QStringLiteral("Fondo"), body, false));
    rowBg->addStretch(1);
    m_hex = new QLabel(body);
    QFont mono(QStringLiteral("monospace"));
    mono.setPixelSize(11);
    m_hex->setFont(mono);
    rowBg->addWidget(m_hex);
    col->addLayout(rowBg);

    m_swatches = new SwatchRow(body);
    QVector<QColor> colors;
    QStringList names;
    for (int i = 0; i < 4; ++i) {
        colors.append(getThemeConfig(kThemes[i]).background);
        names.append(QString::fromUtf8(kThemeNames[i]));
    }
    m_swatches->setColors(colors, names);
    connect(m_swatches, &SwatchRow::picked, this, [this](int i) {
        m_spec.theme = kThemes[i];
        refreshState();
    });
    col->addWidget(m_swatches);

    col->addWidget(makeKicker(QStringLiteral("Plantilla"), body, false));
    m_templates = new TemplateRow(body);
    connect(m_templates, &TemplateRow::picked, this, [this](BackgroundTemplate t) {
        m_spec.tpl = t;
        refreshState();
    });
    col->addWidget(m_templates);

    m_ruler = new MmRuler(body);
    connect(m_ruler, &MmRuler::valueChanged, this, [this](double mm) {
        m_spec.templateScale = mm / 5.0; // 5 mm = trama normal
        refreshState();
    });
    col->addWidget(m_ruler);

    auto* chips = new QHBoxLayout();
    chips->setContentsMargins(0, 0, 0, 0);
    m_folderChip = new InfoChip(QStringLiteral("folder"), QStringLiteral("Sin carpeta"), false, false, body);
    m_folderChip->setCursor(Qt::ArrowCursor);
    chips->addWidget(m_folderChip);
    chips->addStretch(1);
    col->addLayout(chips);

    auto* cta = makeCtaButton(QStringLiteral("Crear pizarra"), body, false);
    m_cta = cta;
    connect(cta, &QPushButton::clicked, this, [this] {
        if (validateBeforeAccept())
            onConfirmed();
    });
    col->addWidget(cta);
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

    for (auto* w : {static_cast<QWidget*>(m_swatches), static_cast<QWidget*>(m_templates),
                    static_cast<QWidget*>(m_ruler), static_cast<QWidget*>(m_folderChip)}) {
        Q_UNUSED(w);
    }
    m_swatches->setDark(dark);
    m_templates->setDark(dark);
    m_ruler->setDark(dark);
    m_folderChip->setDark(dark);
    m_hero->setDark(dark);
    m_hex->setStyleSheet(QStringLiteral("QLabel { color: %1; background: transparent; }")
                             .arg(tokens::paletteFor(dark).muted.name()));
    m_ruler->setValue(5.0);
    m_hero->titleEdit()->setText(QStringLiteral("Pizarra sin título"));
    m_folderChip->setText(folderRelativePath.isEmpty() ? QStringLiteral("Sin carpeta")
                                                       : folderRelativePath.section('/', -1));
    openSheet(pal, dark);
    refreshState();
    m_hero->titleEdit()->setFocus();
    m_hero->titleEdit()->selectAll();
}

void NewBoardSheet::refreshState() {
    int idx = 0;
    for (int i = 0; i < 4; ++i)
        if (kThemes[i] == m_spec.theme)
            idx = i;
    m_swatches->setCurrent(idx);
    m_templates->setCurrent(m_spec.tpl);
    m_ruler->setEnabled(m_spec.tpl != BackgroundTemplate::Blank);
    const ThemeConfig tc = getThemeConfig(m_spec.theme);
    m_hero->setLook(tc, m_spec.tpl, m_spec.templateScale);
    m_hex->setText(tc.background.name(QColor::HexRgb).toUpper());
}

void NewBoardSheet::onConfirmed() {
    const QString clean = LibraryStore::sanitizeBaseName(m_hero->titleEdit()->text());
    m_spec.title = clean.isEmpty() ? QStringLiteral("Pizarra sin título") : clean;
    closeSheet(true);
    emit boardSubmitted(m_spec);
}

} // namespace finily
