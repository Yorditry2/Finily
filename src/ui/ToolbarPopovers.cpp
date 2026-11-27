#include "ui/FloatingToolbar.h"

#include "ui/CanvasWidget.h"
#include "ui/FloatingToolbarInternal.h"
#include "ui/popovers/FilePopover.h"
#include "ui/popovers/StickersPopover.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QToolButton>

#include <algorithm>

namespace finily {

void FloatingToolbar::setUpdateAvailable(const QString& version) {
    if (m_filePopover) {
        m_filePopover->setUpdateAvailable(version);
    }
}

void FloatingToolbar::openStickersPopover() {
    if (!m_stickersPopover || !m_stickersBtn) {
        return;
    }
    openPopover(m_stickersPopover, m_stickersBtn);
    if (m_openPopover == m_stickersPopover) {
        m_stickersPopover->onOpened();
    }
}

void FloatingToolbar::buildContextPill() {
    m_contextPill = new GlassCapsule(this);
    m_contextPill->setObjectName(QStringLiteral("finilyContext"));
    auto* row = new QHBoxLayout(m_contextPill);
    row->setContentsMargins(6, 5, 6, 5);
    row->setSpacing(3);

    auto makeCtxBtn = [&](const QString& iconKind, const QString& tip, bool accent = false) {
        auto* b = new QToolButton(m_contextPill);
        if (accent) {
            b->setObjectName(QStringLiteral("finilyAccentBtn"));
        }
        b->setFixedSize(32, 32);
        b->setIconSize(QSize(18, 18));
        b->setToolButtonStyle(Qt::ToolButtonIconOnly);
        b->setToolTip(tip);
        polishButton(b);
        m_ctxIconSpecs.append(qMakePair(b, iconKind));
        row->addWidget(b);
        return b;
    };

    m_finishPolylineBtn = makeCtxBtn(QStringLiteral("check"), QStringLiteral("Terminar polilínea (Enter)"));
    connect(m_finishPolylineBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas) {
            m_canvas->finishActivePolyline(false);
        }
        refreshFromCanvas();
    });

    m_closePolylineBtn = makeCtxBtn(QStringLiteral("close-poly"), QStringLiteral("Cerrar polígono"));
    connect(m_closePolylineBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas) {
            m_canvas->finishActivePolyline(true);
        }
        refreshFromCanvas();
    });

    m_ctxBeautifyBtn =
        makeCtxBtn(QStringLiteral("straighten"), tr("Embellecer y ordenar escritura"), true);
    connect(m_ctxBeautifyBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas) {
            m_canvas->straightenSelection();
        }
        refreshFromCanvas();
    });

    m_ctxAxesFuncBtn =
        makeCtxBtn(QStringLiteral("fx"), tr("Graficar función f(x) en estos ejes…"), true);
    connect(m_ctxAxesFuncBtn, &QToolButton::clicked, this, [this] {
        if (!m_canvas) {
            return;
        }
        bool ok = false;
        const QString current = m_canvas->selectedAxesFunction();
        const QString expr = QInputDialog::getText(
            window(), tr("Graficar función en Ejes X/Y"),
            tr("Escribe f(x) (ej: x^2 - 4, sin(x), 2*x + 1, sqrt(x), cos(x)):"),
            QLineEdit::Normal, current.isEmpty() ? QStringLiteral("x^2 - 4") : current, &ok);
        if (ok) {
            m_canvas->setSelectedAxesFunction(expr.trimmed());
        }
        refreshFromCanvas();
    });

    m_ctxDuplicateBtn = makeCtxBtn(QStringLiteral("duplicate"), tr("Duplicar selección (Ctrl+D)"));
    connect(m_ctxDuplicateBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas) {
            m_canvas->duplicateSelection();
        }
        refreshFromCanvas();
    });

    m_ctxDeleteBtn = makeCtxBtn(QStringLiteral("trash"), tr("Eliminar selección (Supr)"));
    connect(m_ctxDeleteBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas) {
            m_canvas->deleteSelection();
        }
        refreshFromCanvas();
    });

    m_selectionActions = makeCtxBtn(QStringLiteral("more"), tr("Más acciones de selección…"));
    connect(m_selectionActions, &QToolButton::clicked, this, [this] {
        showSelectionMenu(m_selectionActions->mapToGlobal(QPoint(0, 0)));
    });
    m_contextPill->hide();
}

void FloatingToolbar::showSelectionMenu(const QPoint& position) {
    if (!m_canvas || m_cleanUi || !m_canvas->hasSelection() || m_canvas->isDrawingPolyline()) {
        return;
    }
    QMenu menu(this);
    if (m_canvas->hasExtractableImage()) {
        menu.addAction(tr("📐 Vectorizar figura (instantáneo)"), this,
                       [this] { emit vectorizeSelectedImageRequested(); });
        menu.addAction(tr("✨ Extraer figuras y texto (IA)…"), this,
                       [this] { emit extractSelectedImageRequested(); });
        menu.addSeparator();
    }
    const auto style = m_canvas->selectionStyle();
    if (style.color || style.filled) {
        menu.addAction(tr("Propiedades…"), this, &FloatingToolbar::editSelectionProperties);
    }
    if (m_canvas->hasSelectedAxes()) {
        menu.addAction(tr("Graficar función f(x) en estos ejes…"), this, [this] {
            if (!m_canvas) {
                return;
            }
            bool ok = false;
            const QString current = m_canvas->selectedAxesFunction();
            const QString expr = QInputDialog::getText(
                window(), tr("Graficar función en Ejes X/Y"),
                tr("Escribe f(x) (ej: x^2 - 4, sin(x), 2*x + 1, sqrt(x), cos(x)):"),
                QLineEdit::Normal, current.isEmpty() ? QStringLiteral("x^2 - 4") : current, &ok);
            if (ok) {
                m_canvas->setSelectedAxesFunction(expr.trimmed());
            }
        });
    }
    menu.addAction(tr("Enviar recorte a página nueva"), this,
                   [this] { m_canvas->cropSelectionToNewPage(); });
    menu.addAction(tr("Abrir espacio vertical debajo"), this,
                   [this] { m_canvas->insertVerticalSpaceAtSelection(140.0); });
    if (m_canvas->hasLassoSelection() || m_canvas->hasSelectedShape() || m_canvas->hasSelectedText()) {
        menu.addAction(tr("Guardar como sticker ★"), this, [this] {
            if (!m_canvas) {
                return;
            }
            bool ok = false;
            const QString name = QInputDialog::getText(window(), tr("Guardar como sticker"),
                                                       tr("Nombre corto para el sticker:"),
                                                       QLineEdit::Normal, QString(), &ok)
                                     .trimmed();
            if (ok && !name.isEmpty()) {
                if (m_canvas->saveSelectionAsSticker(name) && m_stickersPopover) {
                    m_stickersPopover->rebuildGrid();
                }
            }
        });
    }
    menu.addSeparator();
    menu.addAction(tr("Cortar"), this, [this] { m_canvas->cutSelection(); });
    menu.addAction(tr("Copiar"), this, [this] { m_canvas->copySelection(); });
    menu.addAction(tr("Duplicar"), this, [this] { m_canvas->duplicateSelection(); });
    menu.addSeparator();
    menu.addAction(tr("Voltear horizontalmente"), this, [this] { m_canvas->flipSelectionHorizontal(); });
    menu.addAction(tr("Voltear verticalmente"), this, [this] { m_canvas->flipSelectionVertical(); });
    menu.addAction(tr("Traer al frente"), this, [this] { m_canvas->bringSelectionToFront(); });
    menu.addAction(tr("Enviar al fondo"), this, [this] { m_canvas->sendSelectionToBack(); });
    if (m_canvas->canGroupSelection()) {
        menu.addAction(tr("Agrupar"), this, [this] { m_canvas->groupSelection(); });
    }
    if (m_canvas->canUngroupSelection()) {
        menu.addAction(tr("Desagrupar"), this, [this] { m_canvas->ungroupSelection(); });
    }
    if (m_canvas->hasLockableSelection()) {
        menu.addAction(m_canvas->isSelectionLocked() ? tr("Desbloquear") : tr("Bloquear"), this,
                       [this] { m_canvas->toggleLockSelection(); });
    }
    if (m_canvas->hasSelectedStrokes()) {
        menu.addSeparator();
        menu.addAction(tr("Embellecer y ordenar escritura"), this,
                       [this] { m_canvas->straightenSelection(); });
    }
    menu.addSeparator();
    menu.addAction(tr("Eliminar"), this, [this] { m_canvas->deleteSelection(); });
    menu.exec(position);
}

void FloatingToolbar::editSelectionProperties() {
    if (!m_canvas) {
        return;
    }
    const auto current = m_canvas->selectionStyle();
    SelectionStyle edits;
    QDialog dialog(window());
    dialog.setWindowTitle(tr("Propiedades de la selección"));
    auto* form = new QFormLayout(&dialog);
    auto colorField = [&](const QString& label, QColor initial, std::optional<QColor>& value) {
        auto* button = new QPushButton(initial.name(), &dialog);
        connect(button, &QPushButton::clicked, &dialog, [button, &dialog, &value] {
            const auto color = QColorDialog::getColor(QColor(button->text()), &dialog);
            if (color.isValid()) {
                value = color;
                button->setText(color.name());
            }
        });
        form->addRow(label, button);
        return button;
    };
    auto options = [&](const QString& label, const QStringList& names, int currentIndex) {
        auto* combo = new QComboBox(&dialog);
        combo->addItems(names);
        combo->setCurrentIndex(std::clamp(currentIndex, 0, int(names.size()) - 1));
        form->addRow(label, combo);
        return combo;
    };
    if (current.color) {
        colorField(current.width ? tr("Color de contorno") : tr("Color"), *current.color, edits.color);
    }
    if (current.width) {
        auto* width = new QDoubleSpinBox(&dialog);
        width->setDecimals(1);
        width->setRange(.1, 200);
        width->setSingleStep(.5);
        width->setSuffix(tr(" px"));
        width->setValue(*current.width);
        connect(width, &QDoubleSpinBox::valueChanged, &dialog, [&](double value) { edits.width = value; });
        form->addRow(tr("Grosor"), width);
    }
    if (current.pattern && *current.pattern != LinePattern::Dimension
        && int(*current.pattern) <= int(LinePattern::Dotted)) {
        auto* pattern =
            options(tr("Línea"), {tr("Sólida"), tr("Guiones"), tr("Punteada")}, int(*current.pattern));
        connect(pattern, &QComboBox::currentIndexChanged, &dialog,
                [&](int index) { edits.pattern = LinePattern(index); });
    }
    if (current.ending) {
        auto* ending =
            options(tr("Remates"), {tr("Sin punta"), tr("Flecha"), tr("Doble flecha")}, int(*current.ending));
        connect(ending, &QComboBox::currentIndexChanged, &dialog,
                [&](int index) { edits.ending = LineEnding(index); });
    }
    if (current.label) {
        auto* label = new QLineEdit(*current.label, &dialog);
        label->setMaxLength(80);
        label->setPlaceholderText(tr("Escribe una medida o una etiqueta"));
        connect(label, &QLineEdit::textChanged, &dialog,
                [&](const QString& value) { edits.label = value.trimmed(); });
        form->addRow(tr("Etiqueta"), label);
    }
    if (current.filled) {
        auto* filled = new QCheckBox(tr("Con relleno"), &dialog);
        filled->setChecked(*current.filled);
        form->addRow(filled);
        auto* fillColor = colorField(tr("Color de relleno"),
                                     current.fillColor.value_or(current.color.value_or(m_canvas->inkColor())),
                                     edits.fillColor);
        auto* pattern = options(tr("Patrón de relleno"),
                                {tr("Suave"), tr("Diagonal"), tr("Punteado"), tr("Sólido"),
                                 tr("Rayado cruzado"), tr("Cuadrícula")},
                                int(current.fillPattern.value_or(ShadePattern::Soft)));
        fillColor->setEnabled(*current.filled);
        pattern->setEnabled(*current.filled);
        connect(filled, &QCheckBox::toggled, &dialog, [&, fillColor, pattern](bool checked) {
            edits.filled = checked;
            fillColor->setEnabled(checked);
            pattern->setEnabled(checked);
        });
        connect(pattern, &QComboBox::currentIndexChanged, &dialog,
                [&](int index) { edits.fillPattern = ShadePattern(index); });
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Aplicar"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancelar"));
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() == QDialog::Accepted && m_canvas) {
        m_canvas->setSelectionStyle(edits);
    }
}

void FloatingToolbar::importPdfDialog() {
    if (!m_canvas) {
        return;
    }
    closePopovers();
    const QString path =
        QFileDialog::getOpenFileName(window(), tr("Importar PDF"), QString(), tr("Archivos PDF (*.pdf)"));
    if (path.isEmpty()) {
        return;
    }
    m_canvas->importPdfFile(path);
    refreshFromCanvas();
}

void FloatingToolbar::exportPdfDialog() {
    if (!m_canvas) {
        return;
    }
    closePopovers();
    QString path = QFileDialog::getSaveFileName(window(), tr("Exportar clase a PDF"),
                                                QStringLiteral("finily-clase.pdf"),
                                                tr("Archivos PDF (*.pdf)"));
    if (path.isEmpty()) {
        return;
    }
    if (!path.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive)) {
        path += QStringLiteral(".pdf");
    }
    m_canvas->exportToPdf(path);
    refreshFromCanvas();
}

void FloatingToolbar::newDocumentAction() {
    if (!m_canvas) {
        return;
    }
    closePopovers();
    if (!confirmDiscardChanges(QStringLiteral("crear un cuaderno nuevo"))) {
        return;
    }
    m_canvas->newDocument();
    refreshFromCanvas();
}

void FloatingToolbar::openNotebookDialog() {
    if (!m_canvas) {
        return;
    }
    closePopovers();
    if (!confirmDiscardChanges(QStringLiteral("abrir otro cuaderno"))) {
        return;
    }
    const QString path = QFileDialog::getOpenFileName(window(), tr("Abrir cuaderno"), QString(),
                                                      tr("Cuaderno Finily (*.finily)"));
    if (path.isEmpty()) {
        return;
    }
    if (!m_canvas->loadFromFile(path)) {
        showToast(tr("No se pudo abrir el cuaderno"));
    }
    refreshFromCanvas();
}

bool FloatingToolbar::confirmDiscardChanges(const QString& action) {
    if (!m_canvas || !m_canvas->hasUnsavedChanges()) {
        return true;
    }
    QMessageBox box(QMessageBox::Warning, tr("Cambios sin guardar"),
                    tr("El cuaderno actual tiene cambios sin guardar.\n"
                       "¿Quieres guardarlos antes de %1?")
                        .arg(action),
                    QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, window());
    box.setDefaultButton(QMessageBox::Save);
    box.button(QMessageBox::Save)->setText(tr("Guardar"));
    box.button(QMessageBox::Discard)->setText(tr("No guardar"));
    box.button(QMessageBox::Cancel)->setText(tr("Cancelar"));
    switch (box.exec()) {
        case QMessageBox::Save:
            saveNotebookAction();
            return !m_canvas->hasUnsavedChanges();
        case QMessageBox::Discard:
            return true;
        default:
            return false;
    }
}

void FloatingToolbar::saveNotebookAction() {
    if (!m_canvas) {
        return;
    }
    closePopovers();
    const QString current = m_canvas->currentFilePath();
    if (current.isEmpty()) {
        saveNotebookAsDialog();
        return;
    }
    m_canvas->saveToFile(current);
    refreshFromCanvas();
}

void FloatingToolbar::saveNotebookAsDialog() {
    if (!m_canvas) {
        return;
    }
    closePopovers();
    QString path = QFileDialog::getSaveFileName(window(), tr("Guardar cuaderno"),
                                                QStringLiteral("cuaderno.finily"),
                                                tr("Cuaderno Finily (*.finily)"), nullptr,
                                                QFileDialog::DontConfirmOverwrite);
    if (path.isEmpty()) {
        return;
    }
    if (!path.endsWith(QStringLiteral(".finily"), Qt::CaseInsensitive)) {
        path += QStringLiteral(".finily");
    }
    const bool exists = QFileInfo::exists(path);
    if (exists) {
        QMessageBox confirmation(QMessageBox::Question, tr("Reemplazar cuaderno"),
                                 tr("¿Quieres reemplazar «%1»?").arg(QFileInfo(path).fileName()),
                                 QMessageBox::Save | QMessageBox::Cancel, window());
        confirmation.button(QMessageBox::Save)->setText(tr("Reemplazar"));
        confirmation.button(QMessageBox::Cancel)->setText(tr("Cancelar"));
        confirmation.setDefaultButton(QMessageBox::Cancel);
        if (confirmation.exec() != QMessageBox::Save) {
            return;
        }
    }
    m_canvas->saveToFile(path, !exists);
    refreshFromCanvas();
}

} // namespace finily
