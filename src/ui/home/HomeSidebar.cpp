#include "ui/home/HomeSidebar.h"

#include <QContextMenuEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace finily {

class SidebarItemButton : public QWidget {
    Q_OBJECT

public:
    explicit SidebarItemButton(QString glyphKind, QString label, QWidget* parent = nullptr)
        : QWidget(parent)
        , m_glyph(std::move(glyphKind))
        , m_label(std::move(label)) {
        setFixedHeight(35);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_Hover, true);
    }

    void setGlyphAndLabel(const QString& glyphKind, const QString& label) {
        m_glyph = glyphKind;
        m_label = label;
        update();
    }

    void setActive(bool active) {
        if (m_active == active)
            return;
        m_active = active;
        update();
    }

    void setBadgeCount(int count) {
        if (m_count == count)
            return;
        m_count = count;
        update();
    }

    void setAccentTone(bool accentTone) {
        m_accentTone = accentTone;
        update();
    }

    void setPalette(const HomePalette& pal) {
        m_pal = pal;
        update();
    }

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        if (m_active) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(m_pal.tint);
            painter.drawRoundedRect(r, 9.0, 9.0);
        } else if (underMouse()) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(m_pal.hover);
            painter.drawRoundedRect(r, 9.0, 9.0);
        }

        const QColor fg = (m_active || m_accentTone) ? m_pal.accent : m_pal.text;
        homeGlyph(m_glyph, fg).paint(&painter, QRect(11, (height() - 18) / 2, 18, 18));

        QFont font = painter.font();
        font.setPixelSize(13);
        font.setWeight(m_active || m_accentTone ? QFont::DemiBold : QFont::Medium);
        painter.setFont(font);
        painter.setPen(fg);

        const int rightSpace = (m_count >= 0) ? 36 : 10;
        const QRect textRect(37, 0, std::max(0, width() - 37 - rightSpace), height());
        painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, m_label);

        if (m_count >= 0) {
            QFont cf = painter.font();
            cf.setPixelSize(11);
            cf.setWeight(QFont::DemiBold);
            painter.setFont(cf);
            painter.setPen(m_active ? m_pal.accent : m_pal.muted);
            painter.drawText(QRect(width() - 34, 0, 24, height()),
                             Qt::AlignRight | Qt::AlignVCenter, QString::number(m_count));
        }
    }

    void enterEvent(QEnterEvent*) override { update(); }
    void leaveEvent(QEvent*) override { update(); }
    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && rect().contains(event->pos()))
            emit clicked();
        QWidget::mouseReleaseEvent(event);
    }

private:
    QString m_glyph;
    QString m_label;
    HomePalette m_pal = homePaletteFor(false);
    bool m_active = false;
    bool m_accentTone = false;
    int m_count = -1;
};

class SidebarFolderButton : public QWidget {
    Q_OBJECT

public:
    explicit SidebarFolderButton(const FolderMeta& meta, QWidget* parent = nullptr)
        : QWidget(parent)
        , m_meta(meta) {
        setFixedHeight(32);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_Hover, true);
    }

    QString relativePath() const { return m_meta.relativePath; }
    void setActive(bool active) {
        if (m_active == active)
            return;
        m_active = active;
        update();
    }
    void setPalette(const HomePalette& pal) {
        m_pal = pal;
        update();
    }

signals:
    void clicked(const QString& relPath);
    void newSubfolderRequested(const QString& relPath);
    void editRequested(const finily::FolderMeta& meta);
    void renameRequested(const QString& relPath);
    void deleteRequested(const QString& relPath);

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        if (m_active) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(m_pal.tint);
            painter.drawRoundedRect(r, 8.0, 8.0);
        } else if (underMouse()) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(m_pal.hover);
            painter.drawRoundedRect(r, 8.0, 8.0);
        }

        const int indent = 10 + std::min(m_meta.depth, 3) * 14;
        const QColor folderColor =
            coverColors().at(std::clamp(m_meta.colorIndex, 0, int(coverColors().size()) - 1));
        homeGlyph(QStringLiteral("folder"), folderColor)
            .paint(&painter, QRect(indent, (height() - 16) / 2, 16, 16));

        QFont font = painter.font();
        font.setPixelSize(12);
        font.setWeight(m_active ? QFont::DemiBold : QFont::Medium);
        painter.setFont(font);
        painter.setPen(m_active ? m_pal.accent : m_pal.text);
        const int textX = indent + 23;
        const int textW = std::max(24, width() - textX - 32);
        const QString elided = QFontMetrics(font).elidedText(m_meta.name, Qt::ElideRight, textW);
        painter.drawText(QRect(textX, 0, textW, height()), Qt::AlignLeft | Qt::AlignVCenter, elided);

        if (m_meta.documentCount > 0) {
            QFont cf = painter.font();
            cf.setPixelSize(10);
            cf.setWeight(QFont::DemiBold);
            painter.setFont(cf);
            painter.setPen(m_active ? m_pal.accent : m_pal.muted);
            painter.drawText(QRect(width() - 32, 0, 22, height()),
                             Qt::AlignRight | Qt::AlignVCenter,
                             QString::number(m_meta.documentCount));
        }
    }

    void enterEvent(QEnterEvent*) override { update(); }
    void leaveEvent(QEvent*) override { update(); }
    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && rect().contains(event->pos()))
            emit clicked(m_meta.relativePath);
        QWidget::mouseReleaseEvent(event);
    }
    void contextMenuEvent(QContextMenuEvent* event) override {
        QMenu menu(this);
        menu.setStyleSheet(QStringLiteral(
            "QMenu { background: %1; border: 1px solid %2; border-radius: 12px; padding: 6px; }"
            "QMenu::item { color: %3; padding: 7px 16px; border-radius: 8px; font-size: 13px; }"
            "QMenu::item:selected { background: %4; }"
            "QMenu::separator { height: 1px; background: %2; margin: 5px 8px; }")
                               .arg(m_pal.surface.name(), m_pal.border.name(QColor::HexArgb),
                                    m_pal.text.name(), m_pal.tint.name(QColor::HexArgb)));
        menu.addAction(QStringLiteral("Nueva subcarpeta…"), this,
                       [this] { emit newSubfolderRequested(m_meta.relativePath); });
        menu.addAction(QStringLiteral("Editar carpeta…"), this,
                       [this] { emit editRequested(m_meta); });
        menu.addAction(QStringLiteral("Renombrar…"), this,
                       [this] { emit renameRequested(m_meta.relativePath); });
        menu.addSeparator();
        menu.addAction(QStringLiteral("Eliminar"), this,
                       [this] { emit deleteRequested(m_meta.relativePath); });
        menu.exec(event->globalPos());
    }

private:
    FolderMeta m_meta;
    HomePalette m_pal = homePaletteFor(false);
    bool m_active = false;
};

HomeSidebar::HomeSidebar(QWidget* parent)
    : QWidget(parent) {
    setFixedWidth(226);
    setAttribute(Qt::WA_StyledBackground, false);
    setAutoFillBackground(false);

    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(14, 22, 14, 16);
    col->setSpacing(6);

    auto* brandRow = new QHBoxLayout();
    brandRow->setContentsMargins(6, 0, 6, 0);
    brandRow->setSpacing(10);
    brandRow->addWidget(new FinilyBadge(this), 0, Qt::AlignVCenter);
    auto* brandLabel = new QLabel(QStringLiteral("Finily"), this);
    brandLabel->setObjectName(QStringLiteral("finilyBrandTitle"));
    brandRow->addWidget(brandLabel, 1, Qt::AlignVCenter);
    col->addLayout(brandRow);
    col->addSpacing(14);

    m_allBtn = new SidebarItemButton(QStringLiteral("all"), QStringLiteral("Todos"), this);
    m_allBtn->setActive(true);
    m_allBtn->setBadgeCount(0);
    connect(m_allBtn, &SidebarItemButton::clicked, this, [this] {
        setSelectedFilter(0);
        emit filterSelected(0);
    });
    col->addWidget(m_allBtn);

    m_recentBtn = new SidebarItemButton(QStringLiteral("recent"), QStringLiteral("Recientes"), this);
    m_recentBtn->setBadgeCount(0);
    connect(m_recentBtn, &SidebarItemButton::clicked, this, [this] {
        setSelectedFilter(1);
        emit filterSelected(1);
    });
    col->addWidget(m_recentBtn);

    col->addSpacing(6);
    m_midLine = new QWidget(this);
    m_midLine->setFixedHeight(1);
    col->addWidget(m_midLine);
    col->addSpacing(4);

    auto* foldersHeader = new QHBoxLayout();
    foldersHeader->setContentsMargins(8, 0, 2, 0);
    m_foldersLabel = new QLabel(QStringLiteral("CARPETAS"), this);
    m_foldersLabel->setObjectName(QStringLiteral("finilySidebarGroup"));
    foldersHeader->addWidget(m_foldersLabel, 1);

    m_addFolderBtn = new QToolButton(this);
    m_addFolderBtn->setFixedSize(24, 24);
    m_addFolderBtn->setIconSize(QSize(14, 14));
    m_addFolderBtn->setToolTip(QStringLiteral("Nueva carpeta"));
    m_addFolderBtn->setCursor(Qt::PointingHandCursor);
    m_addFolderBtn->setFocusPolicy(Qt::NoFocus);
    connect(m_addFolderBtn, &QToolButton::clicked, this, &HomeSidebar::createRootFolderRequested);
    foldersHeader->addWidget(m_addFolderBtn);
    col->addLayout(foldersHeader);

    m_folderScroll = new QScrollArea(this);
    m_folderScroll->setWidgetResizable(true);
    m_folderScroll->setFrameShape(QFrame::NoFrame);
    m_folderScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_folderScroll->viewport()->setAutoFillBackground(false);
    m_folderScroll->viewport()->setAttribute(Qt::WA_StyledBackground, false);

    m_folderContainer = new QWidget;
    m_folderContainer->setObjectName(QStringLiteral("finilySidebarTree"));
    m_folderContainer->setAutoFillBackground(false);
    m_folderLayout = new QVBoxLayout(m_folderContainer);
    m_folderLayout->setContentsMargins(0, 2, 0, 2);
    m_folderLayout->setSpacing(2);
    m_folderLayout->addStretch(1);
    m_folderScroll->setWidget(m_folderContainer);
    col->addWidget(m_folderScroll, 1);

    m_footerLine = new QWidget(this);
    m_footerLine->setFixedHeight(1);
    col->addWidget(m_footerLine);
    col->addSpacing(4);

    m_draftBtn = new SidebarItemButton(QStringLiteral("restore"), QStringLiteral("Continuar borrador"), this);
    m_draftBtn->setAccentTone(true);
    m_draftBtn->setToolTip(QStringLiteral("Recuperar la última sesión autoguardada"));
    m_draftBtn->hide();
    connect(m_draftBtn, &SidebarItemButton::clicked, this, &HomeSidebar::continueDraftRequested);
    col->addWidget(m_draftBtn);

    m_openBtn = new SidebarItemButton(QStringLiteral("folder"), QStringLiteral("Abrir archivo…"), this);
    m_openBtn->setToolTip(QStringLiteral("Abrir un cuaderno .finily desde el disco"));
    connect(m_openBtn, &SidebarItemButton::clicked, this, &HomeSidebar::openExternalFileRequested);
    col->addWidget(m_openBtn);

    m_themeBtn = new SidebarItemButton(QStringLiteral("moon"), QStringLiteral("Modo oscuro"), this);
    connect(m_themeBtn, &SidebarItemButton::clicked, this, [this] { emit darkModeToggled(!m_dark); });
    col->addWidget(m_themeBtn);
}

void HomeSidebar::setSelectedFilter(int filterIndex) {
    m_selectedFilter = (filterIndex == 1) ? 1 : 0;
    m_selectedFolder.clear();
    if (m_allBtn)
        m_allBtn->setActive(m_selectedFilter == 0);
    if (m_recentBtn)
        m_recentBtn->setActive(m_selectedFilter == 1);
    for (SidebarFolderButton* fb : m_folderBtns) {
        if (fb)
            fb->setActive(false);
    }
}

void HomeSidebar::setSelectedFolder(const QString& relativePath) {
    m_selectedFilter = -1;
    m_selectedFolder = relativePath;
    if (m_allBtn)
        m_allBtn->setActive(false);
    if (m_recentBtn)
        m_recentBtn->setActive(false);
    for (SidebarFolderButton* fb : m_folderBtns) {
        if (fb)
            fb->setActive(fb->relativePath() == m_selectedFolder);
    }
}

void HomeSidebar::setCounts(int totalCount, int recentCount) {
    if (m_allBtn)
        m_allBtn->setBadgeCount(totalCount);
    if (m_recentBtn)
        m_recentBtn->setBadgeCount(recentCount);
}

void HomeSidebar::setFolders(const QVector<FolderMeta>& folders) {
    for (SidebarFolderButton* fb : m_folderBtns) {
        if (fb) {
            m_folderLayout->removeWidget(fb);
            fb->deleteLater();
        }
    }
    m_folderBtns.clear();

    for (const FolderMeta& meta : folders) {
        auto* btn = new SidebarFolderButton(meta, m_folderContainer);
        btn->setPalette(m_pal);
        btn->setActive(m_selectedFilter < 0 && meta.relativePath == m_selectedFolder);
        connect(btn, &SidebarFolderButton::clicked, this, &HomeSidebar::folderSelected);
        connect(btn, &SidebarFolderButton::newSubfolderRequested, this,
                &HomeSidebar::createSubfolderRequested);
        connect(btn, &SidebarFolderButton::editRequested, this, &HomeSidebar::editFolderRequested);
        connect(btn, &SidebarFolderButton::renameRequested, this, &HomeSidebar::renameFolderRequested);
        connect(btn, &SidebarFolderButton::deleteRequested, this, &HomeSidebar::deleteFolderRequested);
        m_folderLayout->insertWidget(m_folderLayout->count() - 1, btn);
        m_folderBtns.append(btn);
    }
}

void HomeSidebar::setDraftVisible(bool visible) {
    if (m_draftBtn)
        m_draftBtn->setVisible(visible);
}

void HomeSidebar::applyTheme(const HomePalette& pal, bool dark) {
    m_pal = pal;
    m_dark = dark;
    for (auto* btn : {m_allBtn, m_recentBtn, m_draftBtn, m_openBtn, m_themeBtn}) {
        if (btn)
            btn->setPalette(pal);
    }
    for (SidebarFolderButton* fb : m_folderBtns) {
        if (fb)
            fb->setPalette(pal);
    }
    if (m_addFolderBtn)
        m_addFolderBtn->setIcon(homeGlyph(QStringLiteral("plus"), pal.muted));
    if (m_themeBtn) {
        m_themeBtn->setGlyphAndLabel(dark ? QStringLiteral("sun") : QStringLiteral("moon"),
                                     dark ? QStringLiteral("Modo claro") : QStringLiteral("Modo oscuro"));
    }
    const QString lineQss = QStringLiteral("background: %1;").arg(pal.sidebarBorder.name(QColor::HexArgb));
    if (m_midLine)
        m_midLine->setStyleSheet(lineQss);
    if (m_footerLine)
        m_footerLine->setStyleSheet(lineQss);
    if (m_folderScroll)
        m_folderScroll->viewport()->setAutoFillBackground(false);
    update();
}

void HomeSidebar::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), m_pal.sidebar);
    painter.fillRect(QRect(width() - 1, 0, 1, height()), m_pal.sidebarBorder);
}

#include "HomeSidebar.moc"

} // namespace finily
