#include "ui/home/HomeSidebar.h"

#include "core/Utils.h"
#include "ui/DesignTokens.h"
#include "ui/UiIcons.h"

#include <QContextMenuEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QVBoxLayout>

#include <algorithm>

namespace finily {

// Fila de navegación (.nav del mockup): ícono 16 px o punto de color,
// texto 12.5 px y un ícono final opcional (chevron).
class SidebarNavRow : public QWidget {
    Q_OBJECT

public:
    SidebarNavRow(QString icon, QString label, QWidget* parent = nullptr)
        : QWidget(parent)
        , m_icon(std::move(icon))
        , m_label(std::move(label)) {
        setFixedHeight(30);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_Hover, true);
    }

    // Variante de árbol: punto de color de la carpeta, fila más baja.
    void makeTreeItem(const FolderMeta& meta) {
        m_meta = meta;
        m_isFolder = true;
        m_dot = tokens::folderColor(meta.colorIndex).body2;
        setFixedHeight(26);
    }
    const FolderMeta& meta() const { return m_meta; }
    bool isFolder() const { return m_isFolder; }

    void setTrailing(const QString& icon) { m_trailing = icon; update(); }
    void setActive(bool active) {
        if (m_active != active) {
            m_active = active;
            update();
        }
    }
    void setPalette(const HomePalette& pal) { m_pal = pal; update(); }

signals:
    void clicked();
    void newSubfolderRequested(const QString& relPath);
    void editRequested(const finily::FolderMeta& meta);
    void renameRequested(const QString& relPath);
    void deleteRequested(const QString& relPath);

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRectF r = QRectF(rect());
        if (m_active || underMouse()) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(m_active ? m_pal.tint : m_pal.hover);
            painter.drawRoundedRect(r, 8.0, 8.0);
        }

        const int depthIndent = m_isFolder ? std::min(m_meta.depth, 4) * 12 : 0;
        const qreal x = 8 + depthIndent;
        if (m_isFolder) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(m_dot);
            painter.drawRoundedRect(QRectF(x, (height() - 9) / 2.0, 9, 9), 3, 3);
        } else {
            paintUiIcon(&painter, QRectF(x, (height() - 16) / 2.0, 16, 16), m_icon,
                        m_active ? m_pal.text : m_pal.icon);
        }

        QFont font = painter.font();
        font.setPixelSize(m_isFolder ? 12 : 13);
        font.setWeight(m_active ? QFont::DemiBold : QFont::Medium);
        painter.setFont(font);
        painter.setPen(m_active ? m_pal.text : m_pal.icon);
        const int textX = int(x) + (m_isFolder ? 18 : 25);
        const int textW = std::max(10, width() - textX - (m_trailing.isEmpty() ? 8 : 28));
        painter.drawText(QRect(textX, 0, textW, height()), Qt::AlignLeft | Qt::AlignVCenter,
                         QFontMetrics(font).elidedText(m_label, Qt::ElideRight, textW));

        if (!m_trailing.isEmpty())
            paintUiIcon(&painter, QRectF(width() - 21, (height() - 13) / 2.0, 13, 13), m_trailing,
                        m_pal.faint);
    }

    void enterEvent(QEnterEvent*) override { update(); }
    void leaveEvent(QEvent*) override { update(); }
    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && rect().contains(event->pos()))
            emit clicked();
        QWidget::mouseReleaseEvent(event);
    }
    // Provisional hasta la fase 5 (menú contextual propio, mockup 07).
    void contextMenuEvent(QContextMenuEvent* event) override {
        if (!m_isFolder)
            return;
        QMenu menu(this);
        menu.setStyleSheet(QStringLiteral(
            "QMenu { background: %1; border: 1px solid %2; border-radius: 12px; padding: 6px; }"
            "QMenu::item { color: %3; padding: 7px 16px; border-radius: 8px; font-size: 13px; }"
            "QMenu::item:selected { background: %4; }"
            "QMenu::separator { height: 1px; background: %2; margin: 5px 8px; }")
                               .arg(m_pal.surface.name(), m_pal.border.name(QColor::HexArgb),
                                    m_pal.text.name(), m_pal.tint.name(QColor::HexArgb)));
        const QString rel = m_meta.relativePath;
        menu.addAction(QStringLiteral("Nueva subcarpeta…"), this, [this, rel] { emit newSubfolderRequested(rel); });
        menu.addAction(QStringLiteral("Editar carpeta…"), this, [this] { emit editRequested(m_meta); });
        menu.addAction(QStringLiteral("Renombrar…"), this, [this, rel] { emit renameRequested(rel); });
        menu.addSeparator();
        menu.addAction(QStringLiteral("Eliminar"), this, [this, rel] { emit deleteRequested(rel); });
        menu.exec(event->globalPos());
    }

private:
    QString m_icon;
    QString m_label;
    QString m_trailing;
    FolderMeta m_meta;
    QColor m_dot;
    bool m_isFolder = false;
    bool m_active = false;
    HomePalette m_pal = homePaletteFor(false);
};

// Botón cuadrado de ícono (.sb-btn del mockup, 26 px).
class SidebarIconButton : public QWidget {
    Q_OBJECT

public:
    SidebarIconButton(QString icon, const QString& tip, QWidget* parent = nullptr)
        : QWidget(parent)
        , m_icon(std::move(icon)) {
        setFixedSize(28, 28);
        setToolTip(tip);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_Hover, true);
    }
    void setIcon(const QString& icon) { m_icon = icon; update(); }
    void setPalette(const HomePalette& pal) { m_pal = pal; update(); }

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        if (underMouse()) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(m_pal.hover);
            painter.drawRoundedRect(QRectF(rect()), 8, 8);
        }
        paintUiIcon(&painter, QRectF((width() - 16) / 2.0, (height() - 16) / 2.0, 16, 16), m_icon,
                    m_pal.icon);
    }
    void enterEvent(QEnterEvent*) override { update(); }
    void leaveEvent(QEvent*) override { update(); }
    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && rect().contains(event->pos()))
            emit clicked();
    }

private:
    QString m_icon;
    HomePalette m_pal = homePaletteFor(false);
};

// Contenedor del árbol: dibuja la guía vertical fina a la izquierda.
class SidebarTreeBox : public QWidget {
public:
    using QWidget::QWidget;
    QColor guide;

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(QRectF(15, 2, 1, std::max(0, height() - 4)), guide);
    }
};

HomeSidebar::HomeSidebar(QWidget* parent)
    : QWidget(parent) {
    setFixedWidth(tokens::space::SidebarWidth);
    setAttribute(Qt::WA_StyledBackground, false);
    m_treeOpen = readAppSetting(QStringLiteral("home/foldersOpen"), true).toBool();

    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(10, 14, 10, 12);
    col->setSpacing(1);

    auto* head = new QHBoxLayout();
    head->setContentsMargins(6, 2, 4, 12);
    auto* brand = new QLabel(QStringLiteral("Finily"), this);
    brand->setObjectName(QStringLiteral("finilyBrandTitle"));
    head->addWidget(brand, 1, Qt::AlignVCenter);
    m_collapseBtn = new SidebarIconButton(QStringLiteral("side"), QStringLiteral("Ocultar barra lateral"), this);
    connect(m_collapseBtn, &SidebarIconButton::clicked, this, &HomeSidebar::collapseRequested);
    head->addWidget(m_collapseBtn, 0, Qt::AlignVCenter);
    col->addLayout(head);

    m_docsRow = new SidebarNavRow(QStringLiteral("docs"), QStringLiteral("Documentos"), this);
    connect(m_docsRow, &SidebarNavRow::clicked, this, [this] {
        setSelectedFilter(0);
        emit filterSelected(0);
    });
    col->addWidget(m_docsRow);

    m_foldersRow = new SidebarNavRow(QStringLiteral("folder"), QStringLiteral("Carpetas"), this);
    connect(m_foldersRow, &SidebarNavRow::clicked, this, [this] { setTreeOpen(!m_treeOpen); });
    col->addWidget(m_foldersRow);

    m_treeScroll = new QScrollArea(this);
    m_treeScroll->setWidgetResizable(true);
    m_treeScroll->setFrameShape(QFrame::NoFrame);
    m_treeScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_treeScroll->viewport()->setAutoFillBackground(false);
    m_treeScroll->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; }"));
    m_treeBox = new SidebarTreeBox;
    m_treeBox->setAutoFillBackground(false);
    m_treeLayout = new QVBoxLayout(m_treeBox);
    m_treeLayout->setContentsMargins(25, 1, 0, 1);
    m_treeLayout->setSpacing(1);
    m_treeScroll->setWidget(m_treeBox);
    col->addWidget(m_treeScroll);
    col->addStretch(1);

    m_draftRow = new SidebarNavRow(QStringLiteral("restore"), QStringLiteral("Continuar borrador"), this);
    m_draftRow->setToolTip(QStringLiteral("Recuperar la última sesión autoguardada"));
    m_draftRow->hide();
    connect(m_draftRow, &SidebarNavRow::clicked, this, &HomeSidebar::continueDraftRequested);
    col->addWidget(m_draftRow);

    col->addSpacing(7);
    m_footLine = new QWidget(this);
    m_footLine->setFixedHeight(1);
    col->addWidget(m_footLine);
    col->addSpacing(7);

    auto* icons = new QHBoxLayout();
    icons->setContentsMargins(4, 0, 4, 0);
    icons->setSpacing(4);
    m_themeBtn = new SidebarIconButton(QStringLiteral("moon"), QStringLiteral("Modo oscuro"), this);
    connect(m_themeBtn, &SidebarIconButton::clicked, this, [this] { emit darkModeToggled(!m_dark); });
    icons->addWidget(m_themeBtn);
    icons->addStretch(1);
    col->addLayout(icons);

    setTreeOpen(m_treeOpen);
}

void HomeSidebar::setTreeOpen(bool open) {
    m_treeOpen = open;
    writeAppSetting(QStringLiteral("home/foldersOpen"), open);
    m_foldersRow->setTrailing(open ? QStringLiteral("chevD") : QStringLiteral("chevR"));
    m_treeScroll->setVisible(open && !m_folderRows.isEmpty());
}

void HomeSidebar::refreshActive() {
    m_docsRow->setActive(m_selectedFilter == 0);
    for (SidebarNavRow* row : m_folderRows)
        row->setActive(m_selectedFilter < 0 && row->meta().relativePath == m_selectedFolder);
}

void HomeSidebar::setSelectedFilter(int) {
    m_selectedFilter = 0;
    m_selectedFolder.clear();
    refreshActive();
}

void HomeSidebar::setSelectedFolder(const QString& relativePath) {
    m_selectedFilter = -1;
    m_selectedFolder = relativePath;
    refreshActive();
}

void HomeSidebar::setFolders(const QVector<FolderMeta>& folders) {
    qDeleteAll(m_folderRows);
    m_folderRows.clear();
    for (const FolderMeta& meta : folders) {
        auto* row = new SidebarNavRow(QString(), meta.name, m_treeBox);
        row->makeTreeItem(meta);
        row->setPalette(m_pal);
        const QString rel = meta.relativePath;
        connect(row, &SidebarNavRow::clicked, this, [this, rel] { emit folderSelected(rel); });
        connect(row, &SidebarNavRow::newSubfolderRequested, this, &HomeSidebar::createSubfolderRequested);
        connect(row, &SidebarNavRow::editRequested, this, &HomeSidebar::editFolderRequested);
        connect(row, &SidebarNavRow::renameRequested, this, &HomeSidebar::renameFolderRequested);
        connect(row, &SidebarNavRow::deleteRequested, this, &HomeSidebar::deleteFolderRequested);
        m_treeLayout->addWidget(row);
        // Visibles ya: si no, el layout las ignora al medir el alto del árbol.
        row->show();
        m_folderRows.append(row);
    }
    // El árbol ocupa solo su alto natural; si no cabe, aparece la barra de desplazamiento.
    m_treeLayout->activate();
    m_treeBox->adjustSize();
    m_treeScroll->setMaximumHeight(m_treeBox->sizeHint().height() + 2);
    setTreeOpen(m_treeOpen);
    refreshActive();
}

void HomeSidebar::setDraftVisible(bool visible) {
    m_draftRow->setVisible(visible);
}

void HomeSidebar::applyTheme(const HomePalette& pal, bool dark) {
    m_pal = pal;
    m_dark = dark;
    for (SidebarNavRow* row : {m_docsRow, m_foldersRow, m_draftRow})
        row->setPalette(pal);
    for (SidebarNavRow* row : m_folderRows)
        row->setPalette(pal);
    for (SidebarIconButton* btn : {m_collapseBtn, m_themeBtn})
        btn->setPalette(pal);
    m_themeBtn->setIcon(dark ? QStringLiteral("sun") : QStringLiteral("moon"));
    m_themeBtn->setToolTip(dark ? QStringLiteral("Modo claro") : QStringLiteral("Modo oscuro"));
    static_cast<SidebarTreeBox*>(m_treeBox)->guide = tokens::paletteFor(dark).guide;
    m_treeBox->update();
    m_footLine->setStyleSheet(QStringLiteral("background: %1;").arg(pal.sidebarBorder.name(QColor::HexArgb)));
    update();
}

void HomeSidebar::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), m_pal.sidebar);
    painter.fillRect(QRect(width() - 1, 0, 1, height()), m_pal.sidebarBorder);
}

#include "HomeSidebar.moc"

} // namespace finily
