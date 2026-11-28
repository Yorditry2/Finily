#include "ui/FloatingToolbar.h"

#include "core/Utils.h"
#include "engine/LibraryStore.h"
#include "ui/CanvasWidget.h"
#include "ui/editor/EditorChrome.h"
#include "ui/editor/EditorColorPopover.h"
#include "ui/editor/EditorContextCapsules.h"
#include "ui/editor/EditorModals.h"
#include "ui/editor/EditorSidePanels.h"
#include "ui/editor/EditorToolPanels.h"
#include "ui/home/HomeTheme.h"
#include "ui/home/NewBoardSheet.h"
#include "ui/home/NewNotebookSheet.h"
#include "ui/home/SettingsView.h"

#include <QFileInfo>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QTabletEvent>

#include <algorithm>

namespace finily {

FloatingToolbar::FloatingToolbar(CanvasWidget* canvas, QWidget* parent)
    : QWidget(parent),
      m_canvas(canvas) {
    setObjectName(QStringLiteral("finilyToolbarOverlay"));
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_StyledBackground, false);
    setAutoFillBackground(false);
    setMouseTracking(true);

    m_dockMode = ToolbarDockMode(std::clamp(readAppSetting(QStringLiteral("toolbar/dockMode"), 0).toInt(), 0, 2));
    m_pageNavDir = PageNavDirection(std::clamp(readAppSetting(QStringLiteral("pages/navDir"), 0).toInt(), 0, 1));
    m_elementsRight = readAppSetting(QStringLiteral("panels/elementsRight"), false).toBool();

    initPresets();
    loadPresets();

    if (m_canvas) {
        m_canvas->installEventFilter(this);
        connect(m_canvas, &CanvasWidget::stateChanged, this, &FloatingToolbar::refreshFromCanvas);
        connect(m_canvas, &CanvasWidget::pageStructureChanged, this, [this] {
            if (m_pagesPanel && m_pagesPanel->isVisible())
                m_pagesPanel->syncFromCanvas();
            refreshFromCanvas();
        });
        connect(m_canvas, &CanvasWidget::viewportChanged, this, [this](double) {
            if (m_topBar)
                m_topBar->syncFromCanvas(m_canvas);
            positionContextCapsule();
            updateOverlayMask();
        });
        connect(m_canvas, &CanvasWidget::toastRequested, this, [this](const QString& msg) {
            showToast(msg, false);
        });
        connect(m_canvas, &CanvasWidget::selectionMenuRequested, this, [this](const QPoint& globalPos) {
            if (!m_canvas || !m_canvas->hasSelection() || !m_dropdownMenu)
                return;
            const QPoint local = mapFromGlobal(globalPos);
            QVector<EditorDropdownItem> items;
            items.append({1, tr("Duplicar"), QStringLiteral("Ctrl+D"), QStringLiteral("copy"), QStringLiteral("ui"), false, false, false});
            items.append({2, tr("Copiar"), QStringLiteral("Ctrl+C"), QStringLiteral("copy"), QStringLiteral("ui"), false, false, false});
            items.append({3, tr("Agrupar"), {}, QStringLiteral("shapes"), QStringLiteral("ui"), false, false, false});
            items.append({4, tr("Desagrupar"), {}, QStringLiteral("shapes"), QStringLiteral("ui"), false, false, false});
            items.append({5, tr("Traer al frente"), {}, QStringLiteral("move-up"), QStringLiteral("editor"), false, false, false});
            items.append({6, tr("Enviar atrás"), {}, QStringLiteral("move-down"), QStringLiteral("editor"), false, false, false});
            items.append({7, tr("Eliminar"), QStringLiteral("Supr"), QStringLiteral("trash"), QStringLiteral("ui"), false, true, false});
            m_dropdownMenu->openMenu(items, local + QPoint(4, 4), size(), [this](int id) {
                updateOverlayMask();
                if (!m_canvas)
                    return;
                switch (id) {
                case 1: m_canvas->duplicateSelection(); break;
                case 2: m_canvas->copySelection(); break;
                case 3: m_canvas->groupSelection(); break;
                case 4: m_canvas->ungroupSelection(); break;
                case 5: m_canvas->bringSelectionToFront(); break;
                case 6: m_canvas->sendSelectionToBack(); break;
                case 7: m_canvas->deleteSelection(); break;
                default: break;
                }
                refreshFromCanvas();
            });
            updateOverlayMask();
        });
        connect(m_canvas, &CanvasWidget::edgeMarkMenuRequested, this, &FloatingToolbar::showEdgeMarkCapsule);
        connect(m_canvas, &CanvasWidget::inlineTextEditorChanged, this, [this](bool, const QRect&) {
            if (m_contextCapsule) {
                m_contextCapsule->syncFromCanvas();
                positionContextCapsule();
            }
            updateOverlayMask();
        });
    }

    auto dropdownFn = [this](QWidget* anchor, const QVector<EditorDropdownItem>& items, std::function<void(int)> cb) {
        openDropdown(anchor, items, std::move(cb));
    };

    m_topBar = new EditorTopBar(this);
    m_topBar->setDockMode(m_dockMode);
    connect(m_topBar, &EditorTopBar::homeRequested, this, &FloatingToolbar::homeRequested);
    connect(m_topBar, &EditorTopBar::tabSelected, this, &FloatingToolbar::switchTabRequested);
    connect(m_topBar, &EditorTopBar::closeTabRequested, this, &FloatingToolbar::closeTabRequested);
    connect(m_topBar, &EditorTopBar::addTabRequested, this, &FloatingToolbar::showNewTabMenu);
    connect(m_topBar, &EditorTopBar::tabRenamed, this, [this](int idx, const QString& title) {
        emit switchTabRequested(idx);
        emit renameRequested(title);
    });
    connect(m_topBar, &EditorTopBar::tabMenuRequested, this, &FloatingToolbar::showTabMenu);
    connect(m_topBar, &EditorTopBar::fullscreenRequested, this, &FloatingToolbar::fullscreenToggled);
    connect(m_topBar, &EditorTopBar::undoRequested, this, [this] {
        if (m_canvas) { m_canvas->undo(); refreshFromCanvas(); }
    });
    connect(m_topBar, &EditorTopBar::redoRequested, this, [this] {
        if (m_canvas) { m_canvas->redo(); refreshFromCanvas(); }
    });
    connect(m_topBar, &EditorTopBar::toolSelected, this, &FloatingToolbar::selectTool);
    connect(m_topBar, &EditorTopBar::toolPanelToggled, this, &FloatingToolbar::openToolPanel);
    connect(m_topBar, &EditorTopBar::elementsPanelToggled, this, &FloatingToolbar::openStickersPopover);
    connect(m_topBar, &EditorTopBar::widthSlotSelected, this, [this](ToolType t, int slot) {
        if (m_canvas) m_canvas->setToolWidthSlot(t, slot);
    });
    connect(m_topBar, &EditorTopBar::toolWidthChanged, this, [this](ToolType t, double mm) {
        if (!m_canvas) return;
        m_canvas->setToolFineWidth(t, mm);
        if (m_penPanel && m_penPanel->isVisible()) m_penPanel->syncFromCanvas();
        if (m_hlPanel && m_hlPanel->isVisible()) m_hlPanel->syncFromCanvas();
    });
    connect(m_topBar, &EditorTopBar::fineWidthRequested, this, &FloatingToolbar::openFineWidthPopover);
    connect(m_topBar, &EditorTopBar::colorSwatchSelected, this, [this](ToolType t, int idx) {
        if (m_canvas) m_canvas->setToolColorIndex(t, idx);
    });
    connect(m_topBar, &EditorTopBar::customColorRequested, this, [this](ToolType t, const QRect& r) {
        openColorPopover(t, r, false);
    });
    connect(m_topBar, &EditorTopBar::pagesRequested, this, [this] {
        if (!m_pagesPanel) return;
        closeTransientPopovers();
        if (m_pagesPanel->isVisible())
            m_pagesPanel->hide();
        else {
            m_pagesPanel->syncFromCanvas();
            m_pagesPanel->show();
            m_pagesPanel->raise();
        }
        positionAll();
    });
    connect(m_topBar, &EditorTopBar::paperRequested, this, &FloatingToolbar::openPaperPanel);
    connect(m_topBar, &EditorTopBar::zoomOutRequested, this, [this] {
        if (m_canvas) m_canvas->zoomOut();
    });
    connect(m_topBar, &EditorTopBar::zoomResetRequested, this, [this] {
        if (m_canvas) m_canvas->resetViewport();
    });
    connect(m_topBar, &EditorTopBar::zoomInRequested, this, [this] {
        if (m_canvas) m_canvas->zoomIn();
    });
    connect(m_topBar, &EditorTopBar::exportRequested, this, &FloatingToolbar::showExportMenu);
    connect(m_topBar, &EditorTopBar::gearRequested, this, [this] {
        openSettingsModal(QStringLiteral("escritura"));
    });

    m_detachedDock = new EditorDetachedDock(this);
    connect(m_detachedDock, &EditorDetachedDock::toolSelected, this, &FloatingToolbar::selectTool);
    connect(m_detachedDock, &EditorDetachedDock::toolPanelToggled, this, &FloatingToolbar::openToolPanel);
    connect(m_detachedDock, &EditorDetachedDock::elementsPanelToggled, this, &FloatingToolbar::openStickersPopover);
    connect(m_detachedDock, &EditorDetachedDock::widthSlotSelected, this, [this](ToolType t, int slot) {
        if (m_canvas) m_canvas->setToolWidthSlot(t, slot);
    });
    connect(m_detachedDock, &EditorDetachedDock::fineWidthRequested, this, &FloatingToolbar::openFineWidthPopover);
    connect(m_detachedDock, &EditorDetachedDock::colorSwatchSelected, this, [this](ToolType t, int idx) {
        if (m_canvas) m_canvas->setToolColorIndex(t, idx);
    });
    connect(m_detachedDock, &EditorDetachedDock::customColorRequested, this, [this](ToolType t, const QRect& r) {
        openColorPopover(t, r, false);
    });

    m_pageCapsule = new EditorPageCapsule(this);
    connect(m_pageCapsule, &EditorPageCapsule::prevPageRequested, this, [this] {
        if (m_canvas) m_canvas->prevPage();
    });
    connect(m_pageCapsule, &EditorPageCapsule::nextPageRequested, this, [this] {
        if (m_canvas) m_canvas->nextPage();
    });
    connect(m_pageCapsule, &EditorPageCapsule::addPageRequested, this, [this] {
        if (m_canvas) m_canvas->addPage();
    });
    connect(m_pageCapsule, &EditorPageCapsule::pagesPanelRequested, this, [this] {
        if (!m_pagesPanel) return;
        closeTransientPopovers();
        if (m_pagesPanel->isVisible())
            m_pagesPanel->hide();
        else {
            m_pagesPanel->syncFromCanvas();
            m_pagesPanel->show();
            m_pagesPanel->raise();
        }
        positionAll();
    });

    m_restoreBtn = new EditorIconButton(QStringLiteral("fit"), QStringLiteral("ui"), this);
    m_restoreBtn->setFixedSize(34, 34);
    m_restoreBtn->setToolTip(QStringLiteral("Mostrar barra (Tab)"));
    m_restoreBtn->hide();
    connect(m_restoreBtn, &EditorIconButton::clicked, this, &FloatingToolbar::toggleCleanUi);

    m_colorPopover = new EditorColorPopover(this);
    connect(m_colorPopover, &EditorColorPopover::colorPicked, this, [this](const QColor& c) {
        if (!m_canvas) return;
        if (m_sheetColorMode) {
            m_canvas->setCustomBackground(c);
        } else if (m_selectionColorMode) {
            SelectionStyle s;
            s.color = c;
            m_canvas->setSelectionStyle(s);
        } else if (!m_shapeFillMode) {
            m_canvas->setCustomSwatchColor(m_colorTargetTool, c);
        }
    });
    connect(m_colorPopover, &EditorColorPopover::colorCommitted, this, [this](const QColor& c) {
        if (!m_canvas) return;
        if (m_sheetColorMode)
            m_canvas->setCustomBackground(c);
        else if (m_selectionColorMode) {
            SelectionStyle s;
            s.color = c;
            m_canvas->setSelectionStyle(s);
        } else if (m_shapeFillMode)
            m_canvas->setSelectedShapeFillColor(c);
    });

    m_fineWidthPopover = new EditorFineWidthPopover(this);
    connect(m_fineWidthPopover, &EditorFineWidthPopover::widthChanged, this, [this](double mm) {
        if (m_canvas) m_canvas->setToolFineWidth(m_widthTargetTool, mm);
    });

    auto onPanelResized = [this](QWidget* panel) {
        if (m_openPopover == panel && panel && panel->isVisible())
            positionFloatingCard(panel, m_openPopoverAnchor, panel == m_paperPanel);
        updateOverlayMask();
    };

    m_penPanel = new EditorPenPanel(m_canvas, this);
    connect(m_penPanel, &EditorPenPanel::libraryRequested, this, &FloatingToolbar::openLibraryModal);
    connect(m_penPanel, &EditorPenPanel::customColorRequested, this, [this](ToolType t, const QRect& r) {
        openColorPopover(t, r, false);
    });
    connect(m_penPanel, &EditorPenPanel::panelResized, this, [this, onPanelResized] { onPanelResized(m_penPanel); });

    m_hlPanel = new EditorHighlighterPanel(m_canvas, this);
    connect(m_hlPanel, &EditorHighlighterPanel::customColorRequested, this, [this](ToolType t, const QRect& r) {
        openColorPopover(t, r, false);
    });
    connect(m_hlPanel, &EditorHighlighterPanel::panelResized, this, [this, onPanelResized] { onPanelResized(m_hlPanel); });

    m_eraserPanel = new EditorEraserPanel(m_canvas, this);
    connect(m_eraserPanel, &EditorEraserPanel::clearPageRequested, this, [this] {
        if (!m_canvas) return;
        m_canvas->clearBoard();
        closeTransientPopovers();
        showToast(QStringLiteral("Se borraron las anotaciones de la página"), true);
    });
    connect(m_eraserPanel, &EditorEraserPanel::panelResized, this, [this, onPanelResized] { onPanelResized(m_eraserPanel); });

    m_fillPanel = new EditorFillPanel(m_canvas, this);
    connect(m_fillPanel, &EditorFillPanel::fillSubModeChanged, this, [this](bool segmentMode) {
        if (m_canvas) m_canvas->setFillSegmentMode(segmentMode);
    });
    connect(m_fillPanel, &EditorFillPanel::customColorRequested, this, [this](ToolType t, const QRect& r) {
        openColorPopover(t, r, false);
    });
    m_lassoPanel = new EditorLassoPanel(m_canvas, this);

    m_shapesPanel = new EditorShapesPanel(m_canvas, this);
    connect(m_shapesPanel, &EditorShapesPanel::libraryRequested, this, &FloatingToolbar::openLibraryModal);
    connect(m_shapesPanel, &EditorShapesPanel::panelResized, this, [this, onPanelResized] { onPanelResized(m_shapesPanel); });
    connect(m_shapesPanel, &EditorShapesPanel::bubbleVisibilityChanged, this, [this] { updateOverlayMask(); });

    m_paperPanel = new EditorPaperPanel(m_canvas, this);
    connect(m_paperPanel, &EditorPaperPanel::customSheetColorRequested, this, [this](const QRect& r) {
        openColorPopover(ToolType::Pen, r, false, true);
    });
    connect(m_paperPanel, &EditorPaperPanel::panelResized, this, [this, onPanelResized] { onPanelResized(m_paperPanel); });

    m_elementsPanel = new EditorElementsPanel(m_canvas, this);
    m_elementsPanel->setDropdownOpener(dropdownFn);
    connect(m_elementsPanel, &EditorElementsPanel::closeRequested, this, [this] {
        m_elementsPanel->hide();
        m_topBar->setElementsPanelOpen(false);
        m_detachedDock->setElementsPanelOpen(false);
        positionAll();
    });
    connect(m_elementsPanel, &EditorElementsPanel::insertImageRequested, this, &FloatingToolbar::insertImageDialog);
    connect(m_elementsPanel, &EditorElementsPanel::importPdfRequested, this, &FloatingToolbar::importPdfDialog);
    connect(m_elementsPanel, &EditorElementsPanel::libraryRequested, this, &FloatingToolbar::openLibraryModal);

    m_pagesPanel = new EditorPagesPanel(m_canvas, this);
    m_pagesPanel->setDropdownOpener(dropdownFn);
    connect(m_pagesPanel, &EditorPagesPanel::closeRequested, this, [this] {
        m_pagesPanel->hide();
        positionAll();
    });
    connect(m_pagesPanel, &EditorPagesPanel::pageDeletedToastRequested, this, [this] {
        showToast(QStringLiteral("Página eliminada"), true);
    });

    m_contextCapsule = new EditorContextCapsule(m_canvas, this);
    m_contextCapsule->setDropdownOpener(dropdownFn);
    connect(m_contextCapsule, &EditorContextCapsule::vectorizeRequested,
            this, &FloatingToolbar::vectorizeSelectedImageRequested);
    connect(m_contextCapsule, &EditorContextCapsule::extractAiRequested,
            this, &FloatingToolbar::extractSelectedImageRequested);
    connect(m_contextCapsule, &EditorContextCapsule::fillColorRequested, this, [this](const QRect& r) {
        openColorPopover(ToolType::Fill, r, true);
    });
    connect(m_contextCapsule, &EditorContextCapsule::selectionColorRequested, this, [this](const QRect& r) {
        openColorPopover(ToolType::Pen, r, false, false, true);
    });
    connect(m_contextCapsule, &EditorContextCapsule::elementSaved, this, [this] {
        if (m_elementsPanel && m_elementsPanel->isVisible())
            m_elementsPanel->syncFromCanvas();
    });
    connect(m_contextCapsule, &EditorContextCapsule::layoutChanged, this, [this] {
        positionContextCapsule();
        updateOverlayMask();
    });
    connect(m_contextCapsule, &EditorContextCapsule::moreMenuVisibilityChanged, this, [this] {
        updateOverlayMask();
    });
    connect(m_contextCapsule, &EditorContextCapsule::fontPopoverVisibilityChanged, this, [this] {
        updateOverlayMask();
    });
    connect(m_contextCapsule, &EditorContextCapsule::transientPopoverAboutToOpen, this, &FloatingToolbar::closeTransientPopovers);

    m_edgeMarkCapsule = new EditorEdgeMarkCapsule(m_canvas, this);
    connect(m_edgeMarkCapsule, &EditorEdgeMarkCapsule::visibilityChanged, this, [this] { updateOverlayMask(); });

    m_dropdownMenu = new EditorDropdownMenu(this);
    connect(m_dropdownMenu, &EditorDropdownMenu::menuClosed, this, [this] { updateOverlayMask(); });

    m_toast = new EditorToast(this);
    connect(m_toast, &EditorToast::undoRequested, this, [this] {
        if (m_canvas) { m_canvas->undo(); refreshFromCanvas(); }
    });
    connect(m_toast, &EditorToast::visibilityChanged, this, [this] { updateOverlayMask(); });

    m_confirmCard = new EditorConfirmCard(this);
    connect(m_confirmCard, &EditorConfirmCard::visibilityChanged, this, [this] { updateOverlayMask(); });

    m_settingsModal = new EditorSettingsModal(this);
    m_settingsModal->setDropdownOpener(dropdownFn);
    connect(m_settingsModal, &EditorSettingsModal::dockModeChanged, this, &FloatingToolbar::setDockMode);
    connect(m_settingsModal, &EditorSettingsModal::elementsSideChanged, this, [this](bool rightSide) {
        m_elementsRight = rightSide;
        positionAll();
    });
    connect(m_settingsModal, &EditorSettingsModal::defaultStickerStyleChanged, this, [this](int style) {
        if (m_elementsPanel) m_elementsPanel->setStickerStyle(StickerStyle(std::clamp(style, 0, 2)));
    });
    connect(m_settingsModal, &EditorSettingsModal::pageNavDirectionChanged, this, [this](int dir) {
        setPageNavDirection(PageNavDirection(dir));
    });
    connect(m_settingsModal, &EditorSettingsModal::padActionsChanged, this, &FloatingToolbar::padActionsChanged);
    connect(m_settingsModal, &EditorSettingsModal::closed, this, [this] {
        if (m_dropdownMenu && m_dropdownMenu->isVisible())
            m_dropdownMenu->hide();
        updateOverlayMask();
    });

    m_libraryModal = new EditorLibraryModal(this);
    m_libraryModal->setPresets(m_presets);
    connect(m_libraryModal, &EditorLibraryModal::savePresetRequested, this, &FloatingToolbar::saveCurrentToPreset);
    connect(m_libraryModal, &EditorLibraryModal::selectPresetRequested, this, &FloatingToolbar::selectPreset);
    connect(m_libraryModal, &EditorLibraryModal::penTipSelected, this, [this](int tip) {
        if (m_canvas) { m_canvas->setTool(ToolType::Pen); m_canvas->setPenTip(tip); }
    });
    connect(m_libraryModal, &EditorLibraryModal::solidSelected, this, [this](int k) {
        if (m_canvas) { m_canvas->setTool(ToolType::Shape); m_canvas->setShapeKind(ShapeKind::Solid3D); m_canvas->setSolidKind(k); }
    });
    connect(m_libraryModal, &EditorLibraryModal::paletteApplied, this, [this](ToolType t, const QVector<QColor>& cols) {
        if (m_canvas) m_canvas->setToolPalette(t, cols);
    });
    connect(m_libraryModal, &EditorLibraryModal::stickerSelected, this, [this](const QString& id) {
        if (m_canvas && m_elementsPanel) m_canvas->insertStickerTemplate(id, m_elementsPanel->stickerStyle());
    });
    connect(m_libraryModal, &EditorLibraryModal::closed, this, [this] {
        if (m_dropdownMenu && m_dropdownMenu->isVisible())
            m_dropdownMenu->hide();
        updateOverlayMask();
    });

    m_libraryPickerModal = new EditorLibraryPickerModal(this);
    connect(m_libraryPickerModal, &EditorLibraryPickerModal::notebookSelected,
            this, &FloatingToolbar::openNotebookInNewTabRequested);
    connect(m_libraryPickerModal, &EditorLibraryPickerModal::closed, this, [this] {
        updateOverlayMask();
    });

    m_boardSheet = new NewBoardSheet(this);
    connect(m_boardSheet, &NewBoardSheet::boardSubmitted, this, &FloatingToolbar::createDocumentRequested);
    connect(m_boardSheet, &NewBoardSheet::tagCreateRequested, this, [this](const QString& name) {
        const TagInfo created = LibraryStore::createTag(name);
        if (!created.id.isEmpty())
            m_boardSheet->setTags(LibraryStore::listTags());
    });
    connect(m_boardSheet, &GlassSheet::closed, this, [this] { updateOverlayMask(); });

    m_notebookSheet = new NewNotebookSheet(this);
    connect(m_notebookSheet, &NewNotebookSheet::notebookSubmitted, this, &FloatingToolbar::createDocumentRequested);
    connect(m_notebookSheet, &NewNotebookSheet::tagCreateRequested, this, [this](const QString& name) {
        const TagInfo created = LibraryStore::createTag(name);
        if (!created.id.isEmpty())
            m_notebookSheet->setTags(LibraryStore::listTags());
    });
    connect(m_notebookSheet, &GlassSheet::closed, this, [this] { updateOverlayMask(); });

    for (QWidget* w : {static_cast<QWidget*>(m_topBar), static_cast<QWidget*>(m_detachedDock),
                       static_cast<QWidget*>(m_elementsPanel), static_cast<QWidget*>(m_pagesPanel),
                       static_cast<QWidget*>(m_contextCapsule),
                       static_cast<QWidget*>(m_settingsModal), static_cast<QWidget*>(m_libraryModal)}) {
        if (w)
            w->installEventFilter(this);
    }
    for (QLineEdit* le : findChildren<QLineEdit*>())
        le->installEventFilter(this);

    applyThemeToAll(false);
    refreshFromCanvas();
}

void FloatingToolbar::initPresets() {
    m_presets.clear();
    m_presets.append(PenPreset{0, QStringLiteral("Pluma"), ToolType::Pen, QColor("#18181B"), 2.4,
                               LinePattern::Solid, 1, 1.0, QStringLiteral("1")});
    m_presets.append(PenPreset{1, QStringLiteral("Azul"), ToolType::Pen, QColor("#2563EB"), 2.4,
                               LinePattern::Solid, 1, 1.0, QStringLiteral("2")});
    m_presets.append(PenPreset{2, QStringLiteral("Rojo"), ToolType::Pen, QColor("#DC2626"), 2.4,
                               LinePattern::Solid, 1, 1.0, QStringLiteral("3")});
    m_presets.append(PenPreset{3, QStringLiteral("Triángulo"), ToolType::Shape, QColor("#18181B"), 2.4,
                               LinePattern::Dashed, 1, 1.0, QStringLiteral("4"), ShapeKind::Triangle, 0});
    m_presets.append(PenPreset{4, QStringLiteral("Resaltador"), ToolType::Highlighter, QColor("#FACC15"),
                               10.0, LinePattern::Solid, 0, 0.40, QStringLiteral("5")});
    m_activePreset = 0;
}

void FloatingToolbar::loadPresets() {
    const auto settings = appSettings();
    m_presetsRestored = settings->value(QStringLiteral("presets/customized"), false).toBool();
    for (int i = 0; i < m_presets.size(); ++i) {
        const QString prefix = QStringLiteral("presets/%1/").arg(i);
        if (!settings->contains(prefix + QStringLiteral("tool")))
            continue;
        PenPreset& p = m_presets[i];
        p.label = settings->value(prefix + QStringLiteral("label"), p.label).toString();
        p.tool = ToolType(settings->value(prefix + QStringLiteral("tool"), int(p.tool)).toInt());
        const QColor c(settings->value(prefix + QStringLiteral("color"), p.color.name(QColor::HexArgb)).toString());
        if (c.isValid()) p.color = c;
        p.size = settings->value(prefix + QStringLiteral("size"), p.size).toDouble();
        p.linePattern = LinePattern(settings->value(prefix + QStringLiteral("linePattern"), int(p.linePattern)).toInt());
        p.tipStyle = settings->value(prefix + QStringLiteral("tipStyle"), p.tipStyle).toInt();
        p.opacity = settings->value(prefix + QStringLiteral("opacity"), p.opacity).toDouble();
        p.shapeKind = ShapeKind(settings->value(prefix + QStringLiteral("shapeKind"), int(p.shapeKind)).toInt());
        p.shapeSubVariant = settings->value(prefix + QStringLiteral("shapeSubVariant"), p.shapeSubVariant).toInt();
        p.shortcut = settings->value(prefix + QStringLiteral("shortcut"), p.shortcut).toString();
    }
    if (m_libraryModal)
        m_libraryModal->setPresets(m_presets);
}

void FloatingToolbar::savePresets() {
    auto settings = appSettings();
    settings->setValue(QStringLiteral("presets/customized"), m_presetsRestored);
    for (int i = 0; i < m_presets.size(); ++i) {
        const QString prefix = QStringLiteral("presets/%1/").arg(i);
        const PenPreset& p = m_presets.at(i);
        settings->setValue(prefix + QStringLiteral("label"), p.label);
        settings->setValue(prefix + QStringLiteral("tool"), int(p.tool));
        settings->setValue(prefix + QStringLiteral("color"), p.color.name(QColor::HexArgb));
        settings->setValue(prefix + QStringLiteral("size"), p.size);
        settings->setValue(prefix + QStringLiteral("linePattern"), int(p.linePattern));
        settings->setValue(prefix + QStringLiteral("tipStyle"), p.tipStyle);
        settings->setValue(prefix + QStringLiteral("opacity"), p.opacity);
        settings->setValue(prefix + QStringLiteral("shapeKind"), int(p.shapeKind));
        settings->setValue(prefix + QStringLiteral("shapeSubVariant"), p.shapeSubVariant);
        settings->setValue(prefix + QStringLiteral("shortcut"), p.shortcut);
    }
    if (m_libraryModal)
        m_libraryModal->setPresets(m_presets);
}

void FloatingToolbar::saveCurrentToPreset(int index) {
    if (index < 0 || index >= m_presets.size() || !m_canvas)
        return;
    const ToolType t = m_canvas->tool();
    if (t == ToolType::Eraser || t == ToolType::Select || t == ToolType::Hand || t == ToolType::Laser)
        return;
    PenPreset& p = m_presets[index];
    p.tool = t;
    p.color = m_canvas->inkColor();
    p.size = m_canvas->strokeSize();
    p.linePattern = m_canvas->linePattern();
    p.tipStyle = (t == ToolType::Highlighter) ? m_canvas->highlighterTip() : m_canvas->penTip();
    p.opacity = (t == ToolType::Highlighter) ? m_canvas->highlighterOpacity() : 1.0;
    p.shapeKind = m_canvas->shapeKind();
    p.shapeSubVariant = m_canvas->triangleVariant();
    if (t == ToolType::Pen || t == ToolType::Ballpoint)
        p.label = (p.tipStyle == 0 ? QStringLiteral("Bolígrafo")
                 : (p.tipStyle == 2 ? QStringLiteral("Grafito")
                 : (p.tipStyle == 3 ? QStringLiteral("Tiza") : QStringLiteral("Pluma"))));
    else if (t == ToolType::Highlighter)
        p.label = QStringLiteral("Resaltador");
    else if (t == ToolType::Shape)
        p.label = QStringLiteral("Figuras");
    m_activePreset = index;
    m_presetsRestored = true;
    savePresets();
    showToast(tr("Atajo %1 guardado").arg(index + 1));
}

void FloatingToolbar::selectPreset(int index) {
    if (index < 0 || index >= m_presets.size() || !m_canvas)
        return;
    m_activePreset = index;
    const PenPreset& p = m_presets.at(index);
    m_canvas->setTool(p.tool);
    m_canvas->setInkColor(p.color);
    m_canvas->setStrokeSize(p.size);
    m_canvas->setLinePattern(p.linePattern);
    if (p.tool == ToolType::Highlighter) {
        m_canvas->setHighlighterTip(p.tipStyle);
        m_canvas->setHighlighterOpacity(p.opacity);
    } else if (p.tool == ToolType::Pen || p.tool == ToolType::Ballpoint) {
        m_canvas->setPenTip(p.tipStyle);
    } else if (p.tool == ToolType::Shape) {
        m_canvas->setShapeKind(p.shapeKind);
        m_canvas->setTriangleVariant(p.shapeSubVariant);
    }
    refreshFromCanvas();
}

QString FloatingToolbar::presetShortcut(int index) const {
    if (index < 0 || index >= m_presets.size())
        return QString::number(index + 1);
    return m_presets.at(index).shortcut;
}

void FloatingToolbar::setDockMode(ToolbarDockMode mode) {
    m_dockMode = mode;
    writeAppSetting(QStringLiteral("toolbar/dockMode"), int(mode));
    closeTransientPopovers();
    if (m_topBar)
        m_topBar->setDockMode(mode);
    positionAll();
    refreshFromCanvas();
}

void FloatingToolbar::setPageNavDirection(PageNavDirection dir) {
    if (m_pageNavDir == dir)
        return;
    m_pageNavDir = dir;
    writeAppSetting(QStringLiteral("pages/navDir"), int(dir));
    emit pageNavDirectionChanged(dir);
}

void FloatingToolbar::toggleCleanUi() {
    m_cleanUi = !m_cleanUi;
    closeTransientPopovers();
    positionAll();
    refreshFromCanvas();
}

void FloatingToolbar::toggleFullscreenRequested() {
    emit fullscreenToggled();
}

void FloatingToolbar::selectTool(ToolType tool) {
    if (!m_canvas)
        return;
    closeTransientPopovers();
    m_canvas->setTool(tool);
    refreshFromCanvas();
}

void FloatingToolbar::openStickersPopover() {
    if (!m_elementsPanel)
        return;
    closeTransientPopovers();
    if (m_elementsPanel->isVisible()) {
        m_elementsPanel->hide();
    } else {
        m_elementsPanel->syncFromCanvas();
        m_elementsPanel->show();
        m_elementsPanel->raise();
    }
    m_topBar->setElementsPanelOpen(m_elementsPanel->isVisible());
    m_detachedDock->setElementsPanelOpen(m_elementsPanel->isVisible());
    positionAll();
}

void FloatingToolbar::startInlineRenameActiveTab() {
    if (m_topBar)
        m_topBar->startInlineRenameActiveTab();
}

void FloatingToolbar::openSettingsModal(const QString& section) {
    if (!m_settingsModal)
        return;
    closeTransientPopovers();
    if (m_libraryPickerModal && m_libraryPickerModal->isVisible()) m_libraryPickerModal->hide();
    if (m_boardSheet && m_boardSheet->isVisible()) m_boardSheet->closeSheet(false);
    if (m_notebookSheet && m_notebookSheet->isVisible()) m_notebookSheet->closeSheet(false);
    m_settingsModal->setGeometry(rect());
    m_settingsModal->syncFromCanvas(m_canvas, m_dockMode, int(m_pageNavDir));
    m_settingsModal->openSection(section);
    updateOverlayMask();
}

void FloatingToolbar::openLibraryModal(const QString& section) {
    if (!m_libraryModal)
        return;
    closeTransientPopovers();
    if (m_libraryPickerModal && m_libraryPickerModal->isVisible()) m_libraryPickerModal->hide();
    if (m_boardSheet && m_boardSheet->isVisible()) m_boardSheet->closeSheet(false);
    if (m_notebookSheet && m_notebookSheet->isVisible()) m_notebookSheet->closeSheet(false);
    m_libraryModal->setGeometry(rect());
    m_libraryModal->setPresets(m_presets);
    m_libraryModal->syncFromCanvas(m_canvas);
    m_libraryModal->openSection(section);
    updateOverlayMask();
}

void FloatingToolbar::openLibraryPickerModal() {
    if (!m_libraryPickerModal)
        return;
    closeTransientPopovers();
    if (m_boardSheet && m_boardSheet->isVisible()) m_boardSheet->closeSheet(false);
    if (m_notebookSheet && m_notebookSheet->isVisible()) m_notebookSheet->closeSheet(false);
    m_libraryPickerModal->setGeometry(rect());
    m_libraryPickerModal->openPicker(m_openTabPaths);
    m_libraryPickerModal->raise();
    updateOverlayMask();
}

bool FloatingToolbar::hasModalOpen() const {
    return (m_settingsModal && m_settingsModal->isVisible())
        || (m_libraryModal && m_libraryModal->isVisible())
        || (m_libraryPickerModal && m_libraryPickerModal->isVisible())
        || (m_confirmCard && m_confirmCard->isVisible())
        || (m_boardSheet && m_boardSheet->isVisible())
        || (m_notebookSheet && m_notebookSheet->isVisible());
}

void FloatingToolbar::closeOpenModal() {
    if (m_dropdownMenu && m_dropdownMenu->isVisible()) {
        m_dropdownMenu->hide();
        updateOverlayMask();
        return;
    }
    if (m_confirmCard && m_confirmCard->isVisible()) {
        m_confirmCard->cancel();
        updateOverlayMask();
        return;
    }
    if (m_boardSheet && m_boardSheet->isVisible()) {
        m_boardSheet->closeSheet(false);
        return;
    }
    if (m_notebookSheet && m_notebookSheet->isVisible()) {
        m_notebookSheet->closeSheet(false);
        return;
    }
    if (m_libraryPickerModal && m_libraryPickerModal->isVisible()) {
        m_libraryPickerModal->hide();
        updateOverlayMask();
        return;
    }
    if (m_settingsModal && m_settingsModal->isVisible()) {
        m_settingsModal->hide();
        updateOverlayMask();
        return;
    }
    if (m_libraryModal && m_libraryModal->isVisible()) {
        m_libraryModal->hide();
        updateOverlayMask();
    }
}

bool FloatingToolbar::handleEscape() {
    if (m_dropdownMenu && m_dropdownMenu->isVisible()) {
        m_dropdownMenu->hide();
        updateOverlayMask();
        return true;
    }
    if (hasModalOpen()) {
        closeOpenModal();
        return true;
    }
    if (m_openPopover || (m_edgeMarkCapsule && m_edgeMarkCapsule->isVisible())
        || (m_contextCapsule && m_contextCapsule->moreMenuCard() && m_contextCapsule->moreMenuCard()->isVisible())
        || (m_contextCapsule && m_contextCapsule->fontPopoverCard() && m_contextCapsule->fontPopoverCard()->isVisible())) {
        closeTransientPopovers();
        return true;
    }
    return false;
}

void FloatingToolbar::openRadialColorPicker(const QPointF& nearScreenPos) {
    if (!m_canvas)
        return;
    if (m_canvas->isRadialMenuOpen()) {
        m_canvas->closeRadialMenu();
        return;
    }
    if (m_canvas->radialMenuEnabled()) {
        const QPointF target = nearScreenPos.isNull() ? m_canvas->lastHoverScreenPos() : nearScreenPos;
        m_canvas->openRadialMenu(target);
    }
}

void FloatingToolbar::setNotebookTabs(const QVector<TabEntry>& tabs) {
    QVector<EditorTabItem> items;
    items.reserve(tabs.size());
    m_openTabPaths.clear();
    for (const auto& t : tabs) {
        items.append({t.title, t.isBoard, t.isPdf, t.dirty, t.active});
        if (!t.filePath.isEmpty()) {
            const QString canon = QFileInfo(t.filePath).canonicalFilePath();
            m_openTabPaths.append(canon.isEmpty() ? t.filePath : canon);
        }
    }
    if (m_topBar)
        m_topBar->setTabs(items);
}

void FloatingToolbar::setDocumentTitle(const QString&, bool) {
    // El título vive ahora en cada pestaña de Fila 1.
}

void FloatingToolbar::setUpdateAvailable(const QString& version) {
    m_availableUpdateVersion = version;
}

void FloatingToolbar::applyThemeToAll(bool dark) {
    m_darkUi = dark;
    m_topBar->applyEditorTheme(dark);
    m_detachedDock->applyEditorTheme(dark);
    m_pageCapsule->applyEditorTheme(dark);
    m_restoreBtn->applyEditorTheme(dark);
    m_colorPopover->applyEditorTheme(dark);
    m_fineWidthPopover->applyEditorTheme(dark);
    m_penPanel->applyEditorTheme(dark);
    m_hlPanel->applyEditorTheme(dark);
    m_eraserPanel->applyEditorTheme(dark);
    m_fillPanel->applyEditorTheme(dark);
    m_lassoPanel->applyEditorTheme(dark);
    m_shapesPanel->applyEditorTheme(dark);
    m_paperPanel->applyEditorTheme(dark);
    m_elementsPanel->applyEditorTheme(dark);
    m_pagesPanel->applyEditorTheme(dark);
    m_contextCapsule->applyEditorTheme(dark);
    m_edgeMarkCapsule->applyEditorTheme(dark);
    m_dropdownMenu->applyEditorTheme(dark);
    m_toast->applyEditorTheme(dark);
    m_confirmCard->applyEditorTheme(dark);
    m_settingsModal->applyEditorTheme(dark);
    m_libraryModal->applyEditorTheme(dark);
    if (m_libraryPickerModal)
        m_libraryPickerModal->applyEditorTheme(dark);
}

void FloatingToolbar::refreshFromCanvas() {
    if (!m_canvas)
        return;

    const int themeMode = SettingsView::themeMode();
    const bool dark = (themeMode == SettingsView::Dark)
                   || (themeMode == SettingsView::System
                       && (readAppSetting(QStringLiteral("home/darkMode"), false).toBool()
                           || m_canvas->isCurrentSheetDark()));
    if (dark != m_darkUi)
        applyThemeToAll(dark);

    m_topBar->syncFromCanvas(m_canvas);
    m_detachedDock->syncFromCanvas(m_canvas, m_dockMode);

    const bool infinite = (m_canvas->pageFormat() == PageFormat::Infinite);
    if (infinite && m_pagesPanel->isVisible())
        m_pagesPanel->hide();

    m_pageCapsule->setPageInfo(m_canvas->currentPageIndex() + 1, std::max(1, m_canvas->pageCount()));
    m_pageCapsule->setPagesPanelOpen(m_pagesPanel && m_pagesPanel->isVisible());
    if (m_openPopover == m_penPanel) m_penPanel->syncFromCanvas();
    else if (m_openPopover == m_hlPanel) m_hlPanel->syncFromCanvas();
    else if (m_openPopover == m_eraserPanel) m_eraserPanel->syncFromCanvas();
    else if (m_openPopover == m_fillPanel) m_fillPanel->syncFromCanvas();
    else if (m_openPopover == m_lassoPanel) m_lassoPanel->syncFromCanvas();
    else if (m_openPopover == m_shapesPanel) m_shapesPanel->syncFromCanvas();
    else if (m_openPopover == m_paperPanel) m_paperPanel->syncFromCanvas();

    if (m_pagesPanel->isVisible())
        m_pagesPanel->syncFromCanvas();
    if (m_cleanUi || m_canvas->isActivelyDragging()) {
        m_contextCapsule->hide();
    } else {
        m_contextCapsule->syncFromCanvas();
    }

    positionAll();
}

void FloatingToolbar::openToolPanel(ToolType tool, const QRect& anchorRect) {
    QWidget* target = nullptr;
    if (tool == ToolType::Pen || tool == ToolType::Ballpoint) target = m_penPanel;
    else if (tool == ToolType::Highlighter) target = m_hlPanel;
    else if (tool == ToolType::Eraser) target = m_eraserPanel;
    else if (tool == ToolType::Fill) target = m_fillPanel;
    else if (tool == ToolType::Lasso) target = m_lassoPanel;
    else if (tool == ToolType::Shape || tool == ToolType::Polyline) target = m_shapesPanel;
    if (!target)
        return;

    if (m_openPopover == target && target->isVisible()) {
        closeTransientPopovers();
        return;
    }
    closeTransientPopovers();
    m_openPopover = target;
    m_openPopoverAnchor = anchorRect;

    if (target == m_penPanel) { m_penPanel->syncFromCanvas(); m_penPanel->playOpenAnimation(); }
    else if (target == m_hlPanel) { m_hlPanel->syncFromCanvas(); m_hlPanel->playOpenAnimation(); }
    else if (target == m_eraserPanel) { m_eraserPanel->syncFromCanvas(); m_eraserPanel->playOpenAnimation(); }
    else if (target == m_fillPanel) { m_fillPanel->syncFromCanvas(); m_fillPanel->playOpenAnimation(); }
    else if (target == m_lassoPanel) { m_lassoPanel->syncFromCanvas(); m_lassoPanel->playOpenAnimation(); }
    else if (target == m_shapesPanel) { m_shapesPanel->syncFromCanvas(); m_shapesPanel->playOpenAnimation(); }

    positionFloatingCard(target, anchorRect);
    target->show();
    target->raise();
    updateOverlayMask();
}

void FloatingToolbar::openPaperPanel(const QRect& anchorRect) {
    if (m_openPopover == m_paperPanel && m_paperPanel->isVisible()) {
        closeTransientPopovers();
        return;
    }
    closeTransientPopovers();
    m_openPopover = m_paperPanel;
    m_openPopoverAnchor = anchorRect;
    m_paperPanel->syncFromCanvas();
    positionFloatingCard(m_paperPanel, anchorRect, true);
    m_paperPanel->show();
    m_paperPanel->raise();
    updateOverlayMask();
}

void FloatingToolbar::openColorPopover(ToolType tool, const QRect& anchorRect, bool isShapeFill, bool isSheetBg, bool isSelectionColor) {
    if (m_openPopover == m_colorPopover && m_colorPopover->isVisible()
        && m_shapeFillMode == isShapeFill && m_sheetColorMode == isSheetBg
        && m_selectionColorMode == isSelectionColor) {
        closeTransientPopovers();
        return;
    }
    closeTransientPopovers();
    m_shapeFillMode = isShapeFill;
    m_sheetColorMode = isSheetBg;
    m_selectionColorMode = isSelectionColor;
    m_colorTargetTool = tool;
    m_lastColorAnchor = anchorRect;
    m_openPopover = m_colorPopover;
    m_openPopoverAnchor = anchorRect;

    if (m_canvas) {
        if (isSheetBg) {
            const QColor cur = m_canvas->customBackground().isValid()
                ? m_canvas->customBackground()
                : effectiveThemeConfig(m_canvas->themeId(), {}).background;
            m_colorPopover->setColor(cur);
        } else if (isSelectionColor) {
            m_colorPopover->setColor(m_canvas->selectionStyle().color.value_or(m_canvas->inkColor()));
        } else if (isShapeFill) {
            m_colorPopover->setColor(m_canvas->selectionStyle().fillColor.value_or(m_canvas->inkColor()));
        } else {
            m_colorPopover->setColor(tool == ToolType::Laser ? m_canvas->laserColor() : m_canvas->inkColor());
        }
    }
    if (isSelectionColor) {
        const int margin = 12;
        const int x = std::clamp(anchorRect.center().x() - m_colorPopover->width() / 2,
                                 margin, std::max(margin, width() - m_colorPopover->width() - margin));
        const int minY = (m_topBar && m_topBar->isVisible()) ? m_topBar->height() + 8 : margin;
        int anchorTop = anchorRect.top();
        if (m_contextCapsule && m_contextCapsule->isVisible())
            anchorTop = std::min(anchorTop, m_contextCapsule->geometry().top());
        const int y = std::clamp(anchorTop - m_colorPopover->height() - 10,
                                 minY, std::max(minY, height() - m_colorPopover->height() - margin));
        m_colorPopover->move(x, y);
    } else {
        positionFloatingCard(m_colorPopover, anchorRect, isSheetBg);
    }
    m_colorPopover->show();
    m_colorPopover->raise();
    updateOverlayMask();
}

void FloatingToolbar::openFineWidthPopover(ToolType tool, const QRect& anchorRect) {
    if (m_openPopover == m_fineWidthPopover && m_fineWidthPopover->isVisible()) {
        closeTransientPopovers();
        return;
    }
    closeTransientPopovers();
    m_widthTargetTool = tool;
    m_lastWidthAnchor = anchorRect;
    m_openPopover = m_fineWidthPopover;
    m_openPopoverAnchor = anchorRect;
    if (m_canvas) {
        const auto widths = m_canvas->toolWidths(tool);
        const int slot = m_canvas->activeWidthSlot(tool);
        m_fineWidthPopover->configure(tool, widths[std::clamp(slot, 0, 2)]);
    }
    positionFloatingCard(m_fineWidthPopover, anchorRect);
    m_fineWidthPopover->show();
    m_fineWidthPopover->raise();
    updateOverlayMask();
}

void FloatingToolbar::openDropdown(QWidget* anchor, const QVector<EditorDropdownItem>& items, std::function<void(int)> cb) {
    if (!m_dropdownMenu)
        return;
    closeTransientPopovers();
    QPoint pos(16, 48);
    if (anchor) {
        const QPoint bl = anchor->mapTo(this, QPoint(0, anchor->height() + 6));
        pos = bl;
    }
    m_dropdownMenu->openMenu(items, pos, size(), [this, cb = std::move(cb)](int id) {
        updateOverlayMask();
        if (cb) cb(id);
    });
    updateOverlayMask();
}

void FloatingToolbar::showEdgeMarkCapsule(const QPointF& screenPos, const QPointF& worldPos) {
    if (!m_edgeMarkCapsule || !m_canvas)
        return;
    closeTransientPopovers();
    m_edgeMarkCapsule->showAtWorldPoint(worldPos, m_canvas->inkColor());
    const int x = std::clamp(int(screenPos.x()) - m_edgeMarkCapsule->width() / 2,
                             12, std::max(12, width() - m_edgeMarkCapsule->width() - 12));
    int y = int(screenPos.y()) - m_edgeMarkCapsule->height() - 16;
    const int minY = m_topBar ? m_topBar->geometry().bottom() + 8 : 48;
    if (y < minY)
        y = int(screenPos.y()) + 18;
    m_edgeMarkCapsule->move(x, std::clamp(y, minY, std::max(minY, height() - m_edgeMarkCapsule->height() - 12)));
    updateOverlayMask();
}

void FloatingToolbar::closeTransientPopovers() {
    QRegion closedDirty;
    const QList<QWidget*> pops = {
        m_penPanel, m_hlPanel, m_eraserPanel, m_fillPanel,
        m_lassoPanel, m_shapesPanel, m_shapesPanel ? m_shapesPanel->bubbleWidget() : nullptr, m_paperPanel,
        m_contextCapsule ? m_contextCapsule->moreMenuCard() : nullptr,
        m_contextCapsule ? m_contextCapsule->fontPopoverCard() : nullptr,
        m_colorPopover, m_fineWidthPopover, m_edgeMarkCapsule, m_dropdownMenu
    };
    for (QWidget* w : pops) {
        if (w && w->isVisible()) {
            closedDirty += w->geometry().adjusted(-20, -16, 20, 26);
            if (m_contextCapsule && w == m_contextCapsule->moreMenuCard())
                m_contextCapsule->closeMoreMenu();
            else if (m_contextCapsule && w == m_contextCapsule->fontPopoverCard())
                m_contextCapsule->closeFontPopover();
            else
                w->hide();
        }
    }
    m_openPopover = nullptr;
    updateOverlayMask();
    if (!closedDirty.isEmpty()) {
        const QRect r = closedDirty.boundingRect();
        update(r);
        if (m_canvas)
            m_canvas->update(r);
        if (parentWidget())
            parentWidget()->update(r);
    }
}

void FloatingToolbar::positionFloatingCard(QWidget* card, const QRect& anchorRect, bool preferLeft) {
    if (!card)
        return;
    const int margin = 12;
    int x = anchorRect.left();
    int y = anchorRect.bottom() + 8;

    if (preferLeft)
        x = anchorRect.right() - card->width();
    if (m_dockMode == ToolbarDockMode::Left && anchorRect.left() < 90 && anchorRect.top() > 40) {
        x = anchorRect.right() + 10;
        y = anchorRect.top() - 8;
    } else if (m_dockMode == ToolbarDockMode::Float && anchorRect.bottom() > height() - 90) {
        x = anchorRect.center().x() - card->width() / 2;
        y = anchorRect.top() - card->height() - 10;
    }

    const int minY = (m_topBar && m_topBar->isVisible()) ? m_topBar->height() + 8 : margin;
    x = std::clamp(x, margin, std::max(margin, width() - card->width() - margin));
    y = std::clamp(y, minY, std::max(minY, height() - card->height() - margin));
    card->move(x, y);
}

void FloatingToolbar::positionContextCapsule() {
    if (!m_contextCapsule || !m_contextCapsule->isVisible())
        return;
    m_contextCapsule->adjustSize();
    const int minX = (m_elementsPanel->isVisible() && !m_elementsRight)
        ? m_elementsPanel->geometry().right() + 12
        : ((m_dockMode == ToolbarDockMode::Left && m_detachedDock->isVisible()) ? m_detachedDock->geometry().right() + 12 : 12);
    const int maxRight = m_pagesPanel->isVisible() ? m_pagesPanel->geometry().left() - 12 : width() - 12;
    const int availCenter = (minX + maxRight) / 2;
    const int x = std::clamp(availCenter - m_contextCapsule->width() / 2,
                             minX, std::max(minX, maxRight - m_contextCapsule->width()));
    int y = height() - m_contextCapsule->height() - 18;
    if (m_dockMode == ToolbarDockMode::Float && m_detachedDock->isVisible())
        y = m_detachedDock->geometry().top() - m_contextCapsule->height() - 10;
    m_contextCapsule->move(x, std::max(54, y));
}

void FloatingToolbar::positionAll() {
    const int W = width();
    const int H = height();
    if (W <= 0 || H <= 0)
        return;

    if (m_cleanUi) {
        m_topBar->hide();
        m_detachedDock->hide();
        m_pageCapsule->hide();
        m_elementsPanel->hide();
        m_pagesPanel->hide();
        m_restoreBtn->move(W - 46, 12);
        m_restoreBtn->show();
        m_restoreBtn->raise();
    } else {
        m_restoreBtn->hide();
        m_topBar->setGeometry(0, 0, W, m_dockMode == ToolbarDockMode::Top ? 98 : 42);
        m_topBar->show();

        if (m_dockMode == ToolbarDockMode::Left) {
            m_detachedDock->move(12, 56);
            m_detachedDock->show();
        } else if (m_dockMode == ToolbarDockMode::Float) {
            m_detachedDock->move(std::max(12, (W - m_detachedDock->width()) / 2), H - m_detachedDock->height() - 16);
            m_detachedDock->show();
        } else {
            m_detachedDock->hide();
        }

        const int sideTop = m_topBar->height() + 8;
        const int sideBottom = (m_dockMode == ToolbarDockMode::Float && m_detachedDock->isVisible())
            ? m_detachedDock->geometry().top() - 12 : H - 12;
        const int sideH = std::max(180, sideBottom - sideTop);

        if (m_elementsPanel->isVisible()) {
            const int ex = m_elementsRight
                ? (m_pagesPanel->isVisible() ? m_pagesPanel->geometry().left() - m_elementsPanel->width() - 10 : W - m_elementsPanel->width() - 12)
                : (m_dockMode == ToolbarDockMode::Left ? m_detachedDock->geometry().right() + 10 : 12);
            m_elementsPanel->setGeometry(ex, sideTop, m_elementsPanel->width(), sideH);
        }
        if (m_pagesPanel->isVisible()) {
            m_pagesPanel->setGeometry(W - m_pagesPanel->width() - 12, sideTop, m_pagesPanel->width(), sideH);
        }

        const bool showPgCap = m_canvas && m_canvas->pageFormat() != PageFormat::Infinite
            && !m_pagesPanel->isVisible();
        if (showPgCap) {
            const int px = W - m_pageCapsule->width() - 16;
            const int py = H - m_pageCapsule->height() - 16;
            m_pageCapsule->move(std::max(12, px), py);
            m_pageCapsule->show();
            if (m_dockMode == ToolbarDockMode::Float && m_detachedDock->isVisible()
                && m_detachedDock->geometry().right() + 12 > m_pageCapsule->x()) {
                m_detachedDock->move(std::max(12, m_pageCapsule->x() - m_detachedDock->width() - 12),
                                     m_detachedDock->y());
            }
        } else {
            m_pageCapsule->hide();
        }
    }

    if (m_openPopover && m_openPopover->isVisible())
        positionFloatingCard(m_openPopover, m_openPopoverAnchor, m_openPopover == m_paperPanel);

    positionContextCapsule();

    if (m_toast->isVisible()) {
        const int ty = (m_topBar && m_topBar->isVisible()) ? m_topBar->height() + 14 : 14;
        m_toast->move(std::max(12, (W - m_toast->width()) / 2), ty);
    }
    if (m_settingsModal->isVisible())
        m_settingsModal->setGeometry(rect());
    if (m_libraryModal->isVisible())
        m_libraryModal->setGeometry(rect());
    if (m_libraryPickerModal && m_libraryPickerModal->isVisible())
        m_libraryPickerModal->setGeometry(rect());
    if (m_boardSheet && m_boardSheet->isVisible())
        m_boardSheet->repositionOnResize();
    if (m_notebookSheet && m_notebookSheet->isVisible())
        m_notebookSheet->repositionOnResize();

    updateOverlayMask();
}

void FloatingToolbar::updateOverlayMask() {
    const bool fullCover = m_settingsModal->isVisible() || m_libraryModal->isVisible() ||
        (m_libraryPickerModal && m_libraryPickerModal->isVisible()) ||
        m_confirmCard->isVisible() || m_dropdownMenu->isVisible() ||
        (m_boardSheet && m_boardSheet->isVisible()) ||
        (m_notebookSheet && m_notebookSheet->isVisible());

    QRegion region;
    if (fullCover) {
        region = QRegion(rect());
    } else {
        const QList<QWidget*> surfaces = {
            m_topBar, m_detachedDock, m_pageCapsule, m_restoreBtn,
            m_elementsPanel, m_pagesPanel,
            m_contextCapsule ? static_cast<QWidget*>(m_contextCapsule->topCard()) : nullptr,
            m_contextCapsule ? static_cast<QWidget*>(m_contextCapsule->bottomCard()) : nullptr,
            m_contextCapsule ? static_cast<QWidget*>(m_contextCapsule->moreMenuCard()) : nullptr,
            m_contextCapsule ? static_cast<QWidget*>(m_contextCapsule->fontPopoverCard()) : nullptr,
            m_edgeMarkCapsule,
            m_colorPopover, m_fineWidthPopover, m_penPanel, m_hlPanel, m_eraserPanel,
            m_fillPanel, m_lassoPanel, m_shapesPanel, m_shapesPanel ? m_shapesPanel->bubbleWidget() : nullptr, m_paperPanel, m_toast
        };
        for (QWidget* w : surfaces) {
            if (w && w->isVisible()) {
                const QPoint tl = (w->parentWidget() == this) ? w->pos() : w->mapTo(this, QPoint(0, 0));
                region += QRect(tl, w->size()).adjusted(-18, -14, 18, 24);
            }
        }
    }

    if (region != m_lastOverlayMask) {
        const QRegion removed = m_lastOverlayMask - region;
        m_lastOverlayMask = region;
        setMask(region);
        update();
        if (!removed.isEmpty()) {
            const QRect exposed = removed.boundingRect().adjusted(-6, -6, 6, 6);
            if (m_canvas)
                m_canvas->update(exposed);
            if (parentWidget())
                parentWidget()->update(exposed);
        }
    }
}

QRect FloatingToolbar::activeToolAnchorRect(ToolType tool) const {
    if (m_dockMode == ToolbarDockMode::Top && m_topBar)
        return m_topBar->anchorRectForTool(tool);
    if (m_detachedDock)
        return m_detachedDock->anchorRectForTool(tool);
    return QRect(120, 50, 40, 40);
}

void FloatingToolbar::paintEvent(QPaintEvent* event) {
    QWidget::paintEvent(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QList<EditorGlassCard*> cards = {
        m_detachedDock, m_pageCapsule, m_elementsPanel, m_pagesPanel,
        m_contextCapsule ? m_contextCapsule->topCard() : nullptr,
        m_contextCapsule ? m_contextCapsule->bottomCard() : nullptr,
        m_contextCapsule ? m_contextCapsule->moreMenuCard() : nullptr,
        m_contextCapsule ? m_contextCapsule->fontPopoverCard() : nullptr,
        m_edgeMarkCapsule,
        m_colorPopover, m_fineWidthPopover, m_penPanel, m_hlPanel, m_eraserPanel,
        m_fillPanel, m_lassoPanel, m_shapesPanel, m_shapesPanel ? m_shapesPanel->bubbleWidget() : nullptr, m_paperPanel,
        m_toast, m_dropdownMenu
    };
    for (EditorGlassCard* card : cards) {
        if (!card || !card->isVisible() || card->cornerRadius() <= 0.5)
            continue;
        const qreal lift = (card == (m_contextCapsule ? m_contextCapsule->topCard() : nullptr)
                         || card == (m_contextCapsule ? m_contextCapsule->bottomCard() : nullptr)
                         || card == m_pageCapsule || card == m_toast) ? 3.0 : 5.0;
        const QPoint tl = (card->parentWidget() == this) ? card->pos() : card->mapTo(this, QPoint(0, 0));
        paintSoftShadow(p, QRectF(tl, card->size()).adjusted(0.5, 0.5, -0.5, -0.5),
                        card->cornerRadius(), lift, m_darkUi);
    }

    if (m_confirmCard && m_confirmCard->isVisible()) {
        p.fillRect(rect(), editorPaletteFor(m_darkUi).veil);
        paintSoftShadow(p, QRectF(m_confirmCard->geometry()).adjusted(0.5, 0.5, -0.5, -0.5),
                        m_confirmCard->cornerRadius(), 6.0, m_darkUi);
    }
}

void FloatingToolbar::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    positionAll();
}

void FloatingToolbar::mouseReleaseEvent(QMouseEvent* event) {
    if (m_dropdownMenu->isVisible() && !m_dropdownMenu->geometry().contains(event->pos())) {
        m_dropdownMenu->hide();
        updateOverlayMask();
        return;
    }
    if (m_confirmCard && m_confirmCard->isVisible() && !m_confirmCard->geometry().contains(event->pos())) {
        m_confirmCard->cancel();
        updateOverlayMask();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void FloatingToolbar::tabletEvent(QTabletEvent* event) {
    event->ignore();
}

bool FloatingToolbar::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::MouseButtonPress) {
        if (auto* w = qobject_cast<QWidget*>(watched);
            w == m_topBar || w == m_detachedDock || w == m_elementsPanel || w == m_pagesPanel
            || w == m_contextCapsule || w == m_settingsModal || w == m_libraryModal) {
            auto* me = static_cast<QMouseEvent*>(event);
            const QPoint pt = w->mapTo(this, me->pos());
            if (m_dropdownMenu && m_dropdownMenu->isVisible() && !m_dropdownMenu->geometry().contains(pt)) {
                m_dropdownMenu->hide();
                updateOverlayMask();
                return true;
            }
            if (m_contextCapsule && m_contextCapsule->moreMenuCard() && m_contextCapsule->moreMenuCard()->isVisible()
                && !m_contextCapsule->moreMenuCard()->geometry().contains(pt)
                && w != m_contextCapsule) {
                m_contextCapsule->closeMoreMenu();
                updateOverlayMask();
                return true;
            }
            if (m_contextCapsule && m_contextCapsule->fontPopoverCard() && m_contextCapsule->fontPopoverCard()->isVisible()
                && !m_contextCapsule->fontPopoverCard()->geometry().contains(pt)
                && w != m_contextCapsule) {
                m_contextCapsule->closeFontPopover();
                updateOverlayMask();
                return true;
            }
            if ((w == m_elementsPanel || w == m_pagesPanel || w == m_settingsModal || w == m_libraryModal)
                && ((m_openPopover && m_openPopover->isVisible()) || (m_edgeMarkCapsule && m_edgeMarkCapsule->isVisible()))) {
                closeTransientPopovers();
            }
        }
    }
    if (event->type() == QEvent::ShortcutOverride && qobject_cast<QLineEdit*>(watched)) {
        auto* ke = static_cast<QKeyEvent*>(event);
        if (ke->modifiers() == Qt::NoModifier || ke->modifiers() == Qt::ShiftModifier) {
            const int k = ke->key();
            if ((k >= Qt::Key_A && k <= Qt::Key_Z) || (k >= Qt::Key_0 && k <= Qt::Key_9) ||
                k == Qt::Key_Space || k == Qt::Key_Backspace || k == Qt::Key_Delete ||
                k == Qt::Key_Return || k == Qt::Key_Enter || k == Qt::Key_Escape) {
                ke->accept();
                return true;
            }
        }
    }
    if (watched == m_canvas) {
        if (event->type() == QEvent::Resize) {
            setGeometry(m_canvas->rect());
            positionAll();
        } else if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::TabletPress) {
            const bool anyPop = (m_openPopover && m_openPopover->isVisible())
                || (m_edgeMarkCapsule && m_edgeMarkCapsule->isVisible())
                || (m_dropdownMenu && m_dropdownMenu->isVisible())
                || (m_contextCapsule && m_contextCapsule->moreMenuCard() && m_contextCapsule->moreMenuCard()->isVisible())
                || (m_contextCapsule && m_contextCapsule->fontPopoverCard() && m_contextCapsule->fontPopoverCard()->isVisible());
            if (anyPop) {
                closeTransientPopovers();
                m_swallowNextCanvasRelease = true;
                m_swallowTimer.restart();
                if (event->type() == QEvent::TabletPress)
                    static_cast<QTabletEvent*>(event)->accept();
                return true;
            }
            if (m_swallowNextCanvasRelease && m_swallowTimer.isValid() && m_swallowTimer.elapsed() < 1200) {
                if (event->type() == QEvent::TabletPress)
                    static_cast<QTabletEvent*>(event)->accept();
                return true;
            }
            m_swallowNextCanvasRelease = false;
        } else if (m_swallowNextCanvasRelease) {
            if (!m_swallowTimer.isValid() || m_swallowTimer.elapsed() >= 1200) {
                m_swallowNextCanvasRelease = false;
            } else if (event->type() == QEvent::MouseMove || event->type() == QEvent::TabletMove
                       || event->type() == QEvent::MouseButtonDblClick) {
                if (event->type() == QEvent::TabletMove)
                    static_cast<QTabletEvent*>(event)->accept();
                return true;
            } else if (event->type() == QEvent::MouseButtonRelease || event->type() == QEvent::TabletRelease) {
                if (event->type() == QEvent::TabletRelease)
                    static_cast<QTabletEvent*>(event)->accept();
                QTimer::singleShot(80, this, [this] { m_swallowNextCanvasRelease = false; });
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

#ifdef FINILY_DEV_SNAPSHOTS
void FloatingToolbar::applySnapshotScene(const QString& scene) {
    if (scene == QLatin1String("left")) {
        setDockMode(ToolbarDockMode::Left);
    } else if (scene == QLatin1String("float")) {
        setDockMode(ToolbarDockMode::Float);
    } else if (scene == QLatin1String("pen")) {
        selectTool(ToolType::Pen);
        openToolPanel(ToolType::Pen, activeToolAnchorRect(ToolType::Pen));
    } else if (scene == QLatin1String("eraser")) {
        selectTool(ToolType::Eraser);
        openToolPanel(ToolType::Eraser, activeToolAnchorRect(ToolType::Eraser));
    } else if (scene == QLatin1String("fill")) {
        selectTool(ToolType::Fill);
        openToolPanel(ToolType::Fill, activeToolAnchorRect(ToolType::Fill));
    } else if (scene == QLatin1String("shapes")) {
        selectTool(ToolType::Shape);
        openToolPanel(ToolType::Shape, activeToolAnchorRect(ToolType::Shape));
    } else if (scene == QLatin1String("paper")) {
        openPaperPanel(m_topBar->paperButtonRect());
    } else if (scene == QLatin1String("elements")) {
        if (!m_elementsPanel->isVisible()) openStickersPopover();
        if (m_canvas && m_canvas->pageFormat() != PageFormat::Infinite && !m_pagesPanel->isVisible()) {
            m_pagesPanel->syncFromCanvas();
            m_pagesPanel->show();
            positionAll();
        }
    } else if (scene == QLatin1String("settings")) {
        openSettingsModal(QStringLiteral("figuras"));
    } else if (scene == QLatin1String("settings-tablet")) {
        openSettingsModal(QStringLiteral("tableta"));
    } else if (scene == QLatin1String("library")) {
        openLibraryModal(QStringLiteral("atajos"));
    }
}
#endif

} // namespace finily
