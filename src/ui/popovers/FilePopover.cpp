#include "ui/popovers/FilePopover.h"

#include "core/Utils.h"
#include "ui/CanvasWidget.h"
#include "ui/ToolbarIcons.h"

#include <QApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPixmap>
#include <QToolButton>
#include <QVBoxLayout>

namespace finily {

FilePopover::FilePopover(CanvasWidget* canvas, QWidget* parent)
    : GlassCapsule(parent), m_canvas(canvas) {
    setObjectName(QStringLiteral("finilyPopover"));
    setFixedWidth(300);
    hide();

    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(14, 12, 14, 12);
    column->setSpacing(2);

    m_newNotebookBtn = new QToolButton(this);
    m_newNotebookBtn->setFixedSize(272, 34);
    m_newNotebookBtn->setText(QStringLiteral("Nuevo cuaderno (Ctrl+N)"));
    m_newNotebookBtn->setToolTip(
        QStringLiteral("Empieza un cuaderno en blanco (descarta el contenido actual)"));
    polishButton(m_newNotebookBtn);
    connect(m_newNotebookBtn, &QToolButton::clicked, this, [this] {
        emit closeRequested();
        emit newNotebookRequested();
    });
    column->addWidget(m_newNotebookBtn);

    auto* newTabBtn = new QToolButton(this);
    newTabBtn->setFixedSize(272, 34);
    newTabBtn->setText(QStringLiteral("Nueva pestaña (Ctrl+T)"));
    newTabBtn->setToolTip(
        QStringLiteral("Abre otro cuaderno en una pestaña nueva manteniendo el actual abierto"));
    polishButton(newTabBtn);
    makeMenuRow(newTabBtn);
    connect(newTabBtn, &QToolButton::clicked, this, [this] {
        emit closeRequested();
        emit newTabRequested();
    });
    column->addWidget(newTabBtn);

    m_openNotebookBtn = new QToolButton(this);
    m_openNotebookBtn->setFixedSize(272, 34);
    m_openNotebookBtn->setText(QStringLiteral("Abrir cuaderno... (Ctrl+Shift+O)"));
    m_openNotebookBtn->setToolTip(QStringLiteral("Abre un cuaderno guardado con extensión .finily"));
    polishButton(m_openNotebookBtn);
    connect(m_openNotebookBtn, &QToolButton::clicked, this, [this] {
        emit closeRequested();
        emit openNotebookRequested();
    });
    column->addWidget(m_openNotebookBtn);

    m_saveNotebookBtn = new QToolButton(this);
    m_saveNotebookBtn->setFixedSize(272, 34);
    m_saveNotebookBtn->setText(QStringLiteral("Guardar (Ctrl+S)"));
    m_saveNotebookBtn->setToolTip(
        QStringLiteral("Guarda el cuaderno actual (.finily); si aún no tiene archivo, pide nombre"));
    polishButton(m_saveNotebookBtn);
    connect(m_saveNotebookBtn, &QToolButton::clicked, this, [this] {
        emit closeRequested();
        emit saveNotebookRequested();
    });
    column->addWidget(m_saveNotebookBtn);

    m_saveNotebookAsBtn = new QToolButton(this);
    m_saveNotebookAsBtn->setFixedSize(272, 34);
    m_saveNotebookAsBtn->setText(QStringLiteral("Guardar como... (Ctrl+Shift+S)"));
    m_saveNotebookAsBtn->setToolTip(
        QStringLiteral("Guarda el cuaderno en un archivo .finily nuevo (añade la extensión si falta)"));
    polishButton(m_saveNotebookAsBtn);
    connect(m_saveNotebookAsBtn, &QToolButton::clicked, this, [this] {
        emit closeRequested();
        emit saveNotebookAsRequested();
    });
    column->addWidget(m_saveNotebookAsBtn);

    column->addWidget(makeDivider(this));

    m_importPdfBtn = new QToolButton(this);
    m_importPdfBtn->setFixedSize(272, 34);
    m_importPdfBtn->setText(QStringLiteral("Importar PDF (Ctrl+O)"));
    m_importPdfBtn->setToolTip(QStringLiteral("Inserta las hojas de un PDF como páginas del cuaderno"));
    polishButton(m_importPdfBtn);
    connect(m_importPdfBtn, &QToolButton::clicked, this, [this] {
        emit closeRequested();
        emit importPdfRequested();
    });
    column->addWidget(m_importPdfBtn);

    m_importImageBtn = new QToolButton(this);
    m_importImageBtn->setFixedSize(272, 34);
    m_importImageBtn->setText(QStringLiteral("Insertar imagen..."));
    m_importImageBtn->setToolTip(QStringLiteral("Inserta una imagen PNG, JPG, WEBP o BMP desde el disco"));
    polishButton(m_importImageBtn);
    connect(m_importImageBtn, &QToolButton::clicked, this, [this] {
        if (!m_canvas)
            return;
        emit closeRequested();
        auto settings = appSettings();
        const QString lastDir = settings->value(QStringLiteral("files/lastImageDir")).toString();
        const QString path = QFileDialog::getOpenFileName(
            window(), QStringLiteral("Insertar imagen"), lastDir,
            QStringLiteral("Imágenes (*.png *.jpg *.jpeg *.webp *.bmp)"));
        if (!path.isEmpty()) {
            settings->setValue(QStringLiteral("files/lastImageDir"), QFileInfo(path).absolutePath());
            m_canvas->importImageFile(path);
            rememberRecentImage(path);
            emit refreshToolbarRequested();
        }
    });
    column->addWidget(m_importImageBtn);

    m_pasteBtn = new QToolButton(this);
    m_pasteBtn->setFixedSize(272, 34);
    m_pasteBtn->setText(QStringLiteral("Pegar imagen (Ctrl+V)"));
    m_pasteBtn->setToolTip(QStringLiteral("Pega la imagen del portapapeles en el lienzo"));
    polishButton(m_pasteBtn);
    connect(m_pasteBtn, &QToolButton::clicked, this, [this] {
        emit closeRequested();
        if (m_canvas)
            m_canvas->pasteFromClipboard();
        emit refreshToolbarRequested();
    });
    column->addWidget(m_pasteBtn);

    m_recentGalleryWrap = new QWidget(this);
    auto* galCol = new QVBoxLayout(m_recentGalleryWrap);
    galCol->setContentsMargins(2, 4, 2, 4);
    galCol->setSpacing(4);
    auto* galLbl = new QLabel(QStringLiteral("Galería rápida"), m_recentGalleryWrap);
    galLbl->setObjectName(QStringLiteral("finilySectionTitle"));
    galCol->addWidget(galLbl);
    m_recentGalleryRow = new QHBoxLayout();
    m_recentGalleryRow->setContentsMargins(0, 0, 0, 0);
    m_recentGalleryRow->setSpacing(6);
    galCol->addLayout(m_recentGalleryRow);
    column->addWidget(m_recentGalleryWrap);
    rebuildRecentGallery();

    m_exportBtn = new QToolButton(this);
    m_exportBtn->setFixedSize(272, 34);
    m_exportBtn->setText(QStringLiteral("Exportar clase a PDF (Ctrl+E)"));
    m_exportBtn->setToolTip(QStringLiteral("Guarda toda la clase (trazos, figuras e imágenes) como PDF"));
    polishButton(m_exportBtn);
    connect(m_exportBtn, &QToolButton::clicked, this, [this] {
        emit closeRequested();
        emit exportPdfRequested();
    });
    column->addWidget(m_exportBtn);

    for (QToolButton* item : {m_newNotebookBtn, m_openNotebookBtn, m_saveNotebookBtn, m_saveNotebookAsBtn,
                              m_importPdfBtn, m_importImageBtn, m_pasteBtn, m_exportBtn}) {
        makeMenuRow(item);
    }

    m_pdfInfoLabel = new QLabel(this);
    m_pdfInfoLabel->setWordWrap(true);
    m_pdfInfoLabel->hide();
    column->addWidget(m_pdfInfoLabel);

    column->addWidget(makeDivider(this));

    auto* penCfgToggle = new QToolButton(this);
    penCfgToggle->setObjectName(QStringLiteral("finilyMoreToggle"));
    penCfgToggle->setText(QStringLiteral("Ajustes de lápiz y tableta  ▾"));
    penCfgToggle->setToolButtonStyle(Qt::ToolButtonTextOnly);
    penCfgToggle->setCursor(Qt::PointingHandCursor);
    penCfgToggle->setFocusPolicy(Qt::NoFocus);
    penCfgToggle->setFixedHeight(28);
    column->addWidget(penCfgToggle);

    auto* penCfgBox = new QWidget(this);
    penCfgBox->setVisible(false);
    auto* pressureColumn = new QVBoxLayout(penCfgBox);
    pressureColumn->setContentsMargins(0, 4, 0, 4);
    pressureColumn->setSpacing(6);
    connect(penCfgToggle, &QToolButton::clicked, this, [this, penCfgBox, penCfgToggle] {
        const bool show = penCfgBox->isHidden();
        penCfgBox->setVisible(show);
        penCfgToggle->setText(show ? QStringLiteral("Ajustes de lápiz y tableta  ▴")
                                   : QStringLiteral("Ajustes de lápiz y tableta  ▾"));
        adjustSize();
        emit layoutAdjusted();
    });

    auto* pressureHdr = new QHBoxLayout();
    pressureHdr->setSpacing(6);
    auto* pressureTitle = new QLabel(QStringLiteral("Sensibilidad a la presión"), penCfgBox);
    pressureTitle->setObjectName(QStringLiteral("finilySectionTitle"));
    pressureHdr->addWidget(pressureTitle, 1);
    m_pressureLabel = new QLabel(QStringLiteral("50%"), penCfgBox);
    m_pressureLabel->setObjectName(QStringLiteral("finilyValue"));
    pressureHdr->addWidget(m_pressureLabel);
    pressureColumn->addLayout(pressureHdr);

    m_pressureSlider = new DotStepSlider(penCfgBox);
    m_pressureSlider->setRange(0, 10);
    m_pressureSlider->setSingleStep(1);
    m_pressureSlider->setPageStep(1);
    m_pressureSlider->setToolTip(
        QStringLiteral("Intensidad de presión del lápiz (0% = trazo uniforme, 100% = contraste máximo)"));
    connect(m_pressureSlider, &QSlider::valueChanged, this, [this](int level) {
        if (m_pressureLabel)
            m_pressureLabel->setText(QStringLiteral("%1%").arg(level * 10));
        if (m_updatingUi || !m_canvas)
            return;
        m_canvas->setPressureSensitivity(double(level) / 10.0);
    });
    pressureColumn->addWidget(m_pressureSlider);

    m_swapButtonsBtn = new SwitchButton(penCfgBox);
    m_swapButtonsBtn->setFixedSize(272, 30);
    m_swapButtonsBtn->setText(QStringLiteral("Invertir botones del lápiz"));
    m_swapButtonsBtn->setCheckable(true);
    m_swapButtonsBtn->setChecked(m_canvas && m_canvas->swapPenButtons());
    connect(m_swapButtonsBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas && m_swapButtonsBtn)
            m_canvas->setSwapPenButtons(m_swapButtonsBtn->isChecked());
    });
    pressureColumn->addWidget(m_swapButtonsBtn);

    m_mathBeautifyBtn = new SwitchButton(penCfgBox);
    m_mathBeautifyBtn->setFixedSize(272, 30);
    m_mathBeautifyBtn->setText(QStringLiteral("Embellecer letra por palabra (M)"));
    m_mathBeautifyBtn->setToolTip(
        QStringLiteral("Al terminar de escribir cada palabra, suaviza y alinea tu letra automáticamente "
                       "respetando tu unión y espaciado"));
    m_mathBeautifyBtn->setCheckable(true);
    m_mathBeautifyBtn->setChecked(m_canvas && m_canvas->mathBeautifyEnabled());
    connect(m_mathBeautifyBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas && m_mathBeautifyBtn)
            m_canvas->setMathBeautifyEnabled(m_mathBeautifyBtn->isChecked());
        emit refreshToolbarRequested();
    });
    pressureColumn->addWidget(m_mathBeautifyBtn);

    m_holdToShapeBtn = new SwitchButton(penCfgBox);
    m_holdToShapeBtn->setFixedSize(272, 30);
    m_holdToShapeBtn->setText(QStringLiteral("Auto-forma al mantener"));
    m_holdToShapeBtn->setCheckable(true);
    m_holdToShapeBtn->setChecked(m_canvas ? m_canvas->holdToShapeEnabled() : true);
    connect(m_holdToShapeBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas && m_holdToShapeBtn)
            m_canvas->setHoldToShapeEnabled(m_holdToShapeBtn->isChecked());
        emit refreshToolbarRequested();
    });
    pressureColumn->addWidget(m_holdToShapeBtn);

    m_smartGuidesBtn = new SwitchButton(penCfgBox);
    m_smartGuidesBtn->setFixedSize(272, 30);
    m_smartGuidesBtn->setText(QStringLiteral("Guías inteligentes e imán"));
    m_smartGuidesBtn->setCheckable(true);
    m_smartGuidesBtn->setChecked(m_canvas ? m_canvas->smartGuidesEnabled() : true);
    connect(m_smartGuidesBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas && m_smartGuidesBtn)
            m_canvas->setSmartGuidesEnabled(m_smartGuidesBtn->isChecked());
        emit refreshToolbarRequested();
    });
    pressureColumn->addWidget(m_smartGuidesBtn);

    auto* cursorLbl = new QLabel(QStringLiteral("Puntero de lápiz"), penCfgBox);
    cursorLbl->setObjectName(QStringLiteral("finilySectionTitle"));
    pressureColumn->addWidget(cursorLbl);

    auto* cursorRow = new QHBoxLayout();
    cursorRow->setSpacing(6);
    const QStringList cursorNames = {
        QStringLiteral("Lapicito"),
        QStringLiteral("Aro"),
        QStringLiteral("Cruz"),
    };
    for (int i = 0; i < cursorNames.size(); ++i) {
        auto* btn = new QToolButton(penCfgBox);
        btn->setFixedSize(86, 26);
        btn->setText(cursorNames[i]);
        btn->setCheckable(true);
        polishButton(btn);
        connect(btn, &QToolButton::clicked, this, [this, i] {
            if (m_canvas)
                m_canvas->setCursorStyle(i);
            syncFromCanvas();
            emit refreshToolbarRequested();
        });
        m_cursorBtns.push_back(btn);
        cursorRow->addWidget(btn);
    }
    pressureColumn->addLayout(cursorRow);
    column->addWidget(penCfgBox);

    column->addWidget(makeDivider(this));

    auto* aiCfgBtn = new QToolButton(this);
    aiCfgBtn->setFixedSize(272, 34);
    aiCfgBtn->setText(QStringLiteral("✨ Configurar IA de extracción…"));
    aiCfgBtn->setToolTip(
        QStringLiteral("Configura tu clave API de Gemini para extraer figuras y texto de imágenes"));
    polishButton(aiCfgBtn);
    makeMenuRow(aiCfgBtn);
    connect(aiCfgBtn, &QToolButton::clicked, this, [this] {
        emit closeRequested();
        emit configureAiApiRequested();
    });
    column->addWidget(aiCfgBtn);

    m_updateBtn = new QToolButton(this);
    m_updateBtn->setFixedSize(272, 34);
    m_updateBtn->setText(
        QStringLiteral("Buscar actualizaciones…  (v%1)").arg(QApplication::applicationVersion()));
    m_updateBtn->setToolTip(QStringLiteral("Comprueba si existe una nueva versión de Finily en GitHub"));
    polishButton(m_updateBtn);
    makeMenuRow(m_updateBtn);
    connect(m_updateBtn, &QToolButton::clicked, this, [this] {
        emit closeRequested();
        emit checkForUpdatesRequested();
    });
    column->addWidget(m_updateBtn);

    column->addStretch(1);
    syncFromCanvas();
}

void FilePopover::setUpdateAvailable(const QString& version) {
    if (!m_updateBtn)
        return;
    QLabel* titleLabel = nullptr;
    QLabel* shortcutLabel = nullptr;
    for (QLabel* lbl : m_updateBtn->findChildren<QLabel*>()) {
        if (lbl->objectName() == QLatin1String("finilyShortcut"))
            shortcutLabel = lbl;
        else if (!titleLabel)
            titleLabel = lbl;
    }
    const QString clean = version.trimmed();
    if (clean.isEmpty()) {
        if (titleLabel)
            titleLabel->setText(QStringLiteral("Buscar actualizaciones…"));
        if (shortcutLabel) {
            shortcutLabel->setText(QStringLiteral("v%1").arg(QApplication::applicationVersion()));
            shortcutLabel->show();
        }
    } else {
        if (titleLabel)
            titleLabel->setText(QStringLiteral("Actualizar a v%1…").arg(clean));
        if (shortcutLabel)
            shortcutLabel->hide();
    }
}

void FilePopover::rememberRecentImage(const QString& path) {
    if (path.isEmpty())
        return;
    auto settings = appSettings();
    QStringList recent = settings->value(QStringLiteral("ui/recentImages")).toStringList();
    recent.removeAll(path);
    recent.prepend(path);
    while (recent.size() > 4)
        recent.removeLast();
    settings->setValue(QStringLiteral("ui/recentImages"), recent);
    rebuildRecentGallery();
}

void FilePopover::rebuildRecentGallery() {
    if (!m_recentGalleryWrap || !m_recentGalleryRow)
        return;
    const bool wasVisible = !m_recentGalleryWrap->isHidden();
    while (QLayoutItem* item = m_recentGalleryRow->takeAt(0)) {
        if (QWidget* w = item->widget())
            w->deleteLater();
        delete item;
    }

    auto settings = appSettings();
    const QStringList raw = settings->value(QStringLiteral("ui/recentImages")).toStringList();
    QStringList valid;
    for (const QString& p : raw) {
        if (QFileInfo::exists(p))
            valid.push_back(p);
        if (valid.size() >= 4)
            break;
    }
    if (valid.isEmpty()) {
        m_recentGalleryWrap->hide();
        if (wasVisible) {
            adjustSize();
            emit layoutAdjusted();
        }
        return;
    }

    m_recentGalleryWrap->show();
    for (const QString& path : valid) {
        auto* btn = new QToolButton(m_recentGalleryWrap);
        btn->setFixedSize(62, 44);
        btn->setIconSize(QSize(56, 38));
        btn->setToolTip(QStringLiteral("Insertar: ") + QFileInfo(path).fileName());
        polishButton(btn);
        QPixmap px(path);
        if (!px.isNull())
            btn->setIcon(QIcon(px.scaled(56, 38, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
        else
            btn->setText(QFileInfo(path).fileName().left(6));
        connect(btn, &QToolButton::clicked, this, [this, path] {
            emit closeRequested();
            if (m_canvas && QFileInfo::exists(path)) {
                m_canvas->importImageFile(path);
                rememberRecentImage(path);
                emit refreshToolbarRequested();
            }
        });
        m_recentGalleryRow->addWidget(btn);
    }
    m_recentGalleryRow->addStretch(1);
    if (!wasVisible) {
        adjustSize();
        emit layoutAdjusted();
    }
}

void FilePopover::syncFromCanvas() {
    if (!m_canvas)
        return;
    m_updatingUi = true;

    const int pdfPageCount = m_canvas->documentPdfPageCount();
    const bool showPdf = pdfPageCount > 0;
    const bool pdfVisChanged = m_pdfInfoLabel && (!m_pdfInfoLabel->isHidden() != showPdf);
    if (m_pdfInfoLabel) {
        m_pdfInfoLabel->setVisible(showPdf);
        if (showPdf)
            m_pdfInfoLabel->setText(tr("Páginas de PDF en el cuaderno · %1").arg(pdfPageCount));
    }

    const int pressureLevel = qRound(m_canvas->pressureSensitivity() * 10.0);
    if (m_pressureSlider && m_pressureSlider->value() != pressureLevel)
        m_pressureSlider->setValue(pressureLevel);
    if (m_pressureLabel)
        m_pressureLabel->setText(QStringLiteral("%1%").arg(pressureLevel * 10));

    if (m_swapButtonsBtn)
        m_swapButtonsBtn->setChecked(m_canvas->swapPenButtons());
    if (m_mathBeautifyBtn)
        m_mathBeautifyBtn->setChecked(m_canvas->mathBeautifyEnabled());
    if (m_holdToShapeBtn)
        m_holdToShapeBtn->setChecked(m_canvas->holdToShapeEnabled());
    if (m_smartGuidesBtn)
        m_smartGuidesBtn->setChecked(m_canvas->smartGuidesEnabled());

    const int curStyle = m_canvas->cursorStyle();
    for (int i = 0; i < m_cursorBtns.size(); ++i)
        m_cursorBtns.at(i)->setChecked(i == curStyle);

    m_updatingUi = false;

    if (pdfVisChanged) {
        adjustSize();
        emit layoutAdjusted();
    }
}

void FilePopover::applyTheme(const ThemeConfig& theme, const QString& qss) {
    GlassCapsule::applyTheme(theme, qss);
    for (QToolButton* b : findChildren<QToolButton*>(QStringLiteral("finilySwitch"))) {
        if (auto* sw = dynamic_cast<SwitchButton*>(b))
            sw->setColors(theme.uiText, theme.accent, theme.isDark);
    }
    if (auto* ds = dynamic_cast<DotStepSlider*>(m_pressureSlider))
        ds->setColors(theme.accent, theme.isDark);
    for (const auto& spec : m_iconSpecs)
        spec.first->setIcon(glyphIcon(spec.second, theme.uiText));
}

} // namespace finily
