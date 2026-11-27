#include "ui/popovers/ThumbnailPanel.h"

#include "ui/CanvasWidget.h"
#include "ui/ToolbarIcons.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QIcon>
#include <QItemSelectionModel>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <optional>
#include <utility>

namespace finily {
namespace {

class PageThumbnailList final : public QListWidget {
public:
    explicit PageThumbnailList(QWidget* parent)
        : QListWidget(parent) {
        m_scrollTimer.setInterval(35);
        connect(&m_scrollTimer, &QTimer::timeout, this, [this] {
            auto* bar = verticalScrollBar();
            if (m_dragPosition.y() < 24)
                bar->setValue(bar->value() - 14);
            else if (m_dragPosition.y() >= viewport()->height() - 24)
                bar->setValue(bar->value() + 14);
            updateInsertionMarker();
        });
    }

    std::function<bool(QVector<int>, int)> movePages;

    void setAccentColor(const QColor& accent) {
        if (m_accent == accent)
            return;
        m_accent = accent;
        viewport()->update();
    }

    QVector<int> selectedRows() const {
        QVector<int> rows;
        for (const auto* entry : selectedItems())
            rows.append(row(entry));
        std::sort(rows.begin(), rows.end());
        return rows;
    }

    void cancelDrag() {
        m_pressedRows.clear();
        m_destination = -1;
        m_scrollTimer.stop();
        viewport()->update();
    }

    void moveRows(const QVector<int>& rows, int destination) {
        if (!movePages || rows.isEmpty() || !movePages(rows, destination))
            return;
        const int shifted = int(std::lower_bound(rows.cbegin(), rows.cend(), destination) - rows.cbegin());
        const int first = destination - shifted;
        for (qsizetype i = 0; i < rows.size(); ++i) {
            if (auto* entry = item(first + int(i)))
                entry->setSelected(true);
        }
    }

protected:
    void mousePressEvent(QMouseEvent* event) override {
        cancelDrag();
        const auto position = event->position().toPoint();
        const auto* clicked = itemAt(position);
        const auto previous = selectedRows();
        QListWidget::mousePressEvent(event);
        if (event->button() != Qt::LeftButton || !clicked)
            return;
        const int index = row(clicked);
        m_pressPosition = position;
        m_pressedRows = event->modifiers() == Qt::NoModifier && previous.contains(index)
            ? previous
            : selectedRows();
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        const auto position = event->position().toPoint();
        const int dist = (position - m_pressPosition).manhattanLength();
        if (m_pressedRows.isEmpty() || !(event->buttons() & Qt::LeftButton)
            || (m_destination < 0 && dist < QApplication::startDragDistance())) {
            QListWidget::mouseMoveEvent(event);
            return;
        }
        m_dragPosition = position;
        updateInsertionMarker();
        if (!m_scrollTimer.isActive())
            m_scrollTimer.start();
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton || m_destination < 0) {
            QListWidget::mouseReleaseEvent(event);
            return;
        }
        const QRect bounds = viewport()->rect().adjusted(-12, -12, 12, 12);
        const bool inside = bounds.contains(event->position().toPoint());
        const int destination = m_destination;
        const auto rows = std::move(m_pressedRows);
        cancelDrag();
        {
            const QSignalBlocker blocker(this);
            QListWidget::mouseReleaseEvent(event);
        }
        if (inside)
            moveRows(rows, destination);
    }

    void hideEvent(QHideEvent* event) override {
        cancelDrag();
        QListWidget::hideEvent(event);
    }

    void paintEvent(QPaintEvent* event) override {
        QListWidget::paintEvent(event);
        if (m_destination < 0 || count() == 0)
            return;
        const int y = m_destination >= count()
            ? visualItemRect(item(count() - 1)).bottom() + 3
            : visualItemRect(item(m_destination)).top() - 3;
        QPainter painter(viewport());
        painter.setPen(QPen(m_accent, 2));
        painter.drawLine(6, y, viewport()->width() - 6, y);
    }

private:
    void updateInsertionMarker() {
        const auto* target = itemAt(m_dragPosition);
        if (target) {
            const bool belowCenter = m_dragPosition.y() >= visualItemRect(target).center().y();
            m_destination = row(target) + int(belowCenter);
        } else {
            const int cellH = std::max(1, gridSize().height());
            const int raw = (m_dragPosition.y() + verticalScrollBar()->value() + cellH / 2) / cellH;
            m_destination = std::clamp(raw, 0, count());
        }
        viewport()->update();
    }

    QPoint m_pressPosition;
    QPoint m_dragPosition;
    QVector<int> m_pressedRows;
    int m_destination = -1;
    QTimer m_scrollTimer;
    QColor m_accent{QStringLiteral("#60a5fa")};
};

} // namespace

ThumbnailPanel::ThumbnailPanel(CanvasWidget* canvas, QWidget* parent)
    : GlassCapsule(parent)
    , m_canvas(canvas) {
    setObjectName(QStringLiteral("finilyPopover"));
    setCapsuleRadius(16);
    setFixedWidth(164);
    hide();

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 8, 8, 8);
    outer->setSpacing(6);

    auto* list = new PageThumbnailList(this);
    m_list = list;
    list->setObjectName(QStringLiteral("finilyPageThumbnails"));
    list->setFrameShape(QFrame::NoFrame);
    list->setStyleSheet(QStringLiteral(
        "QListWidget{background:transparent;} QListWidget::item{padding:4px;}"));
    list->setIconSize(thumbnailSize);
    list->setViewMode(QListView::IconMode);
    list->setFlow(QListView::TopToBottom);
    list->setWrapping(false);
    list->setMovement(QListView::Static);
    list->setGridSize(QSize(128, 112));
    list->setFocusPolicy(Qt::NoFocus);
    list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list->setContextMenuPolicy(Qt::CustomContextMenu);
    list->movePages = [this](QVector<int> rows, int index) {
        return m_canvas && m_canvas->movePages(rows, index);
    };
    connect(list, &QListWidget::itemClicked, this, [this, list](QListWidgetItem* item) {
        if (!m_canvas || !item)
            return;
        if (list->selectionMode() != QAbstractItemView::MultiSelection)
            m_canvas->goToPage(list->row(item));
    });
    connect(list, &QListWidget::customContextMenuRequested, this, [this, list](const QPoint& point) {
        const auto* item = list->itemAt(point);
        showPageMenu(list->viewport()->mapToGlobal(point), item ? list->row(item) : -1);
    });
    outer->addWidget(list, 1);

    const ThemeConfig initialTheme = getThemeConfig(ThemeId::DarkBoard);
    list->setAccentColor(initialTheme.accent);

    auto* buttons = new QHBoxLayout();
    m_plusBtn = new QToolButton(this);
    m_plusBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_plusBtn->setIconSize(QSize(14, 14));
    m_plusBtn->setFixedHeight(28);
    m_plusBtn->setText(tr("Nueva página"));
    m_plusBtn->setToolTip(tr("Añadir página después de la actual"));
    m_plusBtn->setIcon(glyphIcon(QStringLiteral("plus"), initialTheme.uiText));
    polishButton(m_plusBtn);
    m_iconSpecs.append(qMakePair(m_plusBtn, QStringLiteral("plus")));
    connect(m_plusBtn, &QToolButton::clicked, this, [this] {
        if (!m_canvas)
            return;
        m_canvas->addPage();
        emit refreshToolbarRequested();
    });
    buttons->addWidget(m_plusBtn, 1);

    m_actionsBtn = new QToolButton(this);
    m_actionsBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_actionsBtn->setIconSize(QSize(16, 16));
    m_actionsBtn->setFixedSize(28, 28);
    m_actionsBtn->setToolTip(tr("Acciones de página"));
    m_actionsBtn->setIcon(glyphIcon(QStringLiteral("more"), initialTheme.uiText));
    polishButton(m_actionsBtn);
    m_iconSpecs.append(qMakePair(m_actionsBtn, QStringLiteral("more")));
    connect(m_actionsBtn, &QToolButton::clicked, this, [this] {
        showPageMenu(m_actionsBtn->mapToGlobal(QPoint(0, m_actionsBtn->height())));
    });
    buttons->addWidget(m_actionsBtn);
    outer->addLayout(buttons);

    m_thumbTimer = new QTimer(this);
    m_thumbTimer->setSingleShot(true);
    m_thumbTimer->setInterval(350);
    connect(m_thumbTimer, &QTimer::timeout, this, &ThumbnailPanel::refreshCurrentThumbnail);
    if (m_canvas)
        connect(m_canvas, &CanvasWidget::pageStructureChanged, this, &ThumbnailPanel::rebuild);
}

ThumbnailPanel::~ThumbnailPanel() = default;

void ThumbnailPanel::rebuild() {
    if (isHidden() || !m_canvas || !m_list)
        return;
    QSignalBlocker blocker(m_list);
    static_cast<PageThumbnailList*>(m_list)->cancelDrag();
    m_list->clear();
    const int count = m_canvas->pageCount();
    for (int i = 0; i < count; ++i) {
        auto* item = new QListWidgetItem(QString::number(i + 1), m_list);
        item->setIcon(QIcon(QPixmap::fromImage(m_canvas->pageThumbnail(i, thumbnailSize))));
        item->setTextAlignment(Qt::AlignCenter);
        item->setToolTip(tr("Página %1").arg(i + 1));
        item->setFlags(item->flags() & ~Qt::ItemIsDropEnabled);
    }
    m_thumbCurrent = m_canvas->currentPageIndex();
    m_list->setCurrentRow(m_thumbCurrent);
}

void ThumbnailPanel::refreshCurrentThumbnail() {
    if (isHidden() || !m_canvas || !m_list)
        return;
    const int current = m_canvas->currentPageIndex();
    if (auto* item = m_list->item(current))
        item->setIcon(QIcon(QPixmap::fromImage(m_canvas->pageThumbnail(current, thumbnailSize))));
}

void ThumbnailPanel::applyTheme(const ThemeConfig& theme, const QString& qss) {
    GlassCapsule::applyTheme(theme, qss);
    for (const auto& spec : m_iconSpecs) {
        if (spec.first)
            spec.first->setIcon(glyphIcon(spec.second, theme.uiText));
    }
    if (auto* list = static_cast<PageThumbnailList*>(m_list))
        list->setAccentColor(theme.accent);
}

void ThumbnailPanel::syncFromCanvas() {
    if (isHidden() || !m_canvas || !m_list || !m_thumbTimer)
        return;
    const int active = m_canvas->currentPageIndex();
    if (m_thumbCurrent != active) {
        if (auto* previous = m_list->item(m_thumbCurrent)) {
            const QImage thumb = m_canvas->pageThumbnail(m_thumbCurrent, thumbnailSize);
            previous->setIcon(QIcon(QPixmap::fromImage(thumb)));
        }
        m_thumbCurrent = active;
        if (m_list->currentRow() != active)
            m_list->setCurrentRow(active, QItemSelectionModel::ClearAndSelect);
    }
    m_thumbTimer->start();
}

void ThumbnailPanel::showPageMenu(const QPoint& globalPosition, int clickedRow) {
    if (!m_canvas || !m_list)
        return;
    auto* list = static_cast<PageThumbnailList*>(m_list);
    QVector<int> rows = list->selectedRows();
    if (clickedRow >= 0 && !rows.contains(clickedRow)) {
        list->setCurrentRow(clickedRow, QItemSelectionModel::ClearAndSelect);
        rows = {clickedRow};
    }
    if (rows.isEmpty())
        rows.append(m_canvas->currentPageIndex());

    const int first = rows.first();
    const int last = rows.last();
    QMenu menu(this);
    auto* multi = menu.addAction(tr("Seleccionar varias páginas"));
    multi->setCheckable(true);
    multi->setChecked(list->selectionMode() == QAbstractItemView::MultiSelection);
    connect(multi, &QAction::toggled, this, [list](bool enabled) {
        list->setSelectionMode(
            enabled ? QAbstractItemView::MultiSelection : QAbstractItemView::ExtendedSelection);
        if (!enabled && list->currentRow() >= 0)
            list->setCurrentRow(list->currentRow(), QItemSelectionModel::ClearAndSelect);
    });
    menu.addSeparator();

    auto* up = menu.addAction(tr("Mover arriba"), [list, rows, first] {
        list->moveRows(rows, first - 1);
    });
    auto* down = menu.addAction(tr("Mover abajo"), [list, rows, last] {
        list->moveRows(rows, last + 2);
    });
    up->setEnabled(first > 0);
    down->setEnabled(last + 1 < m_canvas->pageCount());
    menu.addSeparator();

    menu.addAction(tr("Insertar página antes"), [this, first] {
        if (m_canvas)
            m_canvas->insertPage(first);
    });
    menu.addAction(tr("Insertar página después"), [this, last] {
        if (m_canvas)
            m_canvas->insertPage(last + 1);
    });
    menu.addAction(tr("Insertar varias páginas…"), [this, last] {
        if (!m_canvas)
            return;
        QDialog dialog(window());
        dialog.setWindowTitle(tr("Insertar varias páginas"));
        auto* col = new QVBoxLayout(&dialog);

        auto* rowCount = new QHBoxLayout();
        rowCount->addWidget(new QLabel(tr("Cantidad:"), &dialog));
        auto* spin = new QSpinBox(&dialog);
        spin->setRange(1, 100);
        spin->setValue(5);
        rowCount->addWidget(spin, 1);
        col->addLayout(rowCount);

        auto* rowPos = new QHBoxLayout();
        rowPos->addWidget(new QLabel(tr("Posición:"), &dialog));
        auto* posCombo = new QComboBox(&dialog);
        posCombo->addItem(tr("Después de la página actual"));
        posCombo->addItem(tr("Al final del cuaderno"));
        rowPos->addWidget(posCombo, 1);
        col->addLayout(rowPos);

        auto* rowTpl = new QHBoxLayout();
        rowTpl->addWidget(new QLabel(tr("Plantilla:"), &dialog));
        auto* tplCombo = new QComboBox(&dialog);
        tplCombo->addItem(tr("Igual que el cuaderno"), -1);
        tplCombo->addItem(tr("Liso"), int(BackgroundTemplate::Blank));
        tplCombo->addItem(tr("Cuadrícula"), int(BackgroundTemplate::Grid));
        tplCombo->addItem(tr("Puntos"), int(BackgroundTemplate::Dots));
        tplCombo->addItem(tr("Rayado"), int(BackgroundTemplate::Ruled));
        rowTpl->addWidget(tplCombo, 1);
        col->addLayout(rowTpl);

        auto* dlgButtons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        connect(dlgButtons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(dlgButtons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        col->addWidget(dlgButtons);

        if (dialog.exec() != QDialog::Accepted || !m_canvas)
            return;
        const int count = spin->value();
        const int tplData = tplCombo->currentData().toInt();
        std::optional<BackgroundTemplate> bg;
        if (tplData >= 0)
            bg = static_cast<BackgroundTemplate>(tplData);
        QVector<PageData> batch;
        batch.reserve(count);
        for (int i = 0; i < count; ++i) {
            PageData page;
            page.background = bg;
            batch.append(std::move(page));
        }
        const int targetIndex = (posCombo->currentIndex() == 1) ? m_canvas->pageCount() : (last + 1);
        m_canvas->insertPages(targetIndex, batch);
        emit refreshToolbarRequested();
    });
    menu.addAction(tr("Insertar página cuadriculada después"), [this, last] {
        if (m_canvas)
            m_canvas->insertPage(last + 1, BackgroundTemplate::Grid);
    });
    menu.addAction(tr("Duplicar"), [this, rows] {
        if (m_canvas)
            m_canvas->duplicatePages(rows);
    });
    menu.addSeparator();

    menu.addAction(tr("Cortar"), [this, rows] {
        if (m_canvas && m_canvas->copyPages(rows))
            m_canvas->deletePages(rows);
    });
    menu.addAction(tr("Copiar"), [this, rows] {
        if (m_canvas)
            m_canvas->copyPages(rows);
    });
    auto* pasteBefore = menu.addAction(tr("Pegar antes"), [this, first] {
        if (m_canvas)
            m_canvas->pastePages(first);
    });
    auto* pasteAfter = menu.addAction(tr("Pegar después"), [this, last] {
        if (m_canvas)
            m_canvas->pastePages(last + 1);
    });
    pasteBefore->setEnabled(m_canvas->hasCopiedPages());
    pasteAfter->setEnabled(m_canvas->hasCopiedPages());
    menu.addSeparator();

    const QString deleteLabel = rows.size() == 1 ? tr("Eliminar página") : tr("Eliminar páginas");
    menu.addAction(deleteLabel, [this, rows] {
        if (m_canvas)
            m_canvas->deletePages(rows);
    });
    menu.exec(globalPosition);
}

} // namespace finily
