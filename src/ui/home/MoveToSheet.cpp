#include "ui/home/MoveToSheet.h"

#include "ui/DesignTokens.h"
#include "ui/UiIcons.h"
#include "ui/home/SheetWidgets.h"

#include <QApplication>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace finily {
namespace {

constexpr int kRowH = 32;
constexpr int kMaxTreeH = 260;

QFont mvFont(qreal px, QFont::Weight weight) {
    QFont f = QApplication::font();
    f.setPixelSize(qRound(px));
    f.setWeight(weight);
    return f;
}

} // namespace

// Campo de búsqueda pintado con QPainter (.field del mockup 07).
class MoveSearchBox : public QWidget {
public:
    explicit MoveSearchBox(QWidget* parent)
        : QWidget(parent) {
        setFixedHeight(34);
    }
    void setDark(bool d) { m_dark = d; update(); }
    void setFocused(bool f) { m_focused = f; update(); }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const tokens::Palette tk = tokens::paletteFor(m_dark);
        p.setPen(QPen(m_focused ? tk.accent : tk.border, m_focused ? 1.5 : 1.0));
        p.setBrush(m_focused ? tk.surface : tk.field);
        p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 10, 10);
        paintUiIcon(&p, QRectF(10, 10, 14, 14), QStringLiteral("search"), tk.muted);
    }

private:
    bool m_dark = false;
    bool m_focused = false;
};

// Lista de carpetas con sangría, marca de selección y atenuación de la carpeta actual.
class MoveTreeList : public QWidget {
public:
    struct Entry {
        QString relativePath;
        QString name;
        int depth = 0;
        int colorIndex = 0;
        bool isRoot = false;
    };

    explicit MoveTreeList(QWidget* parent)
        : QWidget(parent) {
        setMouseTracking(true);
        setCursor(Qt::PointingHandCursor);
    }

    void setEntries(const QVector<Entry>& entries, const QString& currentRel,
                    const QString& selectedRel, bool hasSelection) {
        m_entries = entries;
        m_currentRel = currentRel;
        m_selectedRel = selectedRel;
        m_hasSelection = hasSelection;
        m_hover = -1;
        setFixedHeight(std::max(kRowH, int(m_entries.size()) * kRowH));
        update();
    }

    void setDark(bool d) { m_dark = d; update(); }

    int rowYFor(const QString& relPath) const {
        for (int i = 0; i < m_entries.size(); ++i) {
            if (m_entries.at(i).relativePath == relPath)
                return i * kRowH;
        }
        return -1;
    }

    std::function<void(const QString&)> onPicked;
    std::function<void(const QString&)> onActivated;

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const tokens::Palette tk = tokens::paletteFor(m_dark);

        if (m_entries.isEmpty()) {
            p.setFont(mvFont(12, QFont::Medium));
            p.setPen(tk.faint);
            p.drawText(rect().adjusted(8, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft,
                       QStringLiteral("Sin carpetas coincidentes"));
            return;
        }

        for (int i = 0; i < m_entries.size(); ++i) {
            const Entry& en = m_entries.at(i);
            const QRectF row(0, i * kRowH, width(), kRowH - 1);
            const bool isCurrent = (en.relativePath == m_currentRel);
            const bool isSelected = (!isCurrent && m_hasSelection && en.relativePath == m_selectedRel);

            if (isSelected || (!isCurrent && i == m_hover)) {
                p.setPen(Qt::NoPen);
                p.setBrush(isSelected ? tk.soft : tk.hover);
                p.drawRoundedRect(row, 8, 8);
            }

            const qreal x = 8.0 + en.depth * 18.0;
            if (en.isRoot) {
                paintUiIcon(&p, QRectF(x, row.center().y() - 7.0, 14, 14),
                            QStringLiteral("docs"), isCurrent ? tk.faint : (isSelected ? tk.text : tk.icon));
            } else {
                QColor dot = tokens::folderColor(en.colorIndex).body2;
                if (isCurrent)
                    dot.setAlpha(135);
                p.setPen(Qt::NoPen);
                p.setBrush(dot);
                p.drawRoundedRect(QRectF(x + 2.0, row.center().y() - 4.5, 9, 9), 3, 3);
            }

            const qreal textX = x + 22.0;
            qreal rightEdge = row.right() - 8.0;
            if (isCurrent) {
                const QFont kf = mvFont(10.5, QFont::Medium);
                const QString hereLabel = QStringLiteral("aquí está");
                const int kw = QFontMetrics(kf).horizontalAdvance(hereLabel);
                p.setFont(kf);
                p.setPen(tk.faint);
                p.drawText(QRectF(rightEdge - kw, row.top(), kw, row.height()),
                           Qt::AlignVCenter | Qt::AlignRight, hereLabel);
                rightEdge -= kw + 8.0;
            } else if (isSelected) {
                paintUiIcon(&p, QRectF(rightEdge - 14.0, row.center().y() - 7.0, 14, 14),
                            QStringLiteral("check"), tk.text);
                rightEdge -= 20.0;
            }

            const QFont tf = mvFont(12.5, isSelected ? QFont::DemiBold : QFont::Medium);
            p.setFont(tf);
            p.setPen(isCurrent ? tk.faint : (isSelected ? tk.text : tk.icon));
            const qreal textW = std::max<qreal>(20.0, rightEdge - textX);
            p.drawText(QRectF(textX, row.top(), textW, row.height()), Qt::AlignVCenter | Qt::AlignLeft,
                       QFontMetrics(tf).elidedText(en.name, Qt::ElideRight, qRound(textW)));
        }
    }

    void mouseMoveEvent(QMouseEvent* e) override {
        const int i = int(e->position().y() / kRowH);
        const int next = (i >= 0 && i < m_entries.size() && m_entries.at(i).relativePath != m_currentRel) ? i : -1;
        if (next != m_hover) {
            m_hover = next;
            setCursor(next >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
            update();
        }
    }

    void leaveEvent(QEvent*) override {
        if (m_hover != -1) {
            m_hover = -1;
            update();
        }
    }

    void mouseReleaseEvent(QMouseEvent* e) override {
        if (e->button() != Qt::LeftButton)
            return;
        const int i = int(e->position().y() / kRowH);
        if (i >= 0 && i < m_entries.size() && m_entries.at(i).relativePath != m_currentRel) {
            if (onPicked)
                onPicked(m_entries.at(i).relativePath);
        }
    }

    void mouseDoubleClickEvent(QMouseEvent* e) override {
        if (e->button() != Qt::LeftButton)
            return;
        const int i = int(e->position().y() / kRowH);
        if (i >= 0 && i < m_entries.size() && m_entries.at(i).relativePath != m_currentRel) {
            if (onActivated)
                onActivated(m_entries.at(i).relativePath);
        }
    }

private:
    QVector<Entry> m_entries;
    QString m_currentRel;
    QString m_selectedRel;
    bool m_hasSelection = false;
    int m_hover = -1;
    bool m_dark = false;
};

// Fila «+ Nueva carpeta aquí» que se expande en un mini campo inline.
class MoveNewFolderBar : public QWidget {
public:
    explicit MoveNewFolderBar(QWidget* parent)
        : QWidget(parent) {
        setFixedHeight(32);
        setMouseTracking(true);
        setCursor(Qt::PointingHandCursor);
    }

    void setExpanded(bool expanded) {
        m_expanded = expanded;
        m_error = false;
        setFixedHeight(expanded ? 34 : 32);
        setCursor(expanded ? Qt::ArrowCursor : Qt::PointingHandCursor);
        update();
    }
    bool isExpanded() const { return m_expanded; }
    void setError(bool err) { m_error = err; update(); }
    void setDark(bool d) { m_dark = d; update(); }

    std::function<void()> onExpandRequested;

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const tokens::Palette tk = tokens::paletteFor(m_dark);

        if (!m_expanded) {
            if (m_hover) {
                p.setPen(Qt::NoPen);
                p.setBrush(tk.hover);
                p.drawRoundedRect(QRectF(rect()), 8, 8);
            }
            paintUiIcon(&p, QRectF(8, (height() - 14) / 2.0, 14, 14),
                        QStringLiteral("folderPlus"), m_hover ? tk.text : tk.muted);
            p.setFont(mvFont(12.5, QFont::Medium));
            p.setPen(m_hover ? tk.text : tk.muted);
            p.drawText(QRectF(30, 0, width() - 38, height()), Qt::AlignVCenter | Qt::AlignLeft,
                       QStringLiteral("Nueva carpeta aquí"));
        } else {
            p.setPen(QPen(m_error ? tk.danger : tk.accent, 1.5));
            p.setBrush(tk.surface);
            p.drawRoundedRect(QRectF(rect()).adjusted(0.75, 0.75, -0.75, -0.75), 10, 10);
            paintUiIcon(&p, QRectF(10, (height() - 14) / 2.0, 14, 14),
                        QStringLiteral("folderPlus"), m_error ? tk.danger : tk.muted);
        }
    }

    void enterEvent(QEnterEvent*) override { m_hover = true; update(); }
    void leaveEvent(QEvent*) override { m_hover = false; update(); }
    void mouseReleaseEvent(QMouseEvent* e) override {
        if (!m_expanded && e->button() == Qt::LeftButton && rect().contains(e->pos()) && onExpandRequested)
            onExpandRequested();
    }

private:
    bool m_expanded = false;
    bool m_error = false;
    bool m_hover = false;
    bool m_dark = false;
};

MoveToSheet::MoveToSheet(QWidget* parent)
    : GlassSheet(parent) {
    setSideCard(true);
    setBodyMargins(0, 0, 0, 0);
    bodyLayout()->setSpacing(0);

    // Cabecera dentro del cuerpo: título + cierre redondo (mockup 07 .fc-head).
    auto* hdr = new QWidget(cardWidget());
    hdr->setAttribute(Qt::WA_StyledBackground, false);
    hdr->setAutoFillBackground(false);
    auto* hdrRow = new QHBoxLayout(hdr);
    hdrRow->setContentsMargins(16, 14, 16, 10);
    hdrRow->setSpacing(8);

    m_titleLabel = new QLabel(QStringLiteral("Mover"), hdr);
    m_titleLabel->setFont(mvFont(14, QFont::DemiBold));
    m_titleLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    hdrRow->addWidget(m_titleLabel, 1, Qt::AlignVCenter);

    m_closeBtn = new QPushButton(hdr);
    m_closeBtn->setFixedSize(24, 24);
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    m_closeBtn->setFocusPolicy(Qt::NoFocus);
    connect(m_closeBtn, &QPushButton::clicked, this, [this] { closeSheet(false); });
    hdrRow->addWidget(m_closeBtn, 0, Qt::AlignVCenter);
    bodyLayout()->addWidget(hdr);

    auto* body = new QWidget(cardWidget());
    body->setAttribute(Qt::WA_StyledBackground, false);
    body->setAutoFillBackground(false);
    auto* col = new QVBoxLayout(body);
    col->setContentsMargins(16, 4, 16, 16);
    col->setSpacing(12);
    bodyLayout()->addWidget(body);

    m_searchBox = new MoveSearchBox(body);
    auto* searchRow = new QHBoxLayout(m_searchBox);
    searchRow->setContentsMargins(32, 0, 10, 0);
    searchRow->setSpacing(0);
    m_searchEdit = new QLineEdit(m_searchBox);
    m_searchEdit->setFrame(false);
    m_searchEdit->setPlaceholderText(QStringLiteral("Buscar carpeta"));
    m_searchEdit->installEventFilter(this);
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this] { rebuildList(); });
    searchRow->addWidget(m_searchEdit);
    col->addWidget(m_searchBox);

    m_scroll = new QScrollArea(body);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->viewport()->setAutoFillBackground(false);
    m_scroll->viewport()->setAttribute(Qt::WA_StyledBackground, false);
    m_tree = new MoveTreeList(m_scroll);
    m_tree->onPicked = [this](const QString& rel) {
        m_selectedRel = rel;
        m_hasSelection = true;
        rebuildList();
        refreshCta();
    };
    m_tree->onActivated = [this](const QString& rel) {
        m_selectedRel = rel;
        m_hasSelection = true;
        onConfirmed();
    };
    m_scroll->setWidget(m_tree);
    col->addWidget(m_scroll);

    m_newFolderBar = new MoveNewFolderBar(body);
    auto* nfRow = new QHBoxLayout(m_newFolderBar);
    nfRow->setContentsMargins(32, 0, 10, 0);
    nfRow->setSpacing(0);
    m_newFolderEdit = new QLineEdit(m_newFolderBar);
    m_newFolderEdit->setFrame(false);
    m_newFolderEdit->setMaxLength(60);
    m_newFolderEdit->setPlaceholderText(QStringLiteral("Nombre de la carpeta"));
    m_newFolderEdit->installEventFilter(this);
    m_newFolderEdit->hide();
    connect(m_newFolderEdit, &QLineEdit::textChanged, this, [this] {
        m_newFolderBar->setError(false);
    });
    connect(m_newFolderEdit, &QLineEdit::returnPressed, this, &MoveToSheet::submitNewFolder);
    nfRow->addWidget(m_newFolderEdit);
    m_newFolderBar->onExpandRequested = [this] { expandNewFolderInput(); };
    col->addWidget(m_newFolderBar);

    m_cta = makeCtaButton(QStringLiteral("Mover"), body, false);
    connect(m_cta, &QPushButton::clicked, this, [this] {
        if (validateBeforeAccept())
            onConfirmed();
    });
    col->addWidget(m_cta);
}

void MoveToSheet::applyDark(bool dark) {
    const tokens::Palette tk = tokens::paletteFor(dark);
    m_titleLabel->setStyleSheet(QStringLiteral("QLabel { color: %1; background: transparent; }").arg(tk.text.name()));
    m_closeBtn->setIcon(uiIcon(QStringLiteral("x"), tk.icon, 13));
    m_closeBtn->setIconSize(QSize(13, 13));
    m_closeBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: %1; border: none; border-radius: 12px; }"
        "QPushButton:hover { background: %2; }")
                                  .arg(tk.field.name(), tk.hover.name(QColor::HexArgb)));

    auto styleLineEdit = [&](QLineEdit* edit) {
        edit->setFont(mvFont(12.5, QFont::Normal));
        QPalette ep = edit->palette();
        ep.setColor(QPalette::PlaceholderText, tk.faint);
        edit->setPalette(ep);
        edit->setStyleSheet(QStringLiteral(
            "QLineEdit { background: transparent; border: none; color: %1; padding: 0;"
            " selection-background-color: %2; selection-color: %3; }")
                                .arg(tk.text.name(), tk.accent.name(), tk.accentFg.name()));
    };
    styleLineEdit(m_searchEdit);
    styleLineEdit(m_newFolderEdit);

    m_scroll->setStyleSheet(QStringLiteral(
        "QScrollArea { background: transparent; border: none; }"
        "QScrollBar:vertical { background: transparent; width: 7px; margin: 2px 0; }"
        "QScrollBar::handle:vertical { background: %1; border-radius: 3px; min-height: 22px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }")
                                .arg(tk.borderPop.name(QColor::HexArgb)));
    m_searchBox->setDark(dark);
    m_tree->setDark(dark);
    m_newFolderBar->setDark(dark);
}

void MoveToSheet::openMove(const QString& titleLabel, const QString& currentRel,
                           const QVector<FolderMeta>& folders, const HomePalette& pal, bool dark) {
    m_currentRel = LibraryStore::normalizeRelativeFolder(currentRel);
    m_folders = folders;
    m_searchEdit->clear();
    collapseNewFolderInput();

    m_titleLabel->setText(titleLabel.isEmpty() ? QStringLiteral("Mover") : titleLabel);
    m_titleLabel->setToolTip(titleLabel);

    // Preselecciona un destino válido distinto de la carpeta actual.
    m_hasSelection = false;
    m_selectedRel.clear();
    if (!m_currentRel.isEmpty()) {
        m_hasSelection = true;
        m_selectedRel = QString();
    } else if (!m_folders.isEmpty()) {
        m_hasSelection = true;
        m_selectedRel = m_folders.first().relativePath;
    }

    applyDark(dark);
    rebuildList(true);
    refreshCta();
    openSheet(pal, dark);
    m_searchEdit->setFocus();
}

void MoveToSheet::rebuildList(bool adjustHeight) {
    const QString needle = m_searchEdit->text().trimmed();
    QVector<MoveTreeList::Entry> entries;
    if (needle.isEmpty() || QStringLiteral("Documentos").contains(needle, Qt::CaseInsensitive))
        entries.append({QString(), QStringLiteral("Documentos"), 0, 0, true});

    for (const FolderMeta& f : m_folders) {
        if (!needle.isEmpty() && !f.name.contains(needle, Qt::CaseInsensitive)
            && !f.relativePath.contains(needle, Qt::CaseInsensitive)) {
            continue;
        }
        entries.append({f.relativePath, f.name, needle.isEmpty() ? f.depth : 0, f.colorIndex, false});
    }

    m_tree->setEntries(entries, m_currentRel, m_selectedRel, m_hasSelection);
    if (adjustHeight) {
        const int desiredH = std::clamp(int(entries.size()) * kRowH, kRowH, kMaxTreeH);
        m_scroll->setFixedHeight(desiredH);
    }
}

QString MoveToSheet::destinationLabel() const {
    if (!m_hasSelection || m_selectedRel == m_currentRel)
        return {};
    if (m_selectedRel.isEmpty())
        return QStringLiteral("Documentos");
    for (const FolderMeta& f : m_folders) {
        if (f.relativePath == m_selectedRel)
            return f.name;
    }
    return m_selectedRel.section(QLatin1Char('/'), -1);
}

void MoveToSheet::refreshCta() {
    const QString dest = destinationLabel();
    const bool valid = !dest.isEmpty();
    m_cta->setEnabled(valid);
    m_cta->setText(valid ? QStringLiteral("Mover a %1").arg(dest) : QStringLiteral("Elige una carpeta"));
}

void MoveToSheet::expandNewFolderInput() {
    m_newFolderBar->setExpanded(true);
    m_newFolderEdit->clear();
    m_newFolderEdit->show();
    m_newFolderEdit->setFocus();
}

void MoveToSheet::collapseNewFolderInput() {
    m_newFolderEdit->hide();
    m_newFolderEdit->clear();
    m_newFolderBar->setExpanded(false);
}

void MoveToSheet::submitNewFolder() {
    const QString clean = LibraryStore::sanitizeBaseName(m_newFolderEdit->text());
    if (clean.isEmpty()) {
        m_newFolderBar->setError(true);
        return;
    }
    const QString parentRel = m_hasSelection ? m_selectedRel : QString();
    QString newRel, err;
    if (!LibraryStore::createFolder(parentRel, clean, 2, 0, &newRel, &err)) {
        m_newFolderBar->setError(true);
        return;
    }
    m_folders = LibraryStore::listAllFoldersTree();
    m_selectedRel = newRel;
    m_hasSelection = true;
    m_searchEdit->clear();
    collapseNewFolderInput();
    rebuildList(true);
    repositionOnResize();
    refreshCta();
    const int ry = m_tree->rowYFor(newRel);
    if (ry >= 0)
        m_scroll->ensureVisible(0, ry + kRowH / 2, 0, kRowH);
    emit folderCreated(newRel);
}

bool MoveToSheet::validateBeforeAccept() {
    return m_hasSelection && m_selectedRel != m_currentRel;
}

void MoveToSheet::onConfirmed() {
    if (!validateBeforeAccept())
        return;
    const QString target = m_selectedRel;
    closeSheet(true);
    emit moveSubmitted(target);
}

bool MoveToSheet::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_searchEdit) {
        if (event->type() == QEvent::FocusIn)
            m_searchBox->setFocused(true);
        else if (event->type() == QEvent::FocusOut)
            m_searchBox->setFocused(false);
    } else if (watched == m_newFolderEdit && event->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(event);
        if (ke->key() == Qt::Key_Escape) {
            collapseNewFolderInput();
            return true;
        }
    }
    return GlassSheet::eventFilter(watched, event);
}

} // namespace finily
