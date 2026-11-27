#include "ui/popovers/StickersPopover.h"

#include "ui/CanvasWidget.h"
#include "ui/ToolbarIcons.h"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QScrollArea>
#include <QSettings>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace finily {
namespace {

QString normalizeSearchText(const QString& input) {
    const QString decomposed = input.normalized(QString::NormalizationForm_D).toLower();
    QString out;
    out.reserve(decomposed.size());
    for (const QChar& ch : decomposed) {
        if (ch.category() != QChar::Mark_NonSpacing)
            out.append(ch);
    }
    return out;
}

} // namespace

StickersPopover::StickersPopover(CanvasWidget* canvas, QWidget* parent)
    : GlassCapsule(parent)
    , m_canvas(canvas) {
    setObjectName(QStringLiteral("finilyPopover"));
    setCapsuleRadius(16);
    setFixedWidth(404);
    setMaximumHeight(524);
    hide();

    QSettings settings(QStringLiteral("Finily"), QStringLiteral("Finily"));
    const int savedStyle = std::clamp(settings.value(QStringLiteral("stickers/style"), 0).toInt(), 0, 2);
    m_stickerStyle = static_cast<StickerStyle>(savedStyle);

    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(14, 14, 14, 12);
    column->setSpacing(9);

    // Buscador rápido
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setObjectName(QStringLiteral("finilyStickerSearch"));
    m_searchEdit->setPlaceholderText(QStringLiteral("Buscar teorema, fórmula o notable..."));
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setFixedHeight(34);
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString&) {
        rebuildGrid();
    });
    connect(m_searchEdit, &QLineEdit::returnPressed, this, [this] {
        if (!m_canvas || !m_gridHost)
            return;
        const QString firstId = m_gridHost->property("firstStickerId").toString();
        if (firstId.isEmpty())
            return;
        m_canvas->insertStickerTemplate(firstId, m_stickerStyle);
        emit closeRequested();
        emit refreshToolbarRequested();
    });
    column->addWidget(m_searchEdit);

    // Control segmentado de estilo (Limpio / Ficha / Solo dibujo)
    auto* styleRow = new QHBoxLayout();
    styleRow->setSpacing(8);
    auto* styleLabel = new QLabel(QStringLiteral("Estilo"), this);
    styleLabel->setObjectName(QStringLiteral("finilyShortcut"));
    styleRow->addWidget(styleLabel);

    auto* segFrame = new QFrame(this);
    segFrame->setObjectName(QStringLiteral("finilyStickerSeg"));
    auto* segLayout = new QHBoxLayout(segFrame);
    segLayout->setContentsMargins(3, 3, 3, 3);
    segLayout->setSpacing(3);

    const QVector<QPair<QString, StickerStyle>> styles = {
        {QStringLiteral("Limpio"), StickerStyle::CleanWithTitle},
        {QStringLiteral("Ficha"), StickerStyle::Card},
        {QStringLiteral("Solo dibujo"), StickerStyle::DiagramOnly},
    };
    for (int i = 0; i < styles.size(); ++i) {
        auto* btn = new QToolButton(segFrame);
        btn->setObjectName(QStringLiteral("finilyStickerSegBtn"));
        btn->setText(styles.at(i).first);
        btn->setCheckable(true);
        btn->setChecked(styles.at(i).second == m_stickerStyle);
        btn->setFixedHeight(24);
        btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        polishButton(btn);
        const StickerStyle st = styles.at(i).second;
        connect(btn, &QToolButton::clicked, this, [this, st] {
            m_stickerStyle = st;
            QSettings s(QStringLiteral("Finily"), QStringLiteral("Finily"));
            s.setValue(QStringLiteral("stickers/style"), int(st));
            for (int k = 0; k < m_styleBtns.size(); ++k)
                m_styleBtns.at(k)->setChecked(k == int(st));
            rebuildGrid();
        });
        m_styleBtns.append(btn);
        segLayout->addWidget(btn);
    }
    styleRow->addWidget(segFrame, 1);
    column->addLayout(styleRow);

    // Categorías dinámicas desde StickerLibrary
    auto* catGrid = new QGridLayout();
    catGrid->setContentsMargins(0, 0, 0, 0);
    catGrid->setHorizontalSpacing(5);
    catGrid->setVerticalSpacing(5);
    const QStringList categories = stickerCategories();
    for (int i = 0; i < categories.size(); ++i) {
        const QString cat = categories.at(i);
        auto* btn = new QToolButton(this);
        btn->setObjectName(QStringLiteral("finilyStickerChip"));
        btn->setText(cat);
        btn->setCheckable(true);
        btn->setChecked(cat == m_activeCategory);
        btn->setFixedHeight(26);
        btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        polishButton(btn);
        connect(btn, &QToolButton::clicked, this, [this, cat] {
            m_activeCategory = cat;
            for (QToolButton* b : m_catBtns)
                b->setChecked(b->text() == cat);
            rebuildGrid();
        });
        m_catBtns.append(btn);
        catGrid->addWidget(btn, i / 4, i % 4);
    }
    column->addLayout(catGrid);

    // Área desplazable de tarjetas con scrollbar estilizada
    auto* scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("finilyStickerScroll"));
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setFixedHeight(310);
    scroll->viewport()->setAutoFillBackground(false);

    m_gridHost = new QWidget(scroll);
    m_gridHost->setAttribute(Qt::WA_TranslucentBackground);
    m_grid = new QGridLayout(m_gridHost);
    m_grid->setContentsMargins(0, 2, 6, 4);
    m_grid->setHorizontalSpacing(10);
    m_grid->setVerticalSpacing(10);
    m_grid->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    scroll->setWidget(m_gridHost);
    column->addWidget(scroll);

    auto* divider = new QFrame(this);
    divider->setObjectName(QStringLiteral("finilyDivider"));
    column->addWidget(divider);

    // Botón inferior bien delimitado para guardar la selección actual
    auto* saveSelBtn = new QToolButton(this);
    saveSelBtn->setObjectName(QStringLiteral("finilyStickerSaveBtn"));
    saveSelBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    saveSelBtn->setIconSize(QSize(14, 14));
    saveSelBtn->setText(QStringLiteral("Guardar selección actual como sticker"));
    saveSelBtn->setIcon(glyphIcon(QStringLiteral("plus"), m_theme.uiText));
    saveSelBtn->setFixedHeight(32);
    saveSelBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    polishButton(saveSelBtn);
    m_iconSpecs.append(qMakePair(saveSelBtn, QStringLiteral("plus")));
    connect(saveSelBtn, &QToolButton::clicked, this, [this] {
        if (!m_canvas)
            return;
        if (!m_canvas->hasSelection()) {
            emit toastRequested(QStringLiteral("Selecciona un dibujo o figura con el Lazo primero"));
            return;
        }
        bool ok = false;
        const QString name = QInputDialog::getText(
            window(), tr("Guardar como sticker"),
            tr("Nombre corto para el sticker:"),
            QLineEdit::Normal, QString(), &ok).trimmed();
        if (!ok || name.isEmpty())
            return;
        if (m_canvas->saveSelectionAsSticker(name)) {
            m_activeCategory = QStringLiteral("Mis ★");
            for (QToolButton* b : m_catBtns)
                b->setChecked(b->text() == m_activeCategory);
            rebuildGrid();
        }
    });
    column->addWidget(saveSelBtn);
}

StickersPopover::~StickersPopover() = default;

void StickersPopover::onOpened() {
    rebuildGrid();
    if (m_searchEdit) {
        m_searchEdit->setFocus();
        m_searchEdit->selectAll();
    }
}

void StickersPopover::applyTheme(const ThemeConfig& theme, const QString& qss) {
    m_theme = theme;
    GlassCapsule::applyTheme(theme, qss);
    for (const auto& spec : m_iconSpecs) {
        if (spec.first)
            spec.first->setIcon(glyphIcon(spec.second, theme.uiText));
    }
    rebuildGrid();
}

void StickersPopover::rebuildGrid() {
    if (!m_grid || !m_gridHost)
        return;

    while (QLayoutItem* item = m_grid->takeAt(0)) {
        if (QWidget* w = item->widget())
            w->deleteLater();
        delete item;
    }

    const QString query = m_searchEdit
        ? normalizeSearchText(m_searchEdit->text().trimmed())
        : QString();
    const QVector<StickerTemplate> templates = allStickerTemplates();

    QVector<StickerTemplate> matched;
    matched.reserve(templates.size());
    for (const auto& tpl : templates) {
        if (m_activeCategory != QStringLiteral("Todos") && tpl.category != m_activeCategory)
            continue;
        if (!query.isEmpty()) {
            bool hit = normalizeSearchText(tpl.title).contains(query)
                || normalizeSearchText(tpl.category).contains(query)
                || normalizeSearchText(tpl.formula).contains(query);
            if (!hit) {
                for (const QString& kw : tpl.keywords) {
                    if (normalizeSearchText(kw).contains(query)) {
                        hit = true;
                        break;
                    }
                }
            }
            if (!hit)
                continue;
        }
        matched.append(tpl);
    }

    const QString firstId = matched.isEmpty() ? QString() : matched.first().id;
    m_gridHost->setProperty("firstStickerId", firstId);

    if (matched.isEmpty()) {
        auto* empty = new QLabel(m_gridHost);
        empty->setWordWrap(true);
        empty->setAlignment(Qt::AlignCenter);
        empty->setObjectName(QStringLiteral("finilyShortcut"));
        empty->setMinimumHeight(110);
        const bool emptyCustom = (m_activeCategory == QStringLiteral("Mis ★") && query.isEmpty());
        empty->setText(emptyCustom
                           ? QStringLiteral("Selecciona un dibujo con el Lazo y guárdalo como sticker ★")
                           : QStringLiteral("Sin resultados"));
        m_grid->addWidget(empty, 0, 0, 1, 2);
        return;
    }

    for (int i = 0; i < matched.size(); ++i) {
        const StickerTemplate& tpl = matched.at(i);
        auto* card = new QToolButton(m_gridHost);
        card->setObjectName(QStringLiteral("finilyStickerCard"));
        card->setFixedSize(176, 126);
        const QString tip = tpl.formula.isEmpty()
            ? tpl.title
            : (tpl.title + QStringLiteral("\n") + tpl.formula);
        card->setToolTip(tip);
        polishButton(card);

        auto* cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(8, 8, 8, 7);
        cardLayout->setSpacing(3);

        // Miniatura siempre en modo DiagramOnly para máxima nitidez y sin duplicar el título
        auto* preview = new QLabel(card);
        preview->setAlignment(Qt::AlignCenter);
        preview->setFixedHeight(74);
        preview->setAttribute(Qt::WA_TransparentForMouseEvents);
        const QPixmap thumb = renderStickerThumbnail(
            tpl, QSize(160, 74), m_theme, StickerStyle::DiagramOnly);
        preview->setPixmap(thumb);
        cardLayout->addWidget(preview);

        auto* titleLbl = new QLabel(tpl.title, card);
        titleLbl->setObjectName(QStringLiteral("finilyStickerCardTitle"));
        titleLbl->setAlignment(Qt::AlignCenter);
        titleLbl->setAttribute(Qt::WA_TransparentForMouseEvents);
        cardLayout->addWidget(titleLbl);

        if (!tpl.formula.isEmpty()) {
            auto* formulaLbl = new QLabel(tpl.formula, card);
            formulaLbl->setObjectName(QStringLiteral("finilyStickerCardFormula"));
            formulaLbl->setAlignment(Qt::AlignCenter);
            formulaLbl->setAttribute(Qt::WA_TransparentForMouseEvents);
            cardLayout->addWidget(formulaLbl);
        }

        const QString id = tpl.id;
        connect(card, &QToolButton::clicked, this, [this, id] {
            if (m_canvas)
                m_canvas->insertStickerTemplate(id, m_stickerStyle);
            emit closeRequested();
            emit refreshToolbarRequested();
        });

        if (tpl.isCustom) {
            card->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(card, &QToolButton::customContextMenuRequested, this,
                    [this, card, tpl](const QPoint& pos) {
                        QMenu menu(this);
                        menu.addAction(tr("Duplicar sticker"), this, [this, tpl] {
                            if (saveCustomSticker(tpl.title + tr(" (copia)"), tpl.category,
                                                  tpl.strokes, tpl.shapes, tpl.texts)) {
                                emit toastRequested(QStringLiteral("Sticker duplicado"));
                            }
                            rebuildGrid();
                        });
                        menu.addAction(tr("Eliminar sticker"), this, [this, id = tpl.id] {
                            if (deleteCustomSticker(id))
                                emit toastRequested(QStringLiteral("Sticker eliminado"));
                            rebuildGrid();
                        });
                        menu.exec(card->mapToGlobal(pos));
                    });

            auto* delBtn = new QToolButton(card);
            delBtn->setText(QStringLiteral("×"));
            delBtn->setToolTip(tr("Eliminar sticker personalizado"));
            delBtn->setFixedSize(18, 18);
            delBtn->move(card->width() - 23, 5);
            delBtn->setCursor(Qt::PointingHandCursor);
            delBtn->setFocusPolicy(Qt::NoFocus);
            delBtn->setStyleSheet(QStringLiteral(
                "QToolButton { background: rgba(239, 68, 68, 180); color: #ffffff; border: none;"
                " border-radius: 9px; font-weight: 700; font-size: 11px; padding: 0px; }"
                "QToolButton:hover { background: rgba(220, 38, 38, 235); }"));
            connect(delBtn, &QToolButton::clicked, this, [this, id] {
                if (deleteCustomSticker(id))
                    emit toastRequested(QStringLiteral("Sticker eliminado"));
                rebuildGrid();
            });
            delBtn->raise();
        }

        m_grid->addWidget(card, i / 2, i % 2);
    }
}

} // namespace finily
