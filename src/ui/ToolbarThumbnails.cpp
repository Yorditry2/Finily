#include "ui/FloatingToolbar.h"
#include "ui/FloatingToolbarInternal.h"
#include "ui/CanvasWidget.h"

#include <QApplication>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace finily {
namespace {

class PageThumbnailList final : public QListWidget {
public:
    explicit PageThumbnailList(QWidget* parent) : QListWidget(parent) {
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
        const int first = destination - int(std::lower_bound(rows.cbegin(), rows.cend(), destination) - rows.cbegin());
        for (qsizetype i = 0; i < rows.size(); ++i)
            if (auto* entry = item(first + i))
                entry->setSelected(true);
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
            ? previous : selectedRows();
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        const auto position = event->position().toPoint();
        if (m_pressedRows.isEmpty() || !(event->buttons() & Qt::LeftButton)
            || (m_destination < 0 && (position - m_pressPosition).manhattanLength() < QApplication::startDragDistance())) {
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
        const bool inside = viewport()->rect().adjusted(-12, -12, 12, 12).contains(event->position().toPoint());
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
        const int y = m_destination >= count() ? visualItemRect(item(count() - 1)).bottom() + 3
                                               : visualItemRect(item(m_destination)).top() - 3;
        QPainter painter(viewport());
        painter.setPen(QPen(palette().highlight().color(), 2));
        painter.drawLine(6, y, viewport()->width() - 6, y);
    }

private:
    void updateInsertionMarker() {
        const auto* target = itemAt(m_dragPosition);
        m_destination = target ? row(target) + (m_dragPosition.y() >= visualItemRect(target).center().y())
            : std::clamp((m_dragPosition.y() + verticalScrollBar()->value() + gridSize().height() / 2)
                             / gridSize().height(), 0, count());
        viewport()->update();
    }

    QPoint m_pressPosition;
    QPoint m_dragPosition;
    QVector<int> m_pressedRows;
    int m_destination = -1;
    QTimer m_scrollTimer;
};

} // namespace

void FloatingToolbar::buildThumbPanel() {
    m_thumbPanel = new GlassCapsule(this);
    m_thumbPanel->setObjectName("finilyPopover");
    m_thumbPanel->setFixedWidth(164);
    m_thumbPanel->hide();
    auto* outer = new QVBoxLayout(m_thumbPanel);
    outer->setContentsMargins(8, 8, 8, 8);
    outer->setSpacing(6);

    auto* list = new PageThumbnailList(m_thumbPanel);
    m_thumbList = list;
    list->setObjectName("finilyPageThumbnails");
    list->setFrameShape(QFrame::NoFrame);
    list->setStyleSheet("QListWidget{background:transparent;} QListWidget::item{padding:4px;}");
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
    list->movePages = [this](QVector<int> rows, int index) { return m_canvas && m_canvas->movePages(rows, index); };
    connect(list, &QListWidget::itemClicked, this, [this, list](QListWidgetItem* item) {
        if (list->selectionMode() != QAbstractItemView::MultiSelection)
            m_canvas->goToPage(list->row(item));
    });
    connect(list, &QListWidget::customContextMenuRequested, this, [this, list](const QPoint& point) {
        const auto* item = list->itemAt(point);
        showPageMenu(list->viewport()->mapToGlobal(point), item ? list->row(item) : -1);
    });
    outer->addWidget(list, 1);

    auto* buttons = new QHBoxLayout;
    auto* add = new QToolButton(m_thumbPanel);
    add->setText(tr("+  Nueva página"));
    add->setToolTip(tr("Añadir página después de la actual"));
    polishButton(add);
    connect(add, &QToolButton::clicked, this, [this] { m_canvas->addPage(); });
    buttons->addWidget(add, 1);
    m_thumbActions = new QToolButton(m_thumbPanel);
    m_thumbActions->setText(QStringLiteral("⋯"));
    m_thumbActions->setToolTip(tr("Acciones de página"));
    polishButton(m_thumbActions);
    connect(m_thumbActions, &QToolButton::clicked, this, [this] {
        showPageMenu(m_thumbActions->mapToGlobal(QPoint(0, m_thumbActions->height())));
    });
    buttons->addWidget(m_thumbActions);
    outer->addLayout(buttons);

    m_thumbTimer = new QTimer(this);
    m_thumbTimer->setSingleShot(true);
    m_thumbTimer->setInterval(350);
    connect(m_thumbTimer, &QTimer::timeout, this, &FloatingToolbar::refreshPageThumbnail);
    connect(m_canvas, &CanvasWidget::pageStructureChanged, this, &FloatingToolbar::rebuildThumbs);
}

void FloatingToolbar::rebuildThumbs() {
    if (!m_thumbPanel || m_thumbPanel->isHidden() || !m_canvas)
        return;
    QSignalBlocker blocker(m_thumbList);
    static_cast<PageThumbnailList*>(m_thumbList)->cancelDrag();
    m_thumbList->clear();
    for (int i = 0; i < m_canvas->pageCount(); ++i) {
        auto* item = new QListWidgetItem(QString::number(i + 1), m_thumbList);
        item->setIcon(QIcon(QPixmap::fromImage(m_canvas->pageThumbnail(i, thumbnailSize))));
        item->setTextAlignment(Qt::AlignCenter);
        item->setToolTip(tr("Página %1").arg(i + 1));
        item->setFlags(item->flags() & ~Qt::ItemIsDropEnabled);
    }
    m_thumbCurrent = m_canvas->currentPageIndex();
    m_thumbList->setCurrentRow(m_thumbCurrent);
}

void FloatingToolbar::refreshPageThumbnail() {
    if (m_thumbPanel->isHidden() || !m_canvas)
        return;
    const int current = m_canvas->currentPageIndex();
    if (auto* item = m_thumbList->item(current))
        item->setIcon(QIcon(QPixmap::fromImage(m_canvas->pageThumbnail(current, thumbnailSize))));
}

void FloatingToolbar::showPageMenu(const QPoint& position, int clickedRow) {
    if (!m_canvas)
        return;
    auto* list = static_cast<PageThumbnailList*>(m_thumbList);
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
        list->setSelectionMode(enabled ? QAbstractItemView::MultiSelection : QAbstractItemView::ExtendedSelection);
        if (!enabled && list->currentRow() >= 0)
            list->setCurrentRow(list->currentRow(), QItemSelectionModel::ClearAndSelect);
    });
    menu.addSeparator();
    auto* up = menu.addAction(tr("Mover arriba"), [list, rows, first] { list->moveRows(rows, first - 1); });
    auto* down = menu.addAction(tr("Mover abajo"), [list, rows, last] { list->moveRows(rows, last + 2); });
    up->setEnabled(first > 0);
    down->setEnabled(last + 1 < m_canvas->pageCount());
    menu.addSeparator();
    menu.addAction(tr("Insertar página antes"), [this, first] { m_canvas->insertPage(first); });
    menu.addAction(tr("Insertar página después"), [this, last] { m_canvas->insertPage(last + 1); });
    menu.addAction(tr("Insertar página cuadriculada después"), [this, last] {
        m_canvas->insertPage(last + 1, BackgroundTemplate::Grid);
    });
    menu.addAction(tr("Duplicar"), [this, rows] { m_canvas->duplicatePages(rows); });
    menu.addSeparator();
    menu.addAction(tr("Cortar"), [this, rows] {
        if (m_canvas->copyPages(rows))
            m_canvas->deletePages(rows);
    });
    menu.addAction(tr("Copiar"), [this, rows] { m_canvas->copyPages(rows); });
    auto* pasteBefore = menu.addAction(tr("Pegar antes"), [this, first] { m_canvas->pastePages(first); });
    auto* pasteAfter = menu.addAction(tr("Pegar después"), [this, last] { m_canvas->pastePages(last + 1); });
    pasteBefore->setEnabled(m_canvas->hasCopiedPages());
    pasteAfter->setEnabled(m_canvas->hasCopiedPages());
    menu.addSeparator();
    menu.addAction(rows.size() == 1 ? tr("Eliminar página") : tr("Eliminar páginas"),
                   [this, rows] { m_canvas->deletePages(rows); });
    menu.exec(position);
}

void FloatingToolbar::importPdfDialog() {
    if (!m_canvas)
        return;
    closePopovers();
    const QString path = QFileDialog::getOpenFileName(window(), "Importar PDF", QString(), "Archivos PDF (*.pdf)");
    if (path.isEmpty())
        return;
    m_canvas->importPdfFile(path);
    refreshFromCanvas();
}

void FloatingToolbar::exportPdfDialog() {
    if (!m_canvas)
        return;
    closePopovers();
    QString path = QFileDialog::getSaveFileName(window(), "Exportar clase a PDF", "finily-clase.pdf",
                                                "Archivos PDF (*.pdf)");
    if (path.isEmpty())
        return;
    if (!path.endsWith(".pdf", Qt::CaseInsensitive))
        path += ".pdf";
    m_canvas->exportToPdf(path);
    refreshFromCanvas();
}

void FloatingToolbar::newDocumentAction() {
    if (!m_canvas)
        return;
    closePopovers();
    if (!confirmDiscardChanges(QStringLiteral("crear un cuaderno nuevo")))
        return;
    m_canvas->newDocument();
    refreshFromCanvas();
}

void FloatingToolbar::openNotebookDialog() {
    if (!m_canvas)
        return;
    closePopovers();
    if (!confirmDiscardChanges(QStringLiteral("abrir otro cuaderno")))
        return;
    const QString path = QFileDialog::getOpenFileName(window(), "Abrir cuaderno", QString(),
                                                      "Cuaderno Finily (*.finily)");
    if (path.isEmpty())
        return;
    if (!m_canvas->loadFromFile(path))
        showToast(tr("No se pudo abrir el cuaderno"));
    refreshFromCanvas();
}

bool FloatingToolbar::confirmDiscardChanges(const QString& action) {
    if (!m_canvas || !m_canvas->hasUnsavedChanges())
        return true;
    QMessageBox box(QMessageBox::Warning, QStringLiteral("Cambios sin guardar"),
                    QStringLiteral("El cuaderno actual tiene cambios sin guardar.\n"
                                   "¿Quieres guardarlos antes de %1?").arg(action),
                    QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, window());
    box.setDefaultButton(QMessageBox::Save);
    box.button(QMessageBox::Save)->setText(QStringLiteral("Guardar"));
    box.button(QMessageBox::Discard)->setText(QStringLiteral("No guardar"));
    box.button(QMessageBox::Cancel)->setText(QStringLiteral("Cancelar"));
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
    if (!m_canvas)
        return;
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
    if (!m_canvas)
        return;
    closePopovers();
    QString path = QFileDialog::getSaveFileName(window(), "Guardar cuaderno", "cuaderno.finily",
                                                "Cuaderno Finily (*.finily)", nullptr,
                                                QFileDialog::DontConfirmOverwrite);
    if (path.isEmpty())
        return;
    if (!path.endsWith(".finily", Qt::CaseInsensitive))
        path += ".finily";
    const bool exists = QFileInfo::exists(path);
    if (exists) {
        QMessageBox confirmation(QMessageBox::Question, tr("Reemplazar cuaderno"),
                                 tr("¿Quieres reemplazar «%1»?").arg(QFileInfo(path).fileName()),
                                 QMessageBox::Save | QMessageBox::Cancel, window());
        confirmation.button(QMessageBox::Save)->setText(tr("Reemplazar"));
        confirmation.button(QMessageBox::Cancel)->setText(tr("Cancelar"));
        confirmation.setDefaultButton(QMessageBox::Cancel);
        if (confirmation.exec() != QMessageBox::Save)
            return;
    }
    m_canvas->saveToFile(path, !exists);
    refreshFromCanvas();
}

} // namespace finily
