#include "ui/editor/EditorContextCapsules.h"

#include "core/ShapeProtocols.h"
#include "engine/StickerLibrary.h"
#include "ui/CanvasWidget.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace finily {

// ---------------- EditorContextCapsule ----------------

EditorContextCapsule::EditorContextCapsule(CanvasWidget* canvas, QWidget* parent)
    : EditorGlassCard(parent),
      m_canvas(canvas) {
    setRole(Role::ContextCapsule);
    setCornerRadius(12.0);
    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(6, 5, 6, 5);
    m_layout->setSpacing(4);
    hide();
}

void EditorContextCapsule::setDropdownOpener(DropdownRequestFn fn) {
    m_dropdownOpener = std::move(fn);
}

void EditorContextCapsule::applyEditorTheme(bool dark) {
    EditorGlassCard::applyEditorTheme(dark);
    m_lastSignature.clear();
    syncFromCanvas();
}

void EditorContextCapsule::clearControls() {
    while (QLayoutItem* item = m_layout->takeAt(0)) {
        if (item->widget()) {
            item->widget()->hide();
            item->widget()->deleteLater();
        }
        delete item;
    }
}

void EditorContextCapsule::addMoreButton() {
    auto* moreBtn = new EditorIconButton(QStringLiteral("more"), QStringLiteral("ui"), this);
    moreBtn->setFixedSize(28, 28);
    moreBtn->setIconSizePx(15.0);
    moreBtn->applyEditorTheme(m_dark);
    connect(moreBtn, &EditorIconButton::clicked, this, [this, moreBtn] {
        if (!m_canvas || !m_dropdownOpener)
            return;
        const bool locked = m_canvas->isSelectionLocked();
        QVector<EditorDropdownItem> items = {
            {1, QStringLiteral("Cortar"), QStringLiteral("Ctrl+X"), QStringLiteral("cut"), QStringLiteral("editor")},
            {2, QStringLiteral("Copiar"), QStringLiteral("Ctrl+C"), QStringLiteral("copy"), QStringLiteral("ui")},
            {3, QStringLiteral("Pegar"), QStringLiteral("Ctrl+V"), QStringLiteral("paste"), QStringLiteral("editor")},
            {4, QStringLiteral("Duplicar"), QStringLiteral("Ctrl+D"), QStringLiteral("copy"), QStringLiteral("ui")},
            {5, QStringLiteral("Voltear horizontal"), {}, QStringLiteral("flip-h"), QStringLiteral("editor"), true},
            {6, QStringLiteral("Voltear vertical"), {}, QStringLiteral("flip-v"), QStringLiteral("editor")},
            {7, QStringLiteral("Traer al frente"), {}, QStringLiteral("front"), QStringLiteral("editor")},
            {8, QStringLiteral("Enviar al fondo"), {}, QStringLiteral("back"), QStringLiteral("editor")},
            {9, m_canvas->canUngroupSelection() ? QStringLiteral("Desagrupar") : QStringLiteral("Agrupar"),
                QStringLiteral("Ctrl+G"), QStringLiteral("group"), QStringLiteral("editor"), true},
            {10, locked ? QStringLiteral("Desbloquear") : QStringLiteral("Bloquear"),
                 {}, locked ? QStringLiteral("unlock") : QStringLiteral("lock"), QStringLiteral("editor")},
            {11, QStringLiteral("Abrir espacio debajo"), {}, QStringLiteral("space-down"), QStringLiteral("editor"), true},
            {12, QStringLiteral("Enviar a página nueva"), {}, QStringLiteral("send-page"), QStringLiteral("editor")},
            {13, QStringLiteral("Grosor exacto…"), {}, QStringLiteral("sliders"), QStringLiteral("editor"), true},
        };
        m_dropdownOpener(moreBtn, items, [this](int id) {
            if (!m_canvas)
                return;
            switch (id) {
            case 1: m_canvas->cutSelection(); break;
            case 2: m_canvas->copySelection(); break;
            case 3: m_canvas->pasteFromClipboard(); break;
            case 4: m_canvas->duplicateSelection(); break;
            case 5: m_canvas->flipSelectionHorizontal(); break;
            case 6: m_canvas->flipSelectionVertical(); break;
            case 7: m_canvas->bringSelectionToFront(); break;
            case 8: m_canvas->sendSelectionToBack(); break;
            case 9:
                if (m_canvas->canUngroupSelection()) m_canvas->ungroupSelection();
                else m_canvas->groupSelection();
                break;
            case 10: m_canvas->toggleLockSelection(); break;
            case 11: m_canvas->insertVerticalSpaceAtSelection(140.0); break;
            case 12: m_canvas->cropSelectionToNewPage(); break;
            case 13:
                m_fineThicknessOpen = !m_fineThicknessOpen;
                m_lastSignature.clear();
                syncFromCanvas();
                break;
            default: break;
            }
        });
    });
    m_layout->addWidget(moreBtn);
}

void EditorContextCapsule::syncFromCanvas() {
    if (!m_canvas || (!m_canvas->isDrawingPolyline() && !m_canvas->hasSelection())) {
        m_saveElementOpen = false;
        m_fineThicknessOpen = false;
        m_lastSignature.clear();
        if (isVisible()) {
            hide();
            emit layoutChanged();
        }
        return;
    }

    int famVal = 0;
    const ShapeFamily fam = m_canvas->hasSelectedShape() ? m_canvas->selectedShapeFamily(&famVal) : ShapeFamily::None;
    const SelectionStyle st = m_canvas->selectionStyle();
    const bool isPolyline = m_canvas->isDrawingPolyline();
    const bool isAxes = m_canvas->hasSelectedAxes();
    const bool isImage = m_canvas->hasExtractableImage();
    const bool isShape = m_canvas->hasSelectedShape();
    const bool isAngle = isShape && st.label.has_value();
    const bool isStrokes = m_canvas->hasLassoSelection() && m_canvas->hasSelectedStrokes();
    const bool editableField = isAxes || isAngle || m_saveElementOpen;

    QString sig = QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8|%9")
                      .arg(m_dark)
                      .arg(isPolyline)
                      .arg(isAxes)
                      .arg(isImage)
                      .arg(isShape)
                      .arg(int(fam))
                      .arg(isAngle)
                      .arg(isStrokes)
                      .arg(m_saveElementOpen * 2 + m_fineThicknessOpen);
    if (!editableField && isShape) {
        sig += QStringLiteral("|%1|%2|%3|%4|%5")
                   .arg(famVal)
                   .arg(st.fillColor ? st.fillColor->rgba() : 0u)
                   .arg(int(st.pattern.value_or(LinePattern::Solid)))
                   .arg(int(st.ending.value_or(LineEnding::None)))
                   .arg(qRound(st.width.value_or(0.0) * 10.0));
    }

    if (sig == m_lastSignature && isVisible())
        return;
    m_lastSignature = sig;

    clearControls();

    auto makeSep = [this] {
        auto* div = new QWidget(this);
        div->setFixedSize(1, 18);
        div->setStyleSheet(QStringLiteral("background: %1;").arg(m_pal.border.name(QColor::HexArgb)));
        m_layout->addWidget(div);
    };

    auto addDupAndDel = [this] {
        auto* dupBtn = new EditorIconButton(QStringLiteral("copy"), QStringLiteral("ui"), this);
        dupBtn->setFixedSize(28, 28);
        dupBtn->setIconSizePx(15.0);
        dupBtn->applyEditorTheme(m_dark);
        dupBtn->setToolTip(QStringLiteral("Duplicar"));
        connect(dupBtn, &EditorIconButton::clicked, this, [this] {
            if (m_canvas) m_canvas->duplicateSelection();
        });
        m_layout->addWidget(dupBtn);

        auto* delBtn = new EditorIconButton(QStringLiteral("trash"), QStringLiteral("ui"), this);
        delBtn->setFixedSize(28, 28);
        delBtn->setIconSizePx(15.0);
        delBtn->setDanger(true);
        delBtn->applyEditorTheme(m_dark);
        delBtn->setToolTip(QStringLiteral("Eliminar"));
        connect(delBtn, &EditorIconButton::clicked, this, [this] {
            if (m_canvas) m_canvas->deleteSelection();
        });
        m_layout->addWidget(delBtn);
    };

    if (isPolyline) {
        auto* finishBtn = new EditorChipButton(QStringLiteral("Terminar"), this);
        finishBtn->setChecked(true);
        finishBtn->applyEditorTheme(m_dark);
        connect(finishBtn, &EditorChipButton::clicked, this, [this] {
            if (m_canvas) m_canvas->finishActivePolyline(false);
        });
        auto* closeBtn = new EditorChipButton(QStringLiteral("Cerrar"), this);
        closeBtn->applyEditorTheme(m_dark);
        connect(closeBtn, &EditorChipButton::clicked, this, [this] {
            if (m_canvas) m_canvas->finishActivePolyline(true);
        });
        m_layout->addWidget(finishBtn);
        m_layout->addWidget(closeBtn);
    } else if (isAxes) {
        auto* lbl = new QLabel(QStringLiteral("f(x) ="), this);
        lbl->setFont(editorFont(11.5, QFont::DemiBold));
        lbl->setStyleSheet(QStringLiteral("color: %1; padding: 0 4px;").arg(m_pal.muted.name()));
        m_layout->addWidget(lbl);

        auto* edit = new EditorLineEdit(this);
        edit->applyEditorTheme(m_dark);
        edit->setFixedSize(130, 26);
        edit->setText(m_canvas->selectedAxesFunction());
        connect(edit, &QLineEdit::editingFinished, this, [this, edit] {
            if (m_canvas) m_canvas->setSelectedAxesFunction(edit->text());
        });
        connect(edit, &QLineEdit::returnPressed, this, [this, edit] {
            if (m_canvas) m_canvas->setSelectedAxesFunction(edit->text());
        });
        m_layout->addWidget(edit);
        makeSep();
        addDupAndDel();
        addMoreButton();
    } else if (isImage) {
        auto* cropBtn = new EditorChipButton(QStringLiteral("Recortar"), this);
        cropBtn->setIcon(QStringLiteral("crop"), QStringLiteral("editor"));
        cropBtn->applyEditorTheme(m_dark);
        connect(cropBtn, &EditorChipButton::clicked, this, [this] {
            if (m_canvas) m_canvas->cropSelectedImageInteractiveOrRatio(0.0);
        });
        m_layout->addWidget(cropBtn);

        auto* vecBtn = new EditorChipButton(QStringLiteral("Vectorizar figura"), this);
        vecBtn->setIcon(QStringLiteral("vector"), QStringLiteral("editor"));
        vecBtn->applyEditorTheme(m_dark);
        connect(vecBtn, &EditorChipButton::clicked, this, &EditorContextCapsule::vectorizeRequested);
        m_layout->addWidget(vecBtn);

        auto* aiBtn = new EditorChipButton(QStringLiteral("Extraer con IA"), this);
        aiBtn->setIcon(QStringLiteral("sparkle"), QStringLiteral("ui"));
        aiBtn->setChecked(true);
        aiBtn->applyEditorTheme(m_dark);
        connect(aiBtn, &EditorChipButton::clicked, this, &EditorContextCapsule::extractAiRequested);
        m_layout->addWidget(aiBtn);

        auto* opSlider = new EditorSlider(this);
        opSlider->setRange(15.0, 100.0, 5.0);
        opSlider->setValue(m_canvas->selectedImageOpacity() * 100.0);
        opSlider->setFixedWidth(74);
        opSlider->applyEditorTheme(m_dark);
        connect(opSlider, &EditorSlider::valueChanged, this, [this](double pct) {
            if (m_canvas) m_canvas->setSelectedImageOpacity(pct / 100.0);
        });
        m_layout->addWidget(opSlider);

        makeSep();
        addDupAndDel();
        addMoreButton();
    } else if (isShape && isAngle) {
        auto* edit = new EditorLineEdit(this);
        edit->applyEditorTheme(m_dark);
        edit->setFixedSize(72, 26);
        edit->setPlaceholderText(QStringLiteral("Auto"));
        const QString cur = st.label.value_or(QString());
        edit->setText(cur);
        connect(edit, &QLineEdit::editingFinished, this, [this, edit] {
            if (!m_canvas) return;
            SelectionStyle next;
            next.label = edit->text().trimmed();
            m_canvas->setSelectionStyle(next);
        });
        m_layout->addWidget(edit);
        makeSep();
        addDupAndDel();
        addMoreButton();
    } else if (isShape) {
        const bool closed = st.filled.has_value() || st.fillColor.has_value()
                         || (fam != ShapeFamily::None && !(fam == ShapeFamily::Circle && famVal == 1));
        const LinePattern curPat = st.pattern.value_or(LinePattern::Solid);

        auto addPatBtn = [this, curPat](const QString& icon, LinePattern pat, const QString& tip) {
            auto* b = new EditorIconButton(icon, QStringLiteral("editor"), this);
            b->setFixedSize(30, 28);
            b->setIconSizePx(15.0);
            b->setActive(curPat == pat);
            b->setToolTip(tip);
            b->applyEditorTheme(m_dark);
            connect(b, &EditorIconButton::clicked, this, [this, pat] {
                if (!m_canvas) return;
                SelectionStyle s;
                s.pattern = pat;
                m_canvas->setSelectionStyle(s);
                m_lastSignature.clear();
                syncFromCanvas();
            });
            m_layout->addWidget(b);
        };
        addPatBtn(QStringLiteral("line-solid"), LinePattern::Solid, QStringLiteral("Sólida"));
        addPatBtn(QStringLiteral("line-dashed"), LinePattern::Dashed, QStringLiteral("Guiones"));
        addPatBtn(QStringLiteral("line-dotted"), LinePattern::Dotted, QStringLiteral("Puntos"));

        if (closed) {
            makeSep();
            auto* fillBtn = new EditorChipButton(QStringLiteral("Relleno"), this);
            fillBtn->setDotColor(st.fillColor.value_or(m_pal.subtle));
            fillBtn->applyEditorTheme(m_dark);
            connect(fillBtn, &EditorChipButton::clicked, this, [this, fillBtn] {
                const QPoint p = fillBtn->mapTo(parentWidget(), QPoint(0, 0));
                emit fillColorRequested(QRect(p, fillBtn->size()));
            });
            m_layout->addWidget(fillBtn);

            if (fam == ShapeFamily::Triangle || fam == ShapeFamily::Quad ||
                fam == ShapeFamily::Circle || fam == ShapeFamily::Polygon) {
                auto* seg = new EditorSegControl(this);
                if (fam == ShapeFamily::Triangle) {
                    seg->setOptions({QStringLiteral("Libre"), QStringLiteral("Isósc."), QStringLiteral("Rect."), QStringLiteral("Equil.")});
                    seg->setCurrentIndex(std::clamp(famVal, 0, 3));
                } else if (fam == ShapeFamily::Quad) {
                    seg->setOptions({QStringLiteral("Rect."), QStringLiteral("Cuad."), QStringLiteral("Rombo"), QStringLiteral("Paral.")});
                    seg->setCurrentIndex(famVal >= 0 && famVal <= 3 ? famVal : -1);
                } else if (fam == ShapeFamily::Circle) {
                    seg->setOptions({QStringLiteral("Círc."), QStringLiteral("Arco"), QStringLiteral("Sector"), QStringLiteral("Elipse")});
                    seg->setCurrentIndex(std::clamp(famVal, 0, 3));
                } else {
                    seg->setOptions({QStringLiteral("5"), QStringLiteral("6"), QStringLiteral("7"), QStringLiteral("8")});
                    seg->setCurrentIndex(std::clamp(famVal - 5, 0, 3));
                }
                seg->applyEditorTheme(m_dark);
                connect(seg, &EditorSegControl::currentIndexChanged, this, [this, fam](int idx) {
                    if (!m_canvas) return;
                    const int val = (fam == ShapeFamily::Polygon) ? (idx + 5) : idx;
                    m_canvas->applySelectedShapeVariant(fam, val);
                });
                m_layout->addWidget(seg);
            }

            auto* angBtn = new EditorChipButton(QStringLiteral("Ángulos"), this);
            angBtn->applyEditorTheme(m_dark);
            connect(angBtn, &EditorChipButton::clicked, this, [this] {
                if (m_canvas) m_canvas->toggleSelectedShapeAngles();
            });
            m_layout->addWidget(angBtn);
        } else {
            const LineEnding curEnd = st.ending.value_or(LineEnding::None);
            auto addEndBtn = [this, curEnd, curPat](const QString& icon, LineEnding end, bool dim) {
                auto* b = new EditorIconButton(icon, QStringLiteral("editor"), this);
                b->setFixedSize(30, 28);
                b->setIconSizePx(15.0);
                b->setActive(dim ? (curPat == LinePattern::Dimension) : (curEnd == end && curPat != LinePattern::Dimension));
                b->applyEditorTheme(m_dark);
                connect(b, &EditorIconButton::clicked, this, [this, end, dim] {
                    if (!m_canvas) return;
                    SelectionStyle s;
                    if (dim) {
                        s.pattern = LinePattern::Dimension;
                    } else {
                        s.ending = end;
                    }
                    m_canvas->setSelectionStyle(s);
                    m_lastSignature.clear();
                    syncFromCanvas();
                });
                m_layout->addWidget(b);
            };
            addEndBtn(QStringLiteral("end-arrow"), LineEnding::Arrow, false);
            addEndBtn(QStringLiteral("end-double-arrow"), LineEnding::DoubleArrow, false);
            addEndBtn(QStringLiteral("end-dimension"), LineEnding::None, true);
        }

        makeSep();
        addDupAndDel();
        addMoreButton();
    } else {
        if (isStrokes) {
            auto* bBtn = new EditorChipButton(QStringLiteral("Embellecer y ordenar"), this);
            bBtn->setIcon(QStringLiteral("wand"), QStringLiteral("editor"));
            bBtn->applyEditorTheme(m_dark);
            connect(bBtn, &EditorChipButton::clicked, this, [this] {
                if (m_canvas) m_canvas->straightenSelection();
            });
            m_layout->addWidget(bBtn);

            auto* tBtn = new EditorChipButton(QStringLiteral("Pasar a fórmula / texto"), this);
            tBtn->setIcon(QStringLiteral("text"), QStringLiteral("ui"));
            tBtn->applyEditorTheme(m_dark);
            connect(tBtn, &EditorChipButton::clicked, this, [this] {
                if (m_canvas) m_canvas->convertSelectionToText();
            });
            m_layout->addWidget(tBtn);
        }

        if (m_saveElementOpen) {
            auto* nameEdit = new EditorLineEdit(this);
            nameEdit->applyEditorTheme(m_dark);
            nameEdit->setFixedSize(116, 26);
            nameEdit->setPlaceholderText(QStringLiteral("Nombre…"));
            m_layout->addWidget(nameEdit);
            auto* okBtn = new EditorChipButton(QStringLiteral("Guardar"), this);
            okBtn->setChecked(true);
            okBtn->applyEditorTheme(m_dark);
            auto commitSave = [this, nameEdit] {
                if (!m_canvas) return;
                QString name = nameEdit->text().trimmed();
                if (name.isEmpty()) name = QStringLiteral("Elemento");
                const bool saved = m_canvas->saveSelectionAsSticker(name, QStringLiteral("Mis elementos"));
                m_saveElementOpen = false;
                m_lastSignature.clear();
                syncFromCanvas();
                if (saved)
                    emit elementSaved();
            };
            connect(okBtn, &EditorChipButton::clicked, this, commitSave);
            connect(nameEdit, &QLineEdit::returnPressed, this, commitSave);
            m_layout->addWidget(okBtn);
            nameEdit->setFocus();
        } else {
            auto* saveChip = new EditorChipButton(QStringLiteral("Guardar elemento"), this);
            saveChip->setIcon(QStringLiteral("bookmark"), QStringLiteral("editor"));
            saveChip->applyEditorTheme(m_dark);
            connect(saveChip, &EditorChipButton::clicked, this, [this] {
                m_saveElementOpen = true;
                m_lastSignature.clear();
                syncFromCanvas();
            });
            m_layout->addWidget(saveChip);
        }

        makeSep();
        addDupAndDel();
        addMoreButton();
    }

    if (m_fineThicknessOpen && st.width.has_value()) {
        makeSep();
        auto* slider = new EditorSlider(this);
        slider->setRange(0.5, 16.0, 0.5);
        slider->setValue(*st.width);
        slider->setFixedWidth(84);
        slider->applyEditorTheme(m_dark);
        connect(slider, &EditorSlider::valueChanged, this, [this](double w) {
            if (!m_canvas) return;
            SelectionStyle s;
            s.width = w;
            m_canvas->setSelectionStyle(s);
        });
        m_layout->addWidget(slider);
    }

    adjustSize();
    show();
    raise();
    emit layoutChanged();
}

// ---------------- EditorEdgeMarkCapsule ----------------

EditorEdgeMarkCapsule::EditorEdgeMarkCapsule(CanvasWidget* canvas, QWidget* parent)
    : EditorGlassCard(parent),
      m_canvas(canvas) {
    setRole(Role::ContextCapsule);
    setCornerRadius(12.0);
    hide();

    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(6, 5, 6, 5);
    row->setSpacing(3);

    struct MarkDef { const char* icon; EdgeMarkKind kind; const char* tip; };
    const MarkDef defs[8] = {
        {"mark-tick1", EdgeMarkKind::Tick1, "Rayita"},
        {"mark-tick2", EdgeMarkKind::Tick2, "Dos rayitas"},
        {"mark-parallel", EdgeMarkKind::Parallel1, "Paralelas"},
        {"mark-circle", EdgeMarkKind::Bar, "Bolita / Tope"},
        {"mark-dot", EdgeMarkKind::Divider, "Punto divisor"},
        {"mark-saw", EdgeMarkKind::Zigzag, "Serrucho"},
        {"mark-spring", EdgeMarkKind::Spring, "Resorte"},
        {"mark-wall", EdgeMarkKind::Wall, "Pared rugosa"},
    };

    for (const auto& d : defs) {
        auto* btn = new EditorIconButton(QString::fromLatin1(d.icon), QStringLiteral("editor"), this);
        btn->setFixedSize(30, 28);
        btn->setIconSizePx(15.0);
        btn->setToolTip(QString::fromUtf8(d.tip));
        const QString spec = edgeMarkLabel(d.kind);
        connect(btn, &EditorIconButton::clicked, this, [this, spec] {
            if (m_canvas)
                m_canvas->stampCongruenceMarkAt(m_worldPos, spec);
            hide();
            emit visibilityChanged();
        });
        m_markBtns.append(btn);
        row->addWidget(btn);
    }

    auto* div = new QWidget(this);
    div->setFixedSize(1, 18);
    row->addWidget(div);

    m_highlightBtn = new EditorChipButton(QStringLiteral("Resaltar tramo"), this);
    m_highlightBtn->setDotColor(m_activeColor);
    connect(m_highlightBtn, &EditorChipButton::clicked, this, [this] {
        if (m_canvas)
            m_canvas->highlightSegmentAt(m_worldPos, m_activeColor, 0);
        hide();
        emit visibilityChanged();
    });
    row->addWidget(m_highlightBtn);
    adjustSize();
}

void EditorEdgeMarkCapsule::applyEditorTheme(bool dark) {
    EditorGlassCard::applyEditorTheme(dark);
    for (auto* b : m_markBtns)
        b->applyEditorTheme(dark);
    if (m_highlightBtn)
        m_highlightBtn->applyEditorTheme(dark);
}

void EditorEdgeMarkCapsule::showAtWorldPoint(const QPointF& worldPos, const QColor& activeColor) {
    m_worldPos = worldPos;
    if (activeColor.isValid())
        m_activeColor = activeColor;
    if (m_highlightBtn)
        m_highlightBtn->setDotColor(m_activeColor);
    adjustSize();
    show();
    raise();
}

// ---------------- EditorInlineTextBar ----------------

EditorInlineTextBar::EditorInlineTextBar(CanvasWidget* canvas, QWidget* parent)
    : EditorGlassCard(parent),
      m_canvas(canvas) {
    setRole(Role::ContextCapsule);
    setCornerRadius(12.0);
    setFixedHeight(38);
    hide();

    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(6, 4, 6, 4);
    row->setSpacing(4);

    m_fontSeg = new EditorSegControl(this);
    m_fontSeg->setOptions({QStringLiteral("Inter"), QStringLiteral("Manuscrita"), QStringLiteral("Mono")});
    connect(m_fontSeg, &EditorSegControl::currentIndexChanged, this, [this](int idx) {
        m_family = idx;
        if (m_canvas)
            m_canvas->setInlineTextStyle(m_family, m_size, m_bold, m_italic);
    });
    row->addWidget(m_fontSeg);

    m_minusBtn = new EditorIconButton(QStringLiteral("minus"), QStringLiteral("ui"), this);
    m_minusBtn->setFixedSize(26, 26);
    m_minusBtn->setIconSizePx(13.0);
    connect(m_minusBtn, &EditorIconButton::clicked, this, [this] {
        m_size = std::max(10.0, m_size - 2.0);
        if (m_canvas) m_canvas->setInlineTextStyle(m_family, m_size, m_bold, m_italic);
        update();
    });
    row->addWidget(m_minusBtn);

    auto* sizeSpacer = new QWidget(this);
    sizeSpacer->setFixedSize(28, 26);
    row->addWidget(sizeSpacer);

    m_plusBtn = new EditorIconButton(QStringLiteral("plus"), QStringLiteral("ui"), this);
    m_plusBtn->setFixedSize(26, 26);
    m_plusBtn->setIconSizePx(13.0);
    connect(m_plusBtn, &EditorIconButton::clicked, this, [this] {
        m_size = std::min(72.0, m_size + 2.0);
        if (m_canvas) m_canvas->setInlineTextStyle(m_family, m_size, m_bold, m_italic);
        update();
    });
    row->addWidget(m_plusBtn);

    m_boldBtn = new EditorIconButton(QStringLiteral("bold"), QStringLiteral("editor"), this);
    m_boldBtn->setFixedSize(26, 26);
    m_boldBtn->setIconSizePx(14.0);
    connect(m_boldBtn, &EditorIconButton::clicked, this, [this] {
        m_bold = !m_bold;
        m_boldBtn->setActive(m_bold);
        if (m_canvas) m_canvas->setInlineTextStyle(m_family, m_size, m_bold, m_italic);
    });
    row->addWidget(m_boldBtn);

    m_italicBtn = new EditorIconButton(QStringLiteral("italic"), QStringLiteral("editor"), this);
    m_italicBtn->setFixedSize(26, 26);
    m_italicBtn->setIconSizePx(14.0);
    connect(m_italicBtn, &EditorIconButton::clicked, this, [this] {
        m_italic = !m_italic;
        m_italicBtn->setActive(m_italic);
        if (m_canvas) m_canvas->setInlineTextStyle(m_family, m_size, m_bold, m_italic);
    });
    row->addWidget(m_italicBtn);

    adjustSize();
}

void EditorInlineTextBar::applyEditorTheme(bool dark) {
    EditorGlassCard::applyEditorTheme(dark);
    m_fontSeg->applyEditorTheme(dark);
    m_minusBtn->applyEditorTheme(dark);
    m_plusBtn->applyEditorTheme(dark);
    m_boldBtn->applyEditorTheme(dark);
    m_italicBtn->applyEditorTheme(dark);
}

void EditorInlineTextBar::syncFromCanvas() {
    if (!m_canvas)
        return;
    const InlineTextState st = m_canvas->inlineTextState();
    m_family = st.fontFamilyIndex;
    m_size = st.fontSize;
    m_bold = st.bold;
    m_italic = st.italic;
    m_fontSeg->setCurrentIndex(m_family);
    m_boldBtn->setActive(m_bold);
    m_italicBtn->setActive(m_italic);
    update();
}

void EditorInlineTextBar::paintEvent(QPaintEvent* event) {
    EditorGlassCard::paintEvent(event);
    QPainter p(this);
    p.setFont(editorFont(11.5, QFont::DemiBold));
    p.setPen(m_pal.text);
    const QRect r(m_minusBtn->x() + m_minusBtn->width(), 0,
                  std::max(24, m_plusBtn->x() - (m_minusBtn->x() + m_minusBtn->width())), height());
    p.drawText(r, Qt::AlignCenter, QString::number(qRound(m_size)));
}

} // namespace finily
