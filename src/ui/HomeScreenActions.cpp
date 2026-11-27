#include "ui/HomeScreen.h"

#include "core/Utils.h"
#include "engine/DocumentSerializer.h"
#include "engine/LibraryStore.h"
#include "engine/NotebookDocument.h"
#include "engine/PdfEngine.h"
#include "ui/DesignTokens.h"
#include "ui/UiIcons.h"
#include "ui/home/FolderCards.h"
#include "ui/home/HomeMenu.h"
#include "ui/home/HomeMotion.h"
#include "ui/home/HomeSidebar.h"
#include "ui/home/HomeTheme.h"
#include "ui/home/LibraryViews.h"
#include "ui/home/MoveToSheet.h"
#include "ui/home/NewBoardSheet.h"
#include "ui/home/NewFolderSheet.h"
#include "ui/home/NewNotebookSheet.h"
#include "ui/home/NotebookCards.h"
#include "ui/home/TrashCards.h"

#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPainter>
#include <QPdfDocument>
#include <QPointer>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <thread>

namespace finily {

// Diálogo inline Liquid Glass para crear/renombrar etiquetas (sin QInputDialog/QDialog).
class TagPromptOverlay : public QWidget {
public:
    explicit TagPromptOverlay(QWidget* parent)
        : QWidget(parent) {
        setFocusPolicy(Qt::StrongFocus);
        setAttribute(Qt::WA_StyledBackground, false);

        m_edit = new QLineEdit(this);
        m_edit->setFrame(false);
        connect(m_edit, &QLineEdit::returnPressed, this, [this] { finish(true); });
        hide();
    }

    void ask(const QString& title, const QString& initial, const QString& confirmLabel,
             bool dark, std::function<void(const QString&)> onAccept) {
        m_title = title;
        m_confirmText = confirmLabel;
        m_dark = dark;
        m_onAccept = std::move(onAccept);
        m_hoverBtn = 0;

        if (parentWidget())
            setGeometry(parentWidget()->rect());

        const tokens::Palette tk = tokens::paletteFor(dark);
        QFont ef = QApplication::font();
        ef.setPixelSize(13);
        m_edit->setFont(ef);
        m_edit->setStyleSheet(QStringLiteral(
            "QLineEdit { background: %1; color: %2; border: 1.5px solid %3; border-radius: 10px;"
            " padding: 0 10px; selection-background-color: %4; }")
                                  .arg(tk.field.name(), tk.text.name(), tk.accent.name(),
                                       tk.soft.name(QColor::HexArgb)));
        m_edit->setText(initial);

        const QRect c = cardRect();
        m_edit->setGeometry(c.left() + 20, c.top() + 56, c.width() - 40, 36);
        if (parentWidget() && parentWidget()->isVisible())
            m_backdrop = blurBackdropImage(parentWidget()->grab(c).toImage(), 11);
        else
            m_backdrop = QImage();

        show();
        raise();
        m_edit->setFocus(Qt::PopupFocusReason);
        m_edit->selectAll();
        update();
    }

protected:
    QRect cardRect() const {
        return QRect(width() / 2 - 170, height() / 2 - 82, 340, 164);
    }
    QRect cancelRect() const {
        const QRect c = cardRect();
        return QRect(c.left() + 20, c.bottom() - 20 - 36, 146, 36);
    }
    QRect confirmRect() const {
        const QRect c = cardRect();
        return QRect(c.left() + 20 + 146 + 8, c.bottom() - 20 - 36, 146, 36);
    }
    void finish(bool accepted) {
        const QString val = m_edit->text().trimmed();
        auto cb = m_onAccept;
        m_onAccept = {};
        m_backdrop = QImage();
        hide();
        if (accepted && !val.isEmpty() && cb)
            cb(val);
    }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.fillRect(rect(), m_dark ? QColor(0, 0, 0, 80) : QColor(24, 24, 27, 32));
        const QRect c = cardRect();
        paintLiquidGlassPanel(p, QRectF(c), 18.0, m_backdrop, homePaletteFor(m_dark), m_dark);

        const tokens::Palette tk = tokens::paletteFor(m_dark);
        QFont tf = QApplication::font();
        tf.setPixelSize(15);
        tf.setWeight(QFont::DemiBold);
        p.setFont(tf);
        p.setPen(tk.text);
        p.drawText(QRect(c.left() + 20, c.top() + 20, c.width() - 40, 22),
                   Qt::AlignLeft | Qt::AlignVCenter, m_title);

        QFont bf = QApplication::font();
        bf.setPixelSize(13);
        bf.setWeight(QFont::DemiBold);
        p.setFont(bf);
        p.setPen(Qt::NoPen);
        p.setBrush(m_hoverBtn == 1 ? tk.hover : tk.field);
        p.drawRoundedRect(QRectF(cancelRect()), 11.0, 11.0);
        p.setPen(tk.text);
        p.drawText(cancelRect(), Qt::AlignCenter, QStringLiteral("Cancelar"));

        p.setPen(Qt::NoPen);
        p.setBrush(m_hoverBtn == 2 ? homePaletteFor(m_dark).accentHover : tk.accent);
        p.drawRoundedRect(QRectF(confirmRect()), 11.0, 11.0);
        p.setPen(tk.accentFg);
        p.drawText(confirmRect(), Qt::AlignCenter, m_confirmText);
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        const QPoint pos = e->position().toPoint();
        const int h = cancelRect().contains(pos) ? 1 : (confirmRect().contains(pos) ? 2 : 0);
        if (h != m_hoverBtn) {
            m_hoverBtn = h;
            setCursor(h ? Qt::PointingHandCursor : Qt::ArrowCursor);
            update();
        }
    }
    void mouseReleaseEvent(QMouseEvent* e) override {
        const QPoint pos = e->position().toPoint();
        if (confirmRect().contains(pos))
            finish(true);
        else if (cancelRect().contains(pos) || !cardRect().contains(pos))
            finish(false);
    }
    void keyPressEvent(QKeyEvent* e) override {
        if (e->key() == Qt::Key_Escape)
            finish(false);
    }

private:
    QLineEdit* m_edit = nullptr;
    QString m_title;
    QString m_confirmText;
    std::function<void(const QString&)> m_onAccept;
    QImage m_backdrop;
    bool m_dark = false;
    int m_hoverBtn = 0;
};

namespace {

QVector<HomeMenuItem> buildTagSubmenu(const QVector<TagInfo>& allTags,
                                      const QStringList& activeIds,
                                      const std::function<void(const QString&, bool)>& onToggle,
                                      const std::function<void()>& onNewTag) {
    QVector<HomeMenuItem> sub;
    for (const TagInfo& t : allTags) {
        const bool on = activeIds.contains(t.id);
        const QColor dot = tokens::folderColor(t.colorIndex).body2;
        sub << HomeMenuItem::check(t.name, on, dot,
                                   [onToggle, tid = t.id](bool checked) { onToggle(tid, checked); },
                                   true);
    }
    if (!allTags.isEmpty())
        sub << HomeMenuItem::sep();
    sub << HomeMenuItem::act(QStringLiteral("plus"), QStringLiteral("Nueva etiqueta…"), onNewTag);
    return sub;
}

} // namespace

// ------------------------------------------------------------ menús contextuales

void HomeScreen::showNotebookMenu(NotebookCard* card, const QPoint& globalPos) {
    if (!card || !m_menu)
        return;
    const NotebookMeta meta = card->meta();
    const QPointer<NotebookCard> guard(card);

    QVector<HomeMenuItem> items;
    items << HomeMenuItem::act(QStringLiteral("open"), QStringLiteral("Abrir"),
                               [this, path = meta.path] { emit openNotebookRequested(path); },
                               QStringLiteral("↵"));
    items << HomeMenuItem::act(QStringLiteral("tab"), QStringLiteral("Abrir en pestaña nueva"),
                               [this, path = meta.path] { emit openNotebookInNewTabRequested(path); });
    items << HomeMenuItem::sep();
    items << HomeMenuItem::act(QStringLiteral("pencil"), QStringLiteral("Renombrar"),
                               [guard] { if (guard) guard->beginRename(); }, QStringLiteral("F2"));
    items << HomeMenuItem::act(QStringLiteral("copy"), QStringLiteral("Duplicar"),
                               [this, path = meta.path] { duplicateNotebooks({path}); },
                               QStringLiteral("Ctrl D"));

    // «Mover a…»: destinos recientes + «Elegir carpeta…».
    HomeMenuItem move = HomeMenuItem::act(QStringLiteral("folderMove"), QStringLiteral("Mover a…"), {});
    if (!meta.folderRelativePath.isEmpty()) {
        move.children << HomeMenuItem::act(QStringLiteral("docs"), QStringLiteral("Documentos"),
                                           [this, path = meta.path] { moveNotebook(path, QString()); });
    }
    const QStringList recents = LibraryStore::recentMoveFolders();
    int addedRecent = 0;
    for (const QString& rel : recents) {
        if (rel == meta.folderRelativePath)
            continue;
        for (const FolderMeta& f : m_allFolders) {
            if (f.relativePath == rel) {
                move.children << HomeMenuItem::act(QStringLiteral("folder"), f.name,
                                                   [this, path = meta.path, rel] { moveNotebook(path, rel); });
                ++addedRecent;
                break;
            }
        }
        if (addedRecent >= 5)
            break;
    }
    if (addedRecent == 0) {
        for (const FolderMeta& f : m_allFolders) {
            if (f.relativePath == meta.folderRelativePath)
                continue;
            move.children << HomeMenuItem::act(QStringLiteral("folder"), f.name,
                                               [this, path = meta.path, rel = f.relativePath] { moveNotebook(path, rel); });
            if (++addedRecent >= 5)
                break;
        }
    }
    if (!move.children.isEmpty())
        move.children << HomeMenuItem::sep();
    move.children << HomeMenuItem::act(QStringLiteral("folderMove"), QStringLiteral("Elegir carpeta…"),
                                       [this, path = meta.path, title = meta.title, cur = meta.folderRelativePath] {
                                           openMoveSheetFor({path}, {}, title, cur);
                                       });
    items << move;

    const bool fav = LibraryStore::isDocumentFavorite(meta.path);
    items << HomeMenuItem::act(
        QStringLiteral("star"),
        fav ? QStringLiteral("Quitar de favoritos") : QStringLiteral("Añadir a favoritos"),
        [this, path = meta.path, nextFav = !fav] {
            LibraryStore::setDocumentFavorite(path, nextFav);
            refreshLibrary();
        },
        QStringLiteral("Ctrl B"));

    HomeMenuItem tagsItem = HomeMenuItem::act(QStringLiteral("tag"), QStringLiteral("Etiquetas"), {});
    const QStringList activeTags = LibraryStore::documentTags(meta.path);
    tagsItem.children = buildTagSubmenu(
        m_allTags, activeTags,
        [this, path = meta.path](const QString& tagId, bool on) {
            LibraryStore::setDocumentTag(path, tagId, on);
            refreshLibrary();
        },
        [this, path = meta.path] { promptNewTag({path}); });
    items << tagsItem;

    items << HomeMenuItem::sep();
    if (meta.pageFormat != PageFormat::Infinite && !meta.hasPdf) {
        items << HomeMenuItem::act(QStringLiteral("cover"), QStringLiteral("Cambiar portada"), [this, meta] {
            m_notebookSheet->openEditCover(meta, homePaletteFor(m_dark), m_dark);
        });
    } else if (meta.pageFormat == PageFormat::Infinite && !meta.hasPdf) {
        items << HomeMenuItem::act(QStringLiteral("palette"), QStringLiteral("Fondo y plantilla"), [this, meta] {
            m_boardSheet->openEditStyle(meta, homePaletteFor(m_dark), m_dark);
        });
    }
    items << HomeMenuItem::act(QStringLiteral("export"), QStringLiteral("Exportar como PDF"),
                               [this, path = meta.path] { exportNotebooksToPdf({path}); });
    items << HomeMenuItem::sep();
    HomeMenuItem trash = HomeMenuItem::act(
        QStringLiteral("trash"), QStringLiteral("Mover a la papelera"),
        [this, guard, path = meta.path, name = meta.title] { moveToTrash(path, name, false, guard.data()); },
        QStringLiteral("Supr"));
    trash.danger = true;
    items << trash;
    m_menu->popup(mapFromGlobal(globalPos), items);
}

void HomeScreen::showFolderMenu(const FolderMeta& meta, const QPoint& globalPos, ObjectCard* card) {
    if (!m_menu)
        return;
    const QPointer<ObjectCard> guard(card);
    const QString rel = meta.relativePath;
    const QString parentRel = QFileInfo(rel).path() == QLatin1String(".") ? QString() : QFileInfo(rel).path();

    QVector<HomeMenuItem> items;
    items << HomeMenuItem::act(QStringLiteral("open"), QStringLiteral("Abrir"),
                               [this, rel] { openFolder(rel, true); }, QStringLiteral("↵"));
    items << HomeMenuItem::sep();
    items << HomeMenuItem::act(QStringLiteral("pencil"), QStringLiteral("Renombrar"), [this, guard, meta] {
        if (guard)
            guard->beginRename();
        else
            m_folderSheet->openEdit(meta, homePaletteFor(m_dark), m_dark);
    }, QStringLiteral("F2"));

    items << HomeMenuItem::header(QStringLiteral("Color"));
    HomeMenuItem swatches;
    swatches.kind = HomeMenuItem::Kind::Swatches;
    swatches.swatchCurrent = meta.colorIndex;
    swatches.swatchPicked = [this, rel, icon = meta.iconIndex](int color) {
        LibraryStore::updateFolderStyle(rel, color, icon);
        refreshLibrary();
    };
    items << swatches;
    items << HomeMenuItem::act(QStringLiteral("sparkle"), QStringLiteral("Icono…"), [this, meta] {
        m_folderSheet->openEdit(meta, homePaletteFor(m_dark), m_dark);
    });
    items << HomeMenuItem::sep();
    items << HomeMenuItem::act(QStringLiteral("folderPlus"), QStringLiteral("Nueva subcarpeta"), [this, rel] {
        m_folderSheet->setFolders(m_allFolders);
        m_folderSheet->openCreate(rel, homePaletteFor(m_dark), m_dark);
    });

    HomeMenuItem move = HomeMenuItem::act(QStringLiteral("folderMove"), QStringLiteral("Mover a…"), {});
    if (!parentRel.isEmpty()) {
        move.children << HomeMenuItem::act(QStringLiteral("docs"), QStringLiteral("Documentos"),
                                           [this, rel] {
                                               PendingMoveTarget t;
                                               t.folderRels = {rel};
                                               t.label = QFileInfo(rel).fileName();
                                               executeMoveTarget(t, QString(), true);
                                           });
    }
    int added = 0;
    for (const FolderMeta& f : m_allFolders) {
        if (f.relativePath == rel || f.relativePath == parentRel
            || f.relativePath.startsWith(rel + QLatin1Char('/')))
            continue;
        move.children << HomeMenuItem::act(QStringLiteral("folder"), f.name,
                                           [this, rel, dst = f.relativePath] {
                                               PendingMoveTarget t;
                                               t.folderRels = {rel};
                                               t.label = QFileInfo(rel).fileName();
                                               executeMoveTarget(t, dst, true);
                                           });
        if (++added >= 5)
            break;
    }
    if (!move.children.isEmpty())
        move.children << HomeMenuItem::sep();
    move.children << HomeMenuItem::act(QStringLiteral("folderMove"), QStringLiteral("Elegir carpeta…"),
                                       [this, rel, name = meta.name, parentRel] {
                                           openMoveSheetFor({}, {rel}, name, parentRel);
                                       });
    items << move;

    const bool fav = LibraryStore::isFolderFavorite(rel);
    items << HomeMenuItem::act(
        QStringLiteral("star"),
        fav ? QStringLiteral("Quitar de favoritos") : QStringLiteral("Añadir a favoritos"),
        [this, rel, nextFav = !fav] {
            LibraryStore::setFolderFavorite(rel, nextFav);
            refreshLibrary();
        },
        QStringLiteral("Ctrl B"));

    items << HomeMenuItem::sep();
    HomeMenuItem trash = HomeMenuItem::act(
        QStringLiteral("trash"), QStringLiteral("Mover a la papelera"),
        [this, guard, rel, name = meta.name] {
            moveToTrash(LibraryStore::resolveDirectory(rel), name, true, guard.data());
        },
        QStringLiteral("Supr"));
    trash.danger = true;
    items << trash;
    m_menu->popup(mapFromGlobal(globalPos), items);
}

void HomeScreen::showTrashMenu(TrashCard* card, const QPoint& globalPos) {
    if (!card || !m_menu)
        return;
    const TrashEntry entry = card->entry();
    QVector<HomeMenuItem> items;
    items << HomeMenuItem::act(QStringLiteral("restore"), QStringLiteral("Restaurar"),
                               [this, id = entry.id] { restoreFromTrash(id); });
    items << HomeMenuItem::sep();
    HomeMenuItem del = HomeMenuItem::act(QStringLiteral("trash"), QStringLiteral("Eliminar para siempre…"),
                                         [this, entry] { deleteForever(entry); });
    del.danger = true;
    items << del;
    m_menu->popup(mapFromGlobal(globalPos), items);
}

void HomeScreen::showEmptySpaceMenu(const QPoint& globalPos) {
    if (!m_menu)
        return;
    const QString viewName = (m_viewMode == ViewMode::Folder) ? QFileInfo(m_currentFolder).fileName()
                           : (m_viewMode == ViewMode::Favoritos) ? QStringLiteral("Favoritos")
                           : (m_viewMode == ViewMode::FoldersRoot) ? QStringLiteral("Carpetas")
                           : QStringLiteral("Documentos");
    QVector<HomeMenuItem> items;
    items << HomeMenuItem::header(viewName);
    items << HomeMenuItem::act(QStringLiteral("grid"), QStringLiteral("Seleccionar todo"),
                               [this] { selectAllVisible(); }, QStringLiteral("Ctrl A"));
    items << HomeMenuItem::sep();

    HomeMenuItem sortMenu = HomeMenuItem::act(QStringLiteral("sort"), QStringLiteral("Ordenar por"), {});
    auto addSort = [this, &sortMenu](const QString& label, int s) {
        sortMenu.children << HomeMenuItem::check(
            label, m_filter.sort == s, QColor(),
            [this, s](bool) {
                LibraryFilter f = m_filter;
                f.sort = s;
                setLibraryFilter(f);
            },
            false);
    };
    addSort(QStringLiteral("Recientes"), LibraryFilter::Recent);
    addSort(QStringLiteral("Nombre"), LibraryFilter::Name);
    addSort(QStringLiteral("Creación"), LibraryFilter::Created);
    items << sortMenu;

    items << HomeMenuItem::act(QStringLiteral("upload"), QStringLiteral("Importar PDF…"), [this] {
        const QString folder = (m_viewMode == ViewMode::Folder) ? m_currentFolder : QString();
        emit importPdfFromHomeRequested(folder);
    });
    m_menu->popup(mapFromGlobal(globalPos), items);
}

void HomeScreen::showMultiSelectMenu(const QPoint& globalPos) {
    if (!m_menu || m_selectedKeys.isEmpty())
        return;

    QStringList docPaths;
    QStringList folderRels;
    bool anyNotFav = false;
    for (FolderCard* fc : m_folderCards) {
        if (fc && m_selectedKeys.contains(keyForFolder(fc->meta().relativePath))) {
            folderRels.append(fc->meta().relativePath);
            if (!LibraryStore::isFolderFavorite(fc->meta().relativePath))
                anyNotFav = true;
        }
    }
    for (NotebookCard* nc : m_cards) {
        if (nc && m_selectedKeys.contains(keyForDoc(nc->filePath()))) {
            docPaths.append(nc->filePath());
            if (!LibraryStore::isDocumentFavorite(nc->filePath()))
                anyNotFav = true;
        }
    }
    const int count = int(docPaths.size() + folderRels.size());
    if (count == 0)
        return;

    QVector<HomeMenuItem> items;
    items << HomeMenuItem::header(QStringLiteral("%1 elementos").arg(count));
    if (!docPaths.isEmpty()) {
        items << HomeMenuItem::act(QStringLiteral("open"), QStringLiteral("Abrir en pestañas"),
                                   [this, docPaths] { emit openNotebooksInTabsRequested(docPaths); });
        items << HomeMenuItem::sep();
    }
    items << HomeMenuItem::act(QStringLiteral("folderMove"), QStringLiteral("Mover a…"),
                               [this, docPaths, folderRels, count] {
                                   openMoveSheetFor(docPaths, folderRels,
                                                    QStringLiteral("%1 elementos").arg(count), QString());
                               });
    items << HomeMenuItem::act(
        QStringLiteral("star"),
        anyNotFav ? QStringLiteral("Añadir a favoritos") : QStringLiteral("Quitar de favoritos"),
        [this] { toggleFavoriteForSelection(); });

    if (!docPaths.isEmpty()) {
        HomeMenuItem tagsItem = HomeMenuItem::act(QStringLiteral("tag"), QStringLiteral("Etiquetas"), {});
        const QStringList firstTags = LibraryStore::documentTags(docPaths.first());
        tagsItem.children = buildTagSubmenu(
            m_allTags, firstTags,
            [this, docPaths](const QString& tagId, bool on) {
                for (const QString& p : docPaths)
                    LibraryStore::setDocumentTag(p, tagId, on);
                refreshLibrary();
            },
            [this, docPaths] { promptNewTag(docPaths); });
        items << tagsItem;

        items << HomeMenuItem::act(QStringLiteral("export"), QStringLiteral("Exportar como PDF"),
                                   [this, docPaths] { exportNotebooksToPdf(docPaths); });
    }
    items << HomeMenuItem::sep();
    HomeMenuItem trash = HomeMenuItem::act(
        QStringLiteral("trash"), QStringLiteral("Mover a la papelera"),
        [this] { moveSelectionToTrash(); }, QStringLiteral("Supr"));
    trash.danger = true;
    items << trash;
    m_menu->popup(mapFromGlobal(globalPos), items);
}

void HomeScreen::showTagMenu(const TagInfo& tag, const QPoint& globalPos) {
    if (!m_menu)
        return;
    QVector<HomeMenuItem> items;
    items << HomeMenuItem::act(QStringLiteral("pencil"), QStringLiteral("Renombrar etiqueta"),
                               [this, tag] { promptRenameTag(tag); });
    items << HomeMenuItem::header(QStringLiteral("Color"));
    HomeMenuItem swatches;
    swatches.kind = HomeMenuItem::Kind::Swatches;
    swatches.swatchCurrent = tag.colorIndex;
    swatches.swatchPicked = [this, tid = tag.id](int color) {
        LibraryStore::setTagColor(tid, color);
        refreshLibrary();
    };
    items << swatches;
    items << HomeMenuItem::sep();
    HomeMenuItem del = HomeMenuItem::act(QStringLiteral("trash"), QStringLiteral("Borrar etiqueta"),
                                         [this, tag] { deleteTagWithUndo(tag); });
    del.danger = true;
    items << del;
    m_menu->popup(mapFromGlobal(globalPos), items);
}

// ------------------------------------------------------------ acciones

void HomeScreen::openMoveSheetFor(const QStringList& docPaths, const QStringList& folderRels,
                                  const QString& titleLabel, const QString& currentRel) {
    if (!m_moveSheet)
        return;
    m_pendingMove = PendingMoveTarget{docPaths, folderRels, titleLabel, currentRel};
    m_moveSheet->openMove(titleLabel, currentRel, m_allFolders, homePaletteFor(m_dark), m_dark);
}

void HomeScreen::executeMoveTarget(const PendingMoveTarget& target, const QString& destRel,
                                   bool showUndoToast) {
    struct MovedDoc { QString newPath; QString oldAbsDir; };
    struct MovedFolder { QString newRel; QString oldParentRel; QString oldAbs; QString newAbs; };
    QVector<MovedDoc> movedDocs;
    QVector<MovedFolder> movedFolders;
    const QHash<QString, QPoint> beforePos = captureCardPositions();

    for (const QString& path : target.docPaths) {
        const QString oldDir = QFileInfo(path).absolutePath();
        QString newPath, err;
        if (LibraryStore::moveNotebookToFolder(path, destRel, &newPath, &err)) {
            if (newPath != path)
                emit notebookMoved(path, newPath);
            movedDocs.append({newPath, oldDir});
        } else if (!err.isEmpty()) {
            showError(err);
        }
    }

    for (const QString& rel : target.folderRels) {
        const QString oldParent = QFileInfo(rel).path() == QLatin1String(".") ? QString() : QFileInfo(rel).path();
        QString newRel, oldAbs, newAbs, err;
        if (LibraryStore::moveFolder(rel, destRel, &newRel, &oldAbs, &newAbs, &err)) {
            if (m_currentFolder == rel)
                m_currentFolder = newRel;
            else if (m_currentFolder.startsWith(rel + QLatin1Char('/')))
                m_currentFolder = newRel + m_currentFolder.mid(rel.size());
            if (oldAbs != newAbs)
                emit folderRenamed(oldAbs, newAbs);
            movedFolders.append({newRel, oldParent, oldAbs, newAbs});
        } else if (!err.isEmpty()) {
            showError(err);
        }
    }

    if (movedDocs.isEmpty() && movedFolders.isEmpty())
        return;

    if (!destRel.isEmpty())
        LibraryStore::recordRecentMoveFolder(destRel);
    refreshLibraryWithFlip(beforePos, 20);

    if (showUndoToast && m_toast) {
        const QString where = destRel.isEmpty() ? QStringLiteral("Documentos") : QFileInfo(destRel).fileName();
        m_toast->showMessage(
            QStringLiteral("Movido a «%1»").arg(where), QStringLiteral("Deshacer"),
            [this, movedDocs, movedFolders] {
                const auto bPos = captureCardPositions();
                for (const auto& d : movedDocs) {
                    QString back, err2;
                    if (LibraryStore::moveNotebookToDirectory(d.newPath, d.oldAbsDir, &back, &err2))
                        emit notebookMoved(d.newPath, back);
                }
                for (const auto& f : movedFolders) {
                    QString backRel, oldA, newA, err2;
                    if (LibraryStore::moveFolder(f.newRel, f.oldParentRel, &backRel, &oldA, &newA, &err2))
                        emit folderRenamed(oldA, newA);
                }
                refreshLibraryWithFlip(bPos, 20);
            },
            6000, QStringLiteral("folderMove"), QStringLiteral("Ctrl Z"));
    }
}

void HomeScreen::renameNotebookTo(const QString& oldPath, const QString& name, NotebookCard* card) {
    const QString clean = LibraryStore::sanitizeBaseName(name);
    if (!clean.isEmpty()) {
        const QString target = QDir(QFileInfo(oldPath).absolutePath()).filePath(clean + QStringLiteral(".finily"));
        if (target != oldPath && QFileInfo::exists(target)) {
            if (card)
                card->setRenameError(QStringLiteral("Ya existe un documento llamado «%1»").arg(clean));
            else
                showError(QStringLiteral("Ya existe un documento con ese nombre."));
            return;
        }
    }
    QString newPath, err;
    if (!LibraryStore::renameNotebook(oldPath, name, &newPath, &err)) {
        showError(err.isEmpty() ? QStringLiteral("No se pudo renombrar.") : err);
        refreshLibrary();
        return;
    }
    if (newPath != oldPath)
        emit notebookRenamed(oldPath, newPath);
    refreshLibrary();
}

void HomeScreen::renameFolderTo(const QString& relPath, const QString& name, FolderCard* card) {
    const QString clean = LibraryStore::sanitizeBaseName(name);
    if (!clean.isEmpty()) {
        const QString parentRel = QFileInfo(relPath).path() == QLatin1String(".") ? QString() : QFileInfo(relPath).path();
        const QString parentAbs = LibraryStore::resolveDirectory(parentRel);
        const QString oldAbs = LibraryStore::resolveDirectory(relPath);
        const QString targetAbs = QDir(parentAbs).filePath(clean);
        if (targetAbs != oldAbs && QFileInfo::exists(targetAbs)) {
            if (card)
                card->setRenameError(QStringLiteral("Ya existe una carpeta llamada «%1»").arg(clean));
            else
                showError(QStringLiteral("Ya existe una carpeta con ese nombre."));
            return;
        }
    }
    QString newRel, oldAbs, newAbs, err;
    if (!LibraryStore::renameFolder(relPath, name, &newRel, &oldAbs, &newAbs, &err)) {
        showError(err.isEmpty() ? QStringLiteral("No se pudo renombrar la carpeta.") : err);
        refreshLibrary();
        return;
    }
    if (newRel != relPath) {
        if (m_currentFolder == relPath)
            m_currentFolder = newRel;
        else if (m_currentFolder.startsWith(relPath + QLatin1Char('/')))
            m_currentFolder = newRel + m_currentFolder.mid(relPath.size());
        emit folderRenamed(oldAbs, newAbs);
    }
    refreshLibrary();
}

void HomeScreen::duplicateNotebooks(const QStringList& paths) {
    const auto beforePos = captureCardPositions();
    int count = 0;
    QString lastNew;
    for (const QString& path : paths) {
        QString out, err;
        if (LibraryStore::duplicateNotebook(path, &out, &err)) {
            ++count;
            lastNew = out;
        } else if (!err.isEmpty()) {
            showError(err);
        }
    }
    if (count > 0) {
        refreshLibraryWithFlip(beforePos, 20);
        if (NotebookCard* nc = notebookCardFor(lastNew))
            nc->playPopIn();
        if (m_toast) {
            m_toast->showMessage(
                count == 1 ? QStringLiteral("Se duplicó «%1»").arg(QFileInfo(lastNew).completeBaseName())
                           : QStringLiteral("Se duplicaron %1 documentos").arg(count),
                {}, {}, 3500, QStringLiteral("copy"));
        }
    }
}

void HomeScreen::toggleFavoriteForCard(ObjectCard* card) {
    if (!card)
        return;
    if (auto* fc = qobject_cast<FolderCard*>(card)) {
        const bool next = !LibraryStore::isFolderFavorite(fc->meta().relativePath);
        LibraryStore::setFolderFavorite(fc->meta().relativePath, next);
    } else if (auto* nc = qobject_cast<NotebookCard*>(card)) {
        const bool next = !LibraryStore::isDocumentFavorite(nc->filePath());
        LibraryStore::setDocumentFavorite(nc->filePath(), next);
    }
    refreshLibrary();
}

void HomeScreen::toggleFavoriteForSelection() {
    bool anyNotFav = false;
    QStringList docPaths;
    QStringList folderRels;
    for (FolderCard* fc : m_folderCards) {
        if (fc && m_selectedKeys.contains(keyForFolder(fc->meta().relativePath))) {
            folderRels.append(fc->meta().relativePath);
            if (!LibraryStore::isFolderFavorite(fc->meta().relativePath))
                anyNotFav = true;
        }
    }
    for (NotebookCard* nc : m_cards) {
        if (nc && m_selectedKeys.contains(keyForDoc(nc->filePath()))) {
            docPaths.append(nc->filePath());
            if (!LibraryStore::isDocumentFavorite(nc->filePath()))
                anyNotFav = true;
        }
    }
    for (const QString& r : folderRels)
        LibraryStore::setFolderFavorite(r, anyNotFav);
    for (const QString& p : docPaths)
        LibraryStore::setDocumentFavorite(p, anyNotFav);
    refreshLibrary();
}

void HomeScreen::exportNotebooksToPdf(const QStringList& paths) {
    if (paths.isEmpty())
        return;
    if (paths.size() == 1) {
        const QString base = QFileInfo(paths.first()).completeBaseName();
        const QString dest = QFileDialog::getSaveFileName(
            this, QStringLiteral("Exportar como PDF"),
            QDir::home().filePath(base + QStringLiteral(".pdf")),
            QStringLiteral("Documentos PDF (*.pdf)"));
        if (dest.isEmpty())
            return;
        QString err;
        if (!LibraryStore::exportNotebookToPdf(paths.first(), dest, &err)) {
            showError(err.isEmpty() ? QStringLiteral("No se pudo exportar a PDF.") : err);
            return;
        }
        if (m_toast)
            m_toast->showMessage(QStringLiteral("PDF exportado: «%1»").arg(QFileInfo(dest).fileName()),
                                 {}, {}, 4500, QStringLiteral("export"));
        return;
    }

    const QString destDir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Elegir carpeta para exportar %1 PDF").arg(paths.size()),
        QDir::homePath());
    if (destDir.isEmpty())
        return;
    int exported = 0;
    for (const QString& p : paths) {
        const QString out = QDir(destDir).filePath(QFileInfo(p).completeBaseName() + QStringLiteral(".pdf"));
        if (LibraryStore::exportNotebookToPdf(p, out))
            ++exported;
    }
    if (m_toast)
        m_toast->showMessage(QStringLiteral("Exportados %1 de %2 PDF").arg(exported).arg(paths.size()),
                             {}, {}, 4500, QStringLiteral("export"));
}

void HomeScreen::moveNotebook(const QString& filePath, const QString& targetRelativeFolder) {
    PendingMoveTarget t;
    t.docPaths = {filePath};
    t.label = QFileInfo(filePath).completeBaseName();
    executeMoveTarget(t, targetRelativeFolder, true);
}

// Sin confirmación: el aviso ofrece «Deshacer» durante 6 s (DESIGN.md, principio 7)
// y la miniatura vuela en curva hasta Papelera (mockup 11, escena 05).
void HomeScreen::moveToTrash(const QString& absolutePath, const QString& displayName,
                             bool isFolder, ObjectCard* sourceCard) {
    ObjectCard* card = sourceCard;
    if (!card) {
        if (isFolder) {
            for (FolderCard* fc : m_folderCards) {
                if (fc && LibraryStore::resolveDirectory(fc->meta().relativePath) == absolutePath) {
                    card = fc;
                    break;
                }
            }
        } else {
            card = notebookCardFor(absolutePath);
        }
    }

    QPixmap snap;
    QRectF startRect;
    if (card && card->isVisible()) {
        snap = card->grabVisualPixmap();
        const QRect vr = card->visualRectInCard();
        startRect = QRectF(card->mapTo(this, vr.topLeft()), vr.size());
    }
    const QHash<QString, QPoint> beforePos = captureCardPositions();

    QString id, err;
    if (!LibraryStore::moveToTrash(absolutePath, &id, &err)) {
        showError(err.isEmpty() ? QStringLiteral("No se pudo mover a la papelera.") : err);
        return;
    }
    if (isFolder)
        emit folderDeleted(absolutePath);
    else
        emit notebookDeleted(absolutePath);

    refreshLibraryWithFlip(beforePos, 25);

    const QPointF trashCenter = m_sidebar && m_sidebar->isVisible()
        ? QPointF(m_sidebar->trashIconCenterIn(this))
        : QPointF(30.0, height() - 60.0);

    const QPointer<HomeScreen> guard(this);
    TrashFlyOverlay::play(this, snap, startRect, trashCenter, false, [guard] {
        if (guard && guard->m_sidebar)
            guard->m_sidebar->bounceTrashIcon();
    });

    if (m_toast) {
        const QString msg = isFolder
            ? QStringLiteral("«%1» y su contenido se movieron a la papelera").arg(displayName)
            : QStringLiteral("«%1» se movió a la papelera").arg(displayName);
        m_toast->showMessage(msg, QStringLiteral("Deshacer"), [this, id] { restoreFromTrash(id); },
                             6000, QStringLiteral("trash"), QStringLiteral("Ctrl Z"));
    }
}

void HomeScreen::moveSelectionToTrash() {
    if (m_selectedKeys.isEmpty())
        return;
    QStringList folderRels;
    QStringList docPaths;
    for (FolderCard* fc : m_folderCards) {
        if (fc && m_selectedKeys.contains(keyForFolder(fc->meta().relativePath)))
            folderRels.append(fc->meta().relativePath);
    }
    for (NotebookCard* nc : m_cards) {
        if (nc && m_selectedKeys.contains(keyForDoc(nc->filePath())))
            docPaths.append(nc->filePath());
    }
    const int total = int(folderRels.size() + docPaths.size());
    if (total == 1) {
        if (folderRels.size() == 1)
            moveToTrash(LibraryStore::resolveDirectory(folderRels.first()),
                        QFileInfo(folderRels.first()).fileName(), true, folderCardFor(folderRels.first()));
        else
            moveToTrash(docPaths.first(), QFileInfo(docPaths.first()).completeBaseName(),
                        false, notebookCardFor(docPaths.first()));
        return;
    }

    const QHash<QString, QPoint> beforePos = captureCardPositions();
    QStringList trashedIds;
    for (const QString& rel : folderRels) {
        const QString abs = LibraryStore::resolveDirectory(rel);
        QString id;
        if (LibraryStore::moveToTrash(abs, &id)) {
            emit folderDeleted(abs);
            trashedIds.append(id);
        }
    }
    for (const QString& p : docPaths) {
        QString id;
        if (LibraryStore::moveToTrash(p, &id)) {
            emit notebookDeleted(p);
            trashedIds.append(id);
        }
    }
    m_selectedKeys.clear();
    refreshLibraryWithFlip(beforePos, 20);
    if (m_sidebar)
        m_sidebar->bounceTrashIcon();
    if (!trashedIds.isEmpty() && m_toast) {
        m_toast->showMessage(
            QStringLiteral("Se movieron %1 elementos a la papelera").arg(trashedIds.size()),
            QStringLiteral("Deshacer"),
            [this, trashedIds] {
                const auto bPos = captureCardPositions();
                for (const QString& id : trashedIds)
                    LibraryStore::restoreFromTrash(id);
                refreshLibraryWithFlip(bPos, 20);
            },
            6000, QStringLiteral("trash"), QStringLiteral("Ctrl Z"));
    }
}

void HomeScreen::restoreFromTrash(const QString& id) {
    const QHash<QString, QPoint> beforePos = captureCardPositions();
    QString restored, err;
    if (!LibraryStore::restoreFromTrash(id, &restored, &err)) {
        showError(err.isEmpty() ? QStringLiteral("No se pudo restaurar.") : err);
        return;
    }
    m_pendingFlyRestorePath = restored;
    refreshLibraryWithFlip(beforePos, 20);

    if (NotebookCard* card = notebookCardFor(restored)) {
        const QPixmap snap = card->grabVisualPixmap();
        const QRect vr = card->visualRectInCard();
        const QRectF endRect(card->mapTo(this, vr.topLeft()), vr.size());
        const QPointF trashCenter = m_sidebar && m_sidebar->isVisible()
            ? QPointF(m_sidebar->trashIconCenterIn(this))
            : QPointF(30.0, height() - 60.0);
        const QPointer<NotebookCard> cGuard(card);
        TrashFlyOverlay::play(this, snap, endRect, trashCenter, true, [cGuard] {
            if (cGuard) {
                cGuard->setProperty("finilyHideUntilLanded", false);
                cGuard->show();
            }
        });
    }

    if (m_viewMode == ViewMode::Trash && m_toast) {
        m_toast->showMessage(QStringLiteral("«%1» se restauró").arg(QFileInfo(restored).completeBaseName()),
                             {}, {}, 3500, QStringLiteral("restore"));
    }
}

void HomeScreen::promptRestoreFromTrash(const TrashEntry& entry) {
    if (!m_confirm)
        return;
    m_confirm->ask(QStringLiteral("restore"),
                   QStringLiteral("¿Restaurar «%1»?").arg(entry.name),
                   entry.originalRelative.isEmpty()
                       ? QStringLiteral("Volverá a Documentos.")
                       : QStringLiteral("Volverá a «%1».").arg(entry.originalRelative),
                   QStringLiteral("Restaurar"), [this, id = entry.id] { restoreFromTrash(id); });
}

void HomeScreen::deleteForever(const TrashEntry& entry) {
    m_confirm->ask(QStringLiteral("trash"), QStringLiteral("¿Eliminar para siempre?"),
                   QStringLiteral("«%1» se eliminará definitivamente. Esta acción no se puede deshacer.")
                       .arg(entry.name),
                   QStringLiteral("Eliminar"), [this, id = entry.id] {
                       QString err;
                       if (!LibraryStore::deleteFromTrash(id, &err))
                           showError(err.isEmpty() ? QStringLiteral("No se pudo eliminar.") : err);
                       refreshLibrary();
                   });
}

void HomeScreen::askEmptyTrash() {
    const int n = int(LibraryStore::listTrash().size());
    if (n == 0)
        return;
    m_confirm->ask(QStringLiteral("trash"), QStringLiteral("¿Vaciar la papelera?"),
                   n == 1 ? QStringLiteral("Se eliminará para siempre 1 elemento. Esta acción no se puede deshacer.")
                          : QStringLiteral("Se eliminarán para siempre %1 elementos. Esta acción no se puede deshacer.")
                                .arg(n),
                   QStringLiteral("Vaciar"), [this] {
                       LibraryStore::emptyTrash();
                       refreshLibrary();
                   });
}

void HomeScreen::promptNewTag(const QStringList& attachDocPaths) {
    if (!m_tagPrompt)
        m_tagPrompt = new TagPromptOverlay(this);
    m_tagPrompt->ask(QStringLiteral("Nueva etiqueta"), QString(), QStringLiteral("Crear"), m_dark,
                     [this, attachDocPaths](const QString& name) {
                         const TagInfo tag = LibraryStore::createTag(name);
                         if (tag.id.isEmpty())
                             return;
                         for (const QString& p : attachDocPaths)
                             LibraryStore::setDocumentTag(p, tag.id, true);
                         refreshLibrary();
                     });
}

void HomeScreen::promptRenameTag(const TagInfo& tag) {
    if (!m_tagPrompt)
        m_tagPrompt = new TagPromptOverlay(this);
    m_tagPrompt->ask(QStringLiteral("Renombrar etiqueta"), tag.name, QStringLiteral("Guardar"), m_dark,
                     [this, tid = tag.id](const QString& nextName) {
                         LibraryStore::renameTag(tid, nextName);
                         refreshLibrary();
                     });
}

void HomeScreen::deleteTagWithUndo(const TagInfo& tag) {
    const QStringList affectedDocs = LibraryStore::deleteTag(tag.id);
    if (m_viewMode == ViewMode::Tag && m_currentTagId == tag.id) {
        m_viewMode = ViewMode::Todos;
        m_currentTagId.clear();
    }
    refreshLibrary();
    if (m_toast) {
        m_toast->showMessage(
            QStringLiteral("Se borró la etiqueta «%1»").arg(tag.name),
            QStringLiteral("Deshacer"),
            [this, tag, affectedDocs] {
                LibraryStore::restoreTag(tag, affectedDocs);
                refreshLibrary();
            },
            6000, QStringLiteral("tag"), QStringLiteral("Ctrl Z"));
    }
}

void HomeScreen::startPdfImport(const QString& pdfPath, const QString& folderRelativePath) {
    if (pdfPath.isEmpty())
        return;
    const QString base = LibraryStore::sanitizeBaseName(QFileInfo(pdfPath).completeBaseName());
    const QString safeBase = base.isEmpty() ? QStringLiteral("PDF importado") : base;
    const QString targetDir = LibraryStore::resolveDirectory(folderRelativePath);
    if (targetDir.isEmpty()) {
        showError(QStringLiteral("No se encontró la carpeta de destino."));
        return;
    }

    auto* placeholder = new ImportingCard(safeBase, m_gridContent);
    placeholder->setDark(m_dark);
    m_importingCards.append(placeholder);
    updateHeaderAndEmptyState();
    relayoutGrid();

    if (m_pdfCancelFlag)
        m_pdfCancelFlag->store(true);
    m_pdfCancelFlag = std::make_shared<std::atomic_bool>(false);
    const auto cancelToken = m_pdfCancelFlag;

    if (m_toast) {
        m_toast->showProgress(QStringLiteral("pdf"),
                              QStringLiteral("Importando «%1»").arg(QFileInfo(pdfPath).fileName()),
                              0, 1, [cancelToken] { cancelToken->store(true); });
    }

    const QPointer<HomeScreen> selfGuard(this);
    const QPointer<ImportingCard> phGuard(placeholder);
    const QString fileLabel = QFileInfo(pdfPath).fileName();

    std::thread([selfGuard, phGuard, cancelToken, pdfPath, targetDir, safeBase, fileLabel] {
        QPdfDocument doc;
        const auto err = doc.load(pdfPath);
        if (err != QPdfDocument::Error::None || doc.pageCount() <= 0) {
            QMetaObject::invokeMethod(qApp, [selfGuard, phGuard] {
                if (!selfGuard)
                    return;
                if (phGuard) {
                    selfGuard->m_importingCards.removeOne(phGuard.data());
                    phGuard->deleteLater();
                }
                if (selfGuard->m_toast)
                    selfGuard->m_toast->showMessage(QStringLiteral("No se pudo importar el PDF (archivo protegido o dañado)"),
                                                    {}, {}, 5000, QStringLiteral("alert"));
                selfGuard->updateHeaderAndEmptyState();
                selfGuard->relayoutGrid();
            }, Qt::QueuedConnection);
            return;
        }

        const int total = doc.pageCount();
        QVector<ImageItem> pages;
        pages.reserve(total);
        for (int i = 0; i < total; ++i) {
            if (cancelToken->load())
                break;
            const QSizeF points = doc.pagePointSize(i);
            if (points.width() > 0 && points.height() > 0
                && isFinite(points.width()) && isFinite(points.height())) {
                const double worldScale = 1000.0 / points.width();
                const QSizeF world = points * worldScale;
                const double pixelBudget = 256.0 * 1024.0 * 1024.0 / (4.0 * std::max(1, total));
                const double rasterScale = std::min({
                    2.0 * worldScale,
                    2200.0 / std::max(points.width(), points.height()),
                    std::sqrt(pixelBudget / (points.width() * points.height())),
                });
                if (isFinite(rasterScale) && rasterScale > 0.0) {
                    const QSize raster(std::max(1, int(std::ceil(points.width() * rasterScale))),
                                       std::max(1, int(std::ceil(points.height() * rasterScale))));
                    QImage rendered = doc.render(i, raster);
                    if (!rendered.isNull()) {
                        QImage image(rendered.size(), QImage::Format_ARGB32_Premultiplied);
                        image.fill(Qt::white);
                        {
                            QPainter bg(&image);
                            bg.drawImage(0, 0, rendered);
                        }
                        ImageItem page;
                        page.id = freshId();
                        page.rect = QRectF(-world.width() / 2.0, -world.height() / 2.0,
                                           world.width(), world.height());
                        page.image = image;
                        page.isPdfPage = true;
                        page.pdfPageIndex = i;
                        page.pdfSourcePath = QFileInfo(pdfPath).absoluteFilePath();
                        pages.append(page);
                    }
                }
            }
            const int done = i + 1;
            QMetaObject::invokeMethod(qApp, [selfGuard, phGuard, cancelToken, fileLabel, done, total] {
                if (!selfGuard || cancelToken->load())
                    return;
                if (phGuard)
                    phGuard->setStatus(QStringLiteral("%1 de %2").arg(done).arg(total));
                if (selfGuard->m_toast) {
                    selfGuard->m_toast->showProgress(
                        QStringLiteral("pdf"),
                        QStringLiteral("Importando «%1»").arg(fileLabel),
                        done, total, [cancelToken] { cancelToken->store(true); });
                }
            }, Qt::QueuedConnection);
        }

        if (cancelToken->load() || pages.isEmpty()) {
            QMetaObject::invokeMethod(qApp, [selfGuard, phGuard] {
                if (!selfGuard)
                    return;
                if (phGuard) {
                    selfGuard->m_importingCards.removeOne(phGuard.data());
                    phGuard->deleteLater();
                }
                if (selfGuard->m_toast)
                    selfGuard->m_toast->showMessage(QStringLiteral("Importación cancelada"),
                                                    {}, {}, 2500, QStringLiteral("pdf"));
                selfGuard->updateHeaderAndEmptyState();
                selfGuard->relayoutGrid();
            }, Qt::QueuedConnection);
            return;
        }

        QDir().mkpath(targetDir);
        const QString nbPath = LibraryStore::uniqueNotebookPath(targetDir, safeBase);
        NotebookDocument nbDoc;
        nbDoc.configureBlank(ThemeId::PaperLight, BackgroundTemplate::Blank, PageFormat::Infinite,
                             1.0, CoverStyle{-1, 0});
        nbDoc.insertPdfPages(pages, true);
        const bool saved = nbDoc.saveToFile(nbPath, NotebookDocument::DocumentToolSettings{}, true);

        QMetaObject::invokeMethod(qApp, [selfGuard, phGuard, saved, nbPath, safeBase] {
            if (!selfGuard)
                return;
            if (phGuard) {
                selfGuard->m_importingCards.removeOne(phGuard.data());
                phGuard->deleteLater();
            }
            if (!saved) {
                if (selfGuard->m_toast)
                    selfGuard->m_toast->showMessage(QStringLiteral("No se pudo guardar el PDF importado"),
                                                    {}, {}, 5000, QStringLiteral("alert"));
                selfGuard->updateHeaderAndEmptyState();
                selfGuard->relayoutGrid();
                return;
            }
            selfGuard->refreshLibrary();
            if (selfGuard->m_toast) {
                selfGuard->m_toast->showMessage(
                    QStringLiteral("«%1» importado").arg(safeBase),
                    QStringLiteral("Abrir"),
                    [selfGuard, nbPath] {
                        if (selfGuard)
                            emit selfGuard->openNotebookRequested(nbPath);
                    },
                    6000, QStringLiteral("pdf"));
            }
        }, Qt::QueuedConnection);
    }).detach();
}

void HomeScreen::importExternalImageFile(const QString& imgPath, const QString& folderRelativePath) {
    QImage img(imgPath);
    if (img.isNull()) {
        showError(QStringLiteral("No se pudo leer la imagen."));
        return;
    }
    const QString base = LibraryStore::sanitizeBaseName(QFileInfo(imgPath).completeBaseName());
    const QString safeBase = base.isEmpty() ? QStringLiteral("Imagen") : base;
    const QString targetDir = LibraryStore::resolveDirectory(folderRelativePath);
    if (targetDir.isEmpty())
        return;
    QDir().mkpath(targetDir);
    const QString nbPath = LibraryStore::uniqueNotebookPath(targetDir, safeBase);

    const double maxDim = 960.0;
    const double scale = std::min(1.0, maxDim / std::max(1, std::max(img.width(), img.height())));
    const QSizeF worldSize(img.width() * scale, img.height() * scale);

    PageData page;
    ImageItem item;
    item.id = freshId();
    item.image = img;
    item.rect = QRectF(-worldSize.width() / 2.0, -worldSize.height() / 2.0,
                       worldSize.width(), worldSize.height());
    page.images.append(item);

    const auto settings = documentSettings(ShapeKind::Triangle, true, true, false,
                                           PressureProfile::Light, ThemeId::PaperLight,
                                           BackgroundTemplate::Blank, Viewport{}, PageFormat::Infinite);
    if (!serializeDocumentToFile(nbPath, {page}, 0, settings, true)) {
        showError(QStringLiteral("No se pudo guardar la imagen importada."));
        return;
    }
    updateCoverThumbnailForPages(nbPath, {page}, ThemeId::PaperLight, BackgroundTemplate::Blank,
                                 PageFormat::Infinite, 1.0);
    refreshLibrary();
    if (m_toast) {
        m_toast->showMessage(QStringLiteral("«%1» importado").arg(safeBase),
                             QStringLiteral("Abrir"),
                             [this, nbPath] { emit openNotebookRequested(nbPath); },
                             6000, QStringLiteral("docs"));
    }
}

void HomeScreen::showError(const QString& text) {
    if (m_toast)
        m_toast->showMessage(text, {}, {}, 5000, QStringLiteral("alert"));
}

ObjectCard* HomeScreen::hoveredCard() const {
    for (FolderCard* fc : m_folderCards)
        if (fc && fc->isVisible() && fc->underMouse())
            return fc;
    for (NotebookCard* c : m_cards)
        if (c && c->isVisible() && c->underMouse())
            return c;
    return nullptr;
}

FolderCard* HomeScreen::folderCardFor(const QString& relativePath) const {
    for (FolderCard* fc : m_folderCards)
        if (fc && fc->meta().relativePath == relativePath)
            return fc;
    return nullptr;
}

NotebookCard* HomeScreen::notebookCardFor(const QString& filePath) const {
    for (NotebookCard* nc : m_cards)
        if (nc && nc->filePath() == filePath)
            return nc;
    return nullptr;
}

} // namespace finily
