#include "ui/home/HomeSidebar.h"

#include "core/Utils.h"
#include "ui/DesignTokens.h"
#include "ui/UiIcons.h"
#include "ui/home/UpdateBanner.h"

#include <QContextMenuEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSequentialAnimationGroup>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace finily {

// Fila de navegación (.nav del mockup): ícono 16 px o punto de color/etiqueta,
// texto y trailing opcional (chevron o contador).
class SidebarNavRow : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal countPop READ countPop WRITE setCountPop)
    Q_PROPERTY(qreal iconBounce READ iconBounce WRITE setIconBounce)

public:
    SidebarNavRow(QString icon, QString label, QWidget* parent = nullptr)
        : QWidget(parent)
        , m_icon(std::move(icon))
        , m_label(std::move(label)) {
        setFixedHeight(36);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_Hover, true);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    QSize sizeHint() const override { return {180, (m_isFolder || m_isTag) ? 32 : 36}; }
    QSize minimumSizeHint() const override { return {40, (m_isFolder || m_isTag) ? 32 : 36}; }

    // Variante de árbol para carpetas: punto cuadrado redondeado y sangría por nivel.
    void makeTreeItem(const FolderMeta& meta) {
        m_meta = meta;
        m_isFolder = true;
        m_isTag = false;
        m_dot = tokens::folderColor(meta.colorIndex).body2;
        setFixedHeight(32);
    }

    // Variante de árbol para etiquetas: punto circular de 9 px.
    void makeTagItem(const TagInfo& tag) {
        m_tag = tag;
        m_isTag = true;
        m_isFolder = false;
        m_dot = tokens::folderColor(tag.colorIndex).body2;
        setFixedHeight(32);
    }

    const FolderMeta& meta() const { return m_meta; }
    const TagInfo& tag() const { return m_tag; }
    bool isFolder() const { return m_isFolder; }
    bool isTag() const { return m_isTag; }

    void setTrailing(const QString& icon) {
        m_trailing = icon;
        update();
    }

    void setCount(int n) {
        const bool increased = n > m_count && n > 0;
        m_count = n;
        if (increased && isVisible() && !tokens::reducedMotion()) {
            m_countPop = 0.4;
            if (!m_countAnim)
                m_countAnim = new QPropertyAnimation(this, "countPop", this);
            m_countAnim->stop();
            m_countAnim->setDuration(tokens::motion::duration(tokens::motion::Base));
            m_countAnim->setEasingCurve(tokens::motion::spring());
            m_countAnim->setStartValue(0.4);
            m_countAnim->setEndValue(1.0);
            m_countAnim->start();
        } else {
            m_countPop = 1.0;
        }
        update();
    }

    void setActive(bool active) {
        if (m_active != active) {
            m_active = active;
            update();
        }
    }

    void setDropHighlight(bool on) {
        if (m_dropHighlight != on) {
            m_dropHighlight = on;
            update();
        }
    }

    void setTheme(const HomePalette& pal, bool dark) {
        m_pal = pal;
        m_dark = dark;
        update();
    }

    qreal countPop() const { return m_countPop; }
    void setCountPop(qreal v) {
        m_countPop = v;
        update();
    }

    qreal iconBounce() const { return m_iconBounce; }
    void setIconBounce(qreal v) {
        m_iconBounce = v;
        update();
    }

    void playIconBounce() {
        if (tokens::reducedMotion())
            return;
        if (!m_bounceGroup) {
            m_bounceGroup = new QSequentialAnimationGroup(this);
            auto* up = new QPropertyAnimation(this, "iconBounce", m_bounceGroup);
            up->setDuration(90);
            up->setEasingCurve(tokens::motion::out());
            up->setStartValue(0.0);
            up->setEndValue(1.0);
            auto* settle = new QPropertyAnimation(this, "iconBounce", m_bounceGroup);
            settle->setDuration(250);
            settle->setEasingCurve(tokens::motion::spring());
            settle->setStartValue(1.0);
            settle->setEndValue(0.0);
            m_bounceGroup->addAnimation(up);
            m_bounceGroup->addAnimation(settle);
        }
        m_bounceGroup->stop();
        m_bounceGroup->start();
    }

signals:
    void clicked();
    void chevronClicked();
    void menuRequested(const finily::FolderMeta& meta, const QPoint& globalPos);
    void tagMenuRequested(const finily::TagInfo& tag, const QPoint& globalPos);

protected:
    QRect chevronRect() const { return QRect(width() - 28, 0, 28, height()); }

    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const tokens::Palette tk = tokens::paletteFor(m_dark);
        const int depthIndent = m_isFolder ? std::min(m_meta.depth, 5) * 18 : 0;
        const QRectF pillRect(depthIndent, 0, std::max(0, width() - depthIndent), height());

        if (m_dropHighlight) {
            painter.setPen(QPen(tk.accent, 1.5));
            painter.setBrush(tk.soft);
            painter.drawRoundedRect(pillRect.adjusted(0.75, 0.75, -0.75, -0.75), 8.0, 8.0);
        } else if (m_active || underMouse()) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(m_active ? tk.soft : tk.hover);
            painter.drawRoundedRect(pillRect, 8.0, 8.0);
        }

        const qreal x = 10.0 + depthIndent;
        if (m_isFolder) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(m_dot);
            painter.drawRoundedRect(QRectF(x, (height() - 10.0) / 2.0, 10.0, 10.0), 3.0, 3.0);
        } else if (m_isTag) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(m_dot);
            painter.drawEllipse(QRectF(x, (height() - 9.0) / 2.0, 9.0, 9.0));
        } else {
            const qreal iconY = (height() - 18.0) / 2.0;
            const QColor iconColor = m_active ? tk.text : tk.icon;
            if (std::abs(m_iconBounce) > 0.001) {
                painter.save();
                const qreal dy = -3.0 * m_iconBounce;
                const qreal rot = -8.0 * m_iconBounce;
                painter.translate(x + 9.0, iconY + 9.0 + dy);
                painter.rotate(rot);
                paintUiIcon(&painter, QRectF(-9.0, -9.0, 18.0, 18.0), m_icon, iconColor);
                painter.restore();
            } else {
                paintUiIcon(&painter, QRectF(x, iconY, 18.0, 18.0), m_icon, iconColor);
            }
        }

        QFont f = font();
        f.setPixelSize((m_isFolder || m_isTag) ? 13 : 14);
        f.setWeight(m_active ? QFont::DemiBold : QFont::Medium);
        painter.setFont(f);
        painter.setPen(m_active ? tk.text : tk.icon);
        const int iconAdvance = m_isFolder ? 20 : (m_isTag ? 19 : 28);
        const int textX = int(x) + iconAdvance;
        const int rightReserve = (m_trailing.isEmpty() && m_count <= 0) ? 10 : 32;
        const int textW = std::max(10, width() - textX - rightReserve);
        painter.drawText(QRect(textX, 0, textW, height()), Qt::AlignLeft | Qt::AlignVCenter,
                         QFontMetrics(f).elidedText(m_label, Qt::ElideRight, textW));

        if (m_count > 0) {
            QFont cf = font();
            cf.setPixelSize(12);
            cf.setWeight(QFont::Medium);
            painter.setFont(cf);
            painter.setPen(m_active ? tk.muted : tk.faint);
            const QRect countRect(width() - 44, 0, 34, height());
            if (m_countPop < 0.999) {
                painter.save();
                const qreal alpha = std::clamp((m_countPop - 0.4) / 0.6, 0.0, 1.0);
                painter.setOpacity(painter.opacity() * alpha);
                const QPointF c = QRectF(countRect).center();
                painter.translate(c);
                painter.scale(m_countPop, m_countPop);
                painter.translate(-c);
                painter.drawText(countRect, Qt::AlignRight | Qt::AlignVCenter, QString::number(m_count));
                painter.restore();
            } else {
                painter.drawText(countRect, Qt::AlignRight | Qt::AlignVCenter, QString::number(m_count));
            }
        }

        if (!m_trailing.isEmpty()) {
            paintUiIcon(&painter, QRectF(width() - 23, (height() - 14.0) / 2.0, 14.0, 14.0),
                        m_trailing, tk.faint);
        }
    }

    void enterEvent(QEnterEvent*) override { update(); }
    void leaveEvent(QEvent*) override { update(); }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && rect().contains(event->pos())) {
            if (!m_trailing.isEmpty() && chevronRect().contains(event->pos()))
                emit chevronClicked();
            else
                emit clicked();
        }
        QWidget::mouseReleaseEvent(event);
    }

    void contextMenuEvent(QContextMenuEvent* event) override {
        if (m_isFolder)
            emit menuRequested(m_meta, event->globalPos());
        else if (m_isTag)
            emit tagMenuRequested(m_tag, event->globalPos());
    }

private:
    QString m_icon;
    QString m_label;
    int m_count = 0;
    QString m_trailing;
    FolderMeta m_meta;
    TagInfo m_tag;
    QColor m_dot;
    bool m_isFolder = false;
    bool m_isTag = false;
    bool m_active = false;
    bool m_dropHighlight = false;
    bool m_dark = false;
    qreal m_countPop = 1.0;
    qreal m_iconBounce = 0.0;
    QPropertyAnimation* m_countAnim = nullptr;
    QSequentialAnimationGroup* m_bounceGroup = nullptr;
    HomePalette m_pal = homePaletteFor(false);
};

// Botón cuadrado de ícono (.sb-btn del mockup, 28 px).
class SidebarIconButton : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal animScale READ animScale WRITE setAnimScale)

public:
    SidebarIconButton(QString icon, const QString& tip, QWidget* parent = nullptr)
        : QWidget(parent)
        , m_icon(std::move(icon)) {
        setFixedSize(34, 34);
        setToolTip(tip);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_Hover, true);
    }

    void setIcon(const QString& icon) {
        m_icon = icon;
        update();
    }

    void setActive(bool active) {
        if (m_active != active) {
            m_active = active;
            update();
        }
    }

    void setDot(bool dot) {
        if (m_dot != dot) {
            m_dot = dot;
            update();
        }
    }

    void setTheme(const HomePalette& pal, bool dark) {
        m_pal = pal;
        m_dark = dark;
        update();
    }

    qreal animScale() const { return m_animScale; }
    void setAnimScale(qreal s) {
        m_animScale = s;
        update();
    }

    void playPulse() {
        if (tokens::reducedMotion())
            return;
        if (!m_pulseGroup) {
            m_pulseGroup = new QSequentialAnimationGroup(this);
            auto* shrink = new QPropertyAnimation(this, "animScale", m_pulseGroup);
            shrink->setDuration(75);
            shrink->setEasingCurve(tokens::motion::in());
            shrink->setStartValue(1.0);
            shrink->setEndValue(0.88);
            auto* pop = new QPropertyAnimation(this, "animScale", m_pulseGroup);
            pop->setDuration(tokens::motion::duration(tokens::motion::Base));
            pop->setEasingCurve(tokens::motion::spring());
            pop->setStartValue(0.88);
            pop->setEndValue(1.0);
            m_pulseGroup->addAnimation(shrink);
            m_pulseGroup->addAnimation(pop);
        }
        m_pulseGroup->stop();
        m_pulseGroup->start();
    }

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const tokens::Palette tk = tokens::paletteFor(m_dark);
        if (std::abs(m_animScale - 1.0) > 0.001) {
            const QPointF c(width() * 0.5, height() * 0.5);
            painter.translate(c);
            painter.scale(m_animScale, m_animScale);
            painter.translate(-c);
        }

        if (m_active || underMouse()) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(m_active ? tk.soft : tk.hover);
            painter.drawRoundedRect(QRectF(rect()), 9.0, 9.0);
        }

        paintUiIcon(&painter, QRectF((width() - 18.0) / 2.0, (height() - 18.0) / 2.0, 18.0, 18.0),
                    m_icon, m_active ? tk.text : tk.icon);

        if (m_dot) {
            painter.setPen(QPen(m_pal.sidebar, 1.5));
            painter.setBrush(tk.accent);
            painter.drawEllipse(QPointF(width() - 7.5, 7.5), 3.5, 3.5);
        }
    }

    void enterEvent(QEnterEvent*) override { update(); }
    void leaveEvent(QEvent*) override { update(); }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && rect().contains(event->pos()))
            emit clicked();
    }

private:
    QString m_icon;
    bool m_active = false;
    bool m_dot = false;
    bool m_dark = false;
    qreal m_animScale = 1.0;
    QSequentialAnimationGroup* m_pulseGroup = nullptr;
    HomePalette m_pal = homePaletteFor(false);
};

// Contenedor de árbol: dibuja la guía vertical de 1 px (y guías por nivel en subcarpetas).
class SidebarTreeBox : public QWidget {
public:
    explicit SidebarTreeBox(QWidget* parent = nullptr)
        : QWidget(parent) {
        setAttribute(Qt::WA_StyledBackground, false);
        setAutoFillBackground(false);
    }

    QColor guide = tokens::lightPalette().guide;
    const QVector<SidebarNavRow*>* folderRows = nullptr;

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(QRectF(18, 2, 1, std::max(0, height() - 4)), guide);
        if (!folderRows)
            return;
        for (const SidebarNavRow* row : *folderRows) {
            if (!row || !row->isVisible())
                continue;
            const int maxLevel = std::min(row->meta().depth, 5);
            for (int d = 1; d <= maxLevel; ++d) {
                const qreal gx = 28.0 + 14.0 + (d - 1) * 18.0;
                const int gy = row->y();
                const int gh = std::min(row->height() + 1, std::max(0, height() - gy - 2));
                if (gh > 0)
                    painter.fillRect(QRectF(gx, gy, 1.0, gh), guide);
            }
        }
    }
};

// Divisor horizontal de 1 px pintado directamente (inmune a estilos del sistema).
class SidebarDivider : public QWidget {
public:
    explicit SidebarDivider(QWidget* parent = nullptr)
        : QWidget(parent) {
        setFixedHeight(1);
        setMinimumWidth(0);
    }
    QColor color = tokens::lightPalette().border;

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(), color);
    }
};

HomeSidebar::HomeSidebar(QWidget* parent)
    : QWidget(parent) {
    setFixedWidth(tokens::space::SidebarWidth);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    setAttribute(Qt::WA_StyledBackground, false);
    m_treeOpen = readAppSetting(QStringLiteral("home/foldersOpen"), false).toBool();
    m_tagsOpen = readAppSetting(QStringLiteral("home/tagsOpen"), true).toBool();

    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(12, 16, 12, 14);
    col->setSpacing(2);

    auto* head = new QHBoxLayout();
    head->setContentsMargins(8, 2, 4, 14);
    auto* brand = new QLabel(QStringLiteral("Finily"), this);
    brand->setObjectName(QStringLiteral("finilyBrandTitle"));
    brand->setMinimumWidth(0);
    head->addWidget(brand, 1, Qt::AlignVCenter);
    m_collapseBtn = new SidebarIconButton(QStringLiteral("side"), QStringLiteral("Ocultar barra lateral"), this);
    connect(m_collapseBtn, &SidebarIconButton::clicked, this, &HomeSidebar::collapseRequested);
    head->addWidget(m_collapseBtn, 0, Qt::AlignVCenter);
    col->addLayout(head);

    m_docsRow = new SidebarNavRow(QStringLiteral("docs"), QStringLiteral("Documentos"), this);
    connect(m_docsRow, &SidebarNavRow::clicked, this, [this] {
        setSelectedFilter(NavDocs);
        emit filterSelected(NavDocs);
    });
    col->addWidget(m_docsRow);

    m_favsRow = new SidebarNavRow(QStringLiteral("star"), QStringLiteral("Favoritos"), this);
    connect(m_favsRow, &SidebarNavRow::clicked, this, [this] {
        setSelectedFilter(NavFavs);
        emit filterSelected(NavFavs);
    });
    col->addWidget(m_favsRow);

    m_foldersRow = new SidebarNavRow(QStringLiteral("folder"), QStringLiteral("Carpetas"), this);
    connect(m_foldersRow, &SidebarNavRow::chevronClicked, this, [this] { setTreeOpen(!m_treeOpen); });
    connect(m_foldersRow, &SidebarNavRow::clicked, this, [this] {
        const bool alreadyFolders = (!m_settingsSelected && m_selectedFilter == NavFolders);
        setSelectedFilter(NavFolders);
        emit filterSelected(NavFolders);
        if (!m_treeOpen || alreadyFolders)
            setTreeOpen(!m_treeOpen);
    });
    col->addWidget(m_foldersRow);

    m_treeScroll = new QScrollArea(this);
    m_treeScroll->setWidgetResizable(true);
    m_treeScroll->setFrameShape(QFrame::NoFrame);
    m_treeScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_treeScroll->viewport()->setAutoFillBackground(false);
    m_treeScroll->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; }"));
    m_treeScroll->setMinimumWidth(0);
    m_treeScroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_treeBox = new SidebarTreeBox(m_treeScroll);
    m_treeBox->folderRows = &m_folderRows;
    m_treeLayout = new QVBoxLayout(m_treeBox);
    m_treeLayout->setContentsMargins(28, 2, 0, 2);
    m_treeLayout->setSpacing(2);
    m_treeScroll->setWidget(m_treeBox);
    col->addWidget(m_treeScroll);

    m_tagsHeaderRow = new SidebarNavRow(QStringLiteral("tag"), QStringLiteral("Etiquetas"), this);
    connect(m_tagsHeaderRow, &SidebarNavRow::clicked, this, [this] { setTagsOpen(!m_tagsOpen); });
    connect(m_tagsHeaderRow, &SidebarNavRow::chevronClicked, this, [this] { setTagsOpen(!m_tagsOpen); });
    m_tagsHeaderRow->hide();
    col->addWidget(m_tagsHeaderRow);

    m_tagsScroll = new QScrollArea(this);
    m_tagsScroll->setWidgetResizable(true);
    m_tagsScroll->setFrameShape(QFrame::NoFrame);
    m_tagsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_tagsScroll->viewport()->setAutoFillBackground(false);
    m_tagsScroll->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; }"));
    m_tagsScroll->setMinimumWidth(0);
    m_tagsScroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_tagsBox = new SidebarTreeBox(m_tagsScroll);
    m_tagsLayout = new QVBoxLayout(m_tagsBox);
    m_tagsLayout->setContentsMargins(28, 2, 0, 2);
    m_tagsLayout->setSpacing(2);
    m_tagsScroll->setWidget(m_tagsBox);
    m_tagsScroll->hide();
    col->addWidget(m_tagsScroll);

    col->addStretch(1);

    m_updateBanner = new UpdateBanner(this);
    m_updateBanner->hide();
    col->addWidget(m_updateBanner);

    m_draftRow = new SidebarNavRow(QStringLiteral("restore"), QStringLiteral("Continuar borrador"), this);
    m_draftRow->setToolTip(QStringLiteral("Recuperar la última sesión autoguardada"));
    m_draftRow->hide();
    connect(m_draftRow, &SidebarNavRow::clicked, this, &HomeSidebar::continueDraftRequested);
    col->addWidget(m_draftRow);

    m_trashRow = new SidebarNavRow(QStringLiteral("trash"), QStringLiteral("Papelera"), this);
    connect(m_trashRow, &SidebarNavRow::clicked, this, [this] {
        setSelectedFilter(NavTrash);
        emit filterSelected(NavTrash);
    });
    col->addWidget(m_trashRow);

    col->addSpacing(9);
    m_footLine = new SidebarDivider(this);
    col->addWidget(m_footLine);
    col->addSpacing(9);

    auto* icons = new QHBoxLayout();
    icons->setContentsMargins(4, 0, 4, 0);
    icons->setSpacing(6);
    m_themeBtn = new SidebarIconButton(QStringLiteral("moon"), QStringLiteral("Modo oscuro"), this);
    connect(m_themeBtn, &SidebarIconButton::clicked, this, [this] { emit darkModeToggled(!m_dark); });
    icons->addWidget(m_themeBtn);

    m_settingsBtn = new SidebarIconButton(QStringLiteral("gear"), QStringLiteral("Ajustes"), this);
    connect(m_settingsBtn, &SidebarIconButton::clicked, this, &HomeSidebar::settingsRequested);
    icons->addWidget(m_settingsBtn);
    icons->addStretch(1);
    col->addLayout(icons);

    setTreeOpen(m_treeOpen);
    setTagsOpen(m_tagsOpen);
    refreshActive();
}

void HomeSidebar::updateScrollBounds(QScrollArea* scroll, QWidget* box) {
    if (!scroll || !box)
        return;
    const int rowCount = (box == m_treeBox) ? int(m_folderRows.size()) : int(m_tagRows.size());
    const int naturalH = rowCount > 0 ? (rowCount * 34 + 4) : 0;
    box->setMinimumWidth(0);
    box->setMinimumHeight(naturalH);
    scroll->setMinimumHeight(rowCount > 0 ? std::min(naturalH, 195) : 0);
    scroll->setMaximumHeight(std::min(naturalH, 260));
    if (box->layout())
        box->layout()->invalidate();
    box->updateGeometry();
}

void HomeSidebar::setTreeOpen(bool open) {
    m_treeOpen = open;
    writeAppSetting(QStringLiteral("home/foldersOpen"), open);
    m_foldersRow->setTrailing(open ? QStringLiteral("chevD") : QStringLiteral("chevR"));
    updateScrollBounds(m_treeScroll, m_treeBox);
    m_treeScroll->setVisible(open && !m_folderRows.isEmpty());
}

void HomeSidebar::setTagsOpen(bool open) {
    m_tagsOpen = open;
    writeAppSetting(QStringLiteral("home/tagsOpen"), open);
    const bool hasTags = !m_tags.isEmpty();
    m_tagsHeaderRow->setVisible(hasTags);
    m_tagsHeaderRow->setTrailing(open ? QStringLiteral("chevD") : QStringLiteral("chevR"));
    updateScrollBounds(m_tagsScroll, m_tagsBox);
    m_tagsScroll->setVisible(hasTags && open);
}

void HomeSidebar::refreshActive() {
    m_docsRow->setActive(!m_settingsSelected && m_selectedFilter == NavDocs);
    m_favsRow->setActive(!m_settingsSelected && m_selectedFilter == NavFavs);
    m_foldersRow->setActive(!m_settingsSelected && m_selectedFilter == NavFolders);
    m_trashRow->setActive(!m_settingsSelected && m_selectedFilter == NavTrash);
    m_settingsBtn->setActive(m_settingsSelected);

    const bool folderSelectable = !m_settingsSelected && m_selectedFilter < 0 && m_selectedTag.isEmpty();
    for (SidebarNavRow* row : m_folderRows)
        row->setActive(folderSelectable && row->meta().relativePath == m_selectedFolder);

    const bool tagSelectable = !m_settingsSelected && m_selectedFilter < 0 && !m_selectedTag.isEmpty();
    for (SidebarNavRow* row : m_tagRows)
        row->setActive(tagSelectable && row->tag().id == m_selectedTag);
}

void HomeSidebar::setUpdateDotVisible(bool visible) {
    if (m_updateDotVisible == visible)
        return;
    m_updateDotVisible = visible;
    if (m_settingsBtn)
        m_settingsBtn->setDot(visible);
}

void HomeSidebar::setSelectedFilter(int filterIndex) {
    m_selectedFilter = (filterIndex >= NavDocs && filterIndex <= NavTrash) ? filterIndex : NavDocs;
    m_selectedFolder.clear();
    m_selectedTag.clear();
    m_settingsSelected = false;
    refreshActive();
}

void HomeSidebar::setSelectedFolder(const QString& relativePath) {
    m_selectedFilter = -1;
    m_selectedFolder = relativePath;
    m_selectedTag.clear();
    m_settingsSelected = false;
    refreshActive();
}

void HomeSidebar::setSelectedTag(const QString& tagId) {
    m_selectedFilter = -1;
    m_selectedFolder.clear();
    m_selectedTag = tagId;
    m_settingsSelected = false;
    if (!tagId.isEmpty() && !m_tagsOpen)
        setTagsOpen(true);
    refreshActive();
}

void HomeSidebar::setSelectedSettings() {
    m_selectedFilter = -1;
    m_selectedFolder.clear();
    m_selectedTag.clear();
    m_settingsSelected = true;
    refreshActive();
}

void HomeSidebar::setFolders(const QVector<FolderMeta>& folders) {
    for (SidebarNavRow* oldRow : m_folderRows) {
        if (!oldRow) continue;
        m_treeLayout->removeWidget(oldRow);
        oldRow->hide();
        delete oldRow;
    }
    m_folderRows.clear();
    for (const FolderMeta& meta : folders) {
        auto* row = new SidebarNavRow(QString(), meta.name, m_treeBox);
        row->makeTreeItem(meta);
        row->setTheme(m_pal, m_dark);
        row->setDropHighlight(!m_dropHighlightFolder.isEmpty() && meta.relativePath == m_dropHighlightFolder);
        const QString rel = meta.relativePath;
        connect(row, &SidebarNavRow::clicked, this, [this, rel] {
            setSelectedFolder(rel);
            emit folderSelected(rel);
        });
        connect(row, &SidebarNavRow::menuRequested, this, &HomeSidebar::folderMenuRequested);
        m_treeLayout->addWidget(row);
        row->show();
        m_folderRows.append(row);
    }
    updateScrollBounds(m_treeScroll, m_treeBox);
    setTreeOpen(m_treeOpen);
    refreshActive();
}

void HomeSidebar::setTags(const QVector<TagInfo>& tags) {
    m_tags = tags;
    for (SidebarNavRow* oldRow : m_tagRows) {
        if (!oldRow) continue;
        m_tagsLayout->removeWidget(oldRow);
        oldRow->hide();
        delete oldRow;
    }
    m_tagRows.clear();
    for (const TagInfo& tag : tags) {
        auto* row = new SidebarNavRow(QString(), tag.name, m_tagsBox);
        row->makeTagItem(tag);
        row->setTheme(m_pal, m_dark);
        const QString tagId = tag.id;
        connect(row, &SidebarNavRow::clicked, this, [this, tagId] {
            setSelectedTag(tagId);
            emit tagSelected(tagId);
        });
        connect(row, &SidebarNavRow::tagMenuRequested, this, &HomeSidebar::tagMenuRequested);
        m_tagsLayout->addWidget(row);
        row->show();
        m_tagRows.append(row);
    }
    updateScrollBounds(m_tagsScroll, m_tagsBox);
    setTagsOpen(m_tagsOpen);
    refreshActive();
}

void HomeSidebar::setDraftVisible(bool visible) {
    m_draftRow->setVisible(visible);
}

void HomeSidebar::setTrashCount(int count) {
    m_trashRow->setCount(count);
}

void HomeSidebar::setDropHighlightFolder(const QString& relativePath) {
    if (m_dropHighlightFolder == relativePath)
        return;
    m_dropHighlightFolder = relativePath;
    for (SidebarNavRow* row : m_folderRows) {
        row->setDropHighlight(!relativePath.isEmpty() && row->meta().relativePath == relativePath);
    }
}

QString HomeSidebar::folderAtGlobalPos(const QPoint& globalPos) const {
    if (!isVisible() || !m_treeScroll->isVisible())
        return {};
    const QPoint vpPos = m_treeScroll->viewport()->mapFromGlobal(globalPos);
    if (!m_treeScroll->viewport()->rect().contains(vpPos))
        return {};
    const QPoint boxPos = m_treeBox->mapFromGlobal(globalPos);
    for (const SidebarNavRow* row : m_folderRows) {
        if (row->isVisible() && row->geometry().contains(boxPos))
            return row->meta().relativePath;
    }
    return {};
}

void HomeSidebar::setPanelWidth(int width) {
    const int clamped = std::clamp(width, 0, tokens::space::SidebarWidth);
    if (m_panelWidth == clamped && this->width() == clamped)
        return;
    m_panelWidth = clamped;
    setFixedWidth(clamped);
}

QPoint HomeSidebar::themeButtonCenterIn(QWidget* ancestor) const {
    if (!m_themeBtn)
        return {};
    const QPoint c = m_themeBtn->rect().center();
    return ancestor ? m_themeBtn->mapTo(ancestor, c) : m_themeBtn->mapToGlobal(c);
}

void HomeSidebar::pulseThemeButton() {
    if (m_themeBtn)
        m_themeBtn->playPulse();
}

QPoint HomeSidebar::trashIconCenterIn(QWidget* ancestor) const {
    if (!m_trashRow)
        return {};
    const QPoint c(19, m_trashRow->height() / 2);
    return ancestor ? m_trashRow->mapTo(ancestor, c) : m_trashRow->mapToGlobal(c);
}

void HomeSidebar::bounceTrashIcon() {
    if (m_trashRow)
        m_trashRow->playIconBounce();
}

void HomeSidebar::applyTheme(const HomePalette& pal, bool dark) {
    m_pal = pal;
    m_dark = dark;
    const tokens::Palette tk = tokens::paletteFor(dark);

    for (SidebarNavRow* row : {m_docsRow, m_favsRow, m_foldersRow, m_tagsHeaderRow, m_draftRow, m_trashRow})
        row->setTheme(pal, dark);
    for (SidebarNavRow* row : m_folderRows)
        row->setTheme(pal, dark);
    for (SidebarNavRow* row : m_tagRows)
        row->setTheme(pal, dark);
    for (SidebarIconButton* btn : {m_collapseBtn, m_themeBtn, m_settingsBtn})
        btn->setTheme(pal, dark);

    m_themeBtn->setIcon(dark ? QStringLiteral("sun") : QStringLiteral("moon"));
    m_themeBtn->setToolTip(dark ? QStringLiteral("Modo claro") : QStringLiteral("Modo oscuro"));
    m_settingsBtn->setDot(m_updateDotVisible);

    m_treeBox->guide = tk.guide;
    m_treeBox->update();
    m_tagsBox->guide = tk.guide;
    m_tagsBox->update();

    m_footLine->color = pal.sidebarBorder;
    m_footLine->update();

    if (m_updateBanner)
        m_updateBanner->applyTheme(pal, dark);

    update();
}

void HomeSidebar::paintEvent(QPaintEvent*) {
    if (width() <= 0)
        return;
    QPainter painter(this);
    painter.fillRect(rect(), m_pal.sidebar);
    painter.fillRect(QRect(width() - 1, 0, 1, height()), m_pal.sidebarBorder);
}

} // namespace finily

#include "HomeSidebar.moc"
