#include "ui/home/GlassSheet.h"

#include "ui/DesignTokens.h"
#include "ui/home/SheetWidgets.h"

#include <QApplication>
#include <QButtonGroup>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace finily {
namespace {

QFont sheetFont(qreal px, QFont::Weight weight) {
    QFont f = QApplication::font();
    f.setPixelSize(qRound(px));
    f.setWeight(weight);
    return f;
}

QIcon makeSwatchIcon(const QColor& swatch, const QColor& accent) {
    auto render = [&](bool selected) {
        QPixmap pm(56, 56);
        pm.setDevicePixelRatio(2.0);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing, true);
        if (selected) {
            p.setPen(QPen(accent, 2.0));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(QRectF(2.0, 2.0, 24.0, 24.0));
        }
        p.setPen(QPen(QColor(0, 0, 0, 34), 0.8));
        p.setBrush(swatch);
        p.drawEllipse(QRectF(5.0, 5.0, 18.0, 18.0));
        if (selected) {
            p.setPen(QPen(Qt::white, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawLine(QPointF(10.5, 14.2), QPointF(13.0, 16.6));
            p.drawLine(QPointF(13.0, 16.6), QPointF(17.6, 11.4));
        }
        return pm;
    };
    QIcon icon;
    icon.addPixmap(render(false), QIcon::Normal, QIcon::Off);
    icon.addPixmap(render(true), QIcon::Normal, QIcon::On);
    return icon;
}

QIcon makeThemeMiniIcon(ThemeId id) {
    const ThemeConfig tc = getThemeConfig(id);
    QPixmap pm(124, 68);
    pm.setDevicePixelRatio(2.0);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF r(1.0, 1.0, 60.0, 32.0);
    p.setPen(QPen(tc.isDark ? QColor(255, 255, 255, 40) : QColor(15, 23, 42, 30), 1.0));
    p.setBrush(tc.background);
    p.drawRoundedRect(r, 6.0, 6.0);
    p.setPen(Qt::NoPen);
    p.setBrush(tc.patternColor);
    for (int y = 8; y <= 26; y += 9) {
        for (int x = 10; x <= 52; x += 10)
            p.drawEllipse(QPointF(x, y), 0.85, 0.85);
    }
    QPainterPath wave;
    wave.moveTo(11.0, 22.0);
    wave.cubicTo(20.0, 11.0, 33.0, 24.0, 48.0, 12.0);
    p.setPen(QPen(tc.defaultInk, 1.6, Qt::SolidLine, Qt::RoundCap));
    p.setBrush(Qt::NoBrush);
    p.drawPath(wave);
    return QIcon(pm);
}

} // namespace

GlassSheet::GlassSheet(QWidget* parent)
    : QWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_StyledBackground, false);

    m_cardWidget = new QWidget(this);
    m_cardWidget->setAttribute(Qt::WA_StyledBackground, false);
    m_cardWidget->setAutoFillBackground(false);
    m_cardOpacity = new QGraphicsOpacityEffect(m_cardWidget);
    m_cardOpacity->setOpacity(1.0);
    m_cardOpacity->setEnabled(false);
    m_cardWidget->setGraphicsEffect(m_cardOpacity);

    auto* root = new QVBoxLayout(m_cardWidget);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* header = new QWidget(m_cardWidget);
    header->setFixedHeight(52);
    auto* headerRow = new QHBoxLayout(header);
    headerRow->setContentsMargins(16, 10, 16, 6);
    headerRow->setSpacing(8);

    m_cancelBtn = new QToolButton(header);
    m_cancelBtn->setObjectName(QStringLiteral("finilySheetCancel"));
    m_cancelBtn->setText(QStringLiteral("Cancelar"));
    m_cancelBtn->setFont(sheetFont(12.5, QFont::DemiBold));
    m_cancelBtn->setFixedHeight(32);
    m_cancelBtn->setCursor(Qt::PointingHandCursor);
    m_cancelBtn->setFocusPolicy(Qt::NoFocus);
    connect(m_cancelBtn, &QToolButton::clicked, this, [this] { closeSheet(false); });
    headerRow->addWidget(m_cancelBtn, 0, Qt::AlignVCenter);

    m_titleLabel = new QLabel(header);
    m_titleLabel->setObjectName(QStringLiteral("finilySheetTitle"));
    m_titleLabel->setFont(sheetFont(15, QFont::Bold));
    m_titleLabel->setAlignment(Qt::AlignCenter);
    headerRow->addWidget(m_titleLabel, 1, Qt::AlignVCenter);

    m_confirmBtn = new QToolButton(header);
    m_confirmBtn->setObjectName(QStringLiteral("finilySheetConfirm"));
    m_confirmBtn->setText(QStringLiteral("Crear"));
    m_confirmBtn->setFont(sheetFont(12.5, QFont::Bold));
    m_confirmBtn->setFixedHeight(32);
    m_confirmBtn->setCursor(Qt::PointingHandCursor);
    m_confirmBtn->setFocusPolicy(Qt::NoFocus);
    connect(m_confirmBtn, &QToolButton::clicked, this, [this] {
        if (validateBeforeAccept())
            onConfirmed();
    });
    headerRow->addWidget(m_confirmBtn, 0, Qt::AlignVCenter);
    m_header = header;
    root->addWidget(header);

    m_bodyLayout = new QVBoxLayout();
    m_bodyLayout->setContentsMargins(22, 4, 22, 20);
    m_bodyLayout->setSpacing(9);
    root->addLayout(m_bodyLayout);

    m_anim = new QPropertyAnimation(this, "progress", this);
    connect(m_anim, &QPropertyAnimation::finished, this, [this] {
        if (m_cardOpacity)
            m_cardOpacity->setEnabled(false);
        if (!m_closing)
            return;
        m_closing = false;
        const bool accepted = m_pendingAccept;
        m_pendingAccept = false;
        m_blurredBg = QImage();
        hide();
        emit closed();
        if (!accepted)
            emit cancelled();
    });
    hide();
}

void GlassSheet::configureHeader(const QString& title, const QString& confirmText) {
    if (m_titleLabel)
        m_titleLabel->setText(title);
    if (m_confirmBtn)
        m_confirmBtn->setText(confirmText);
}

void GlassSheet::setCardWidth(int width) {
    m_cardWidth = std::max(340, width);
}

void GlassSheet::setSideCard(bool side) {
    m_side = side;
    m_cardWidth = tokens::space::FloatCardWidth;
    if (m_header)
        m_header->setVisible(!side);
}

void GlassSheet::setHeaderVisible(bool visible) {
    if (m_header)
        m_header->setVisible(visible);
}

void GlassSheet::setBodyMargins(int l, int t, int r, int b) {
    m_bodyLayout->setContentsMargins(l, t, r, b);
}

int GlassSheet::bodySpacing() const {
    return m_bodyLayout->spacing();
}

void GlassSheet::setProgress(qreal p) {
    m_progress = std::clamp(p, 0.0, 1.0);
    const bool reduced = tokens::reducedMotion();
    const qreal slideX = m_closing ? 16.0 : 24.0;
    const qreal slideY = m_closing ? 10.0 : 14.0;
    const int dx = (!reduced && m_side) ? qRound((1.0 - m_progress) * slideX) : 0;
    const int dy = (!reduced && !m_side) ? qRound((1.0 - m_progress) * slideY) : 0;
    if (m_cardWidget && !m_cardRect.isEmpty())
        m_cardWidget->move(m_cardRect.left() + dx, m_cardRect.top() + dy);
    if (m_cardOpacity) {
        if (m_progress < 0.999) {
            if (!m_cardOpacity->isEnabled())
                m_cardOpacity->setEnabled(true);
            m_cardOpacity->setOpacity(m_progress);
        } else {
            m_cardOpacity->setOpacity(1.0);
            m_cardOpacity->setEnabled(false);
        }
    }
    update();
}

void GlassSheet::openSheet(const HomePalette& pal, bool dark) {
    m_anim->stop();
    m_closing = false;
    m_pendingAccept = false;
    setAttribute(Qt::WA_TransparentForMouseEvents, false);

    m_pal = pal;
    m_dark = dark;
    setStyleSheet(sheetStylesheet());
    refreshSheetWidgetsTheme(m_cardWidget, dark);
    if (isVisible())
        hide();
    updateCardGeometry(true);
    setProgress(0.0);
    show();
    raise();

    m_anim->setDuration(tokens::motion::duration(m_side ? tokens::motion::Slow : tokens::motion::Base));
    m_anim->setEasingCurve(tokens::motion::curve(tokens::motion::out()));
    m_anim->setStartValue(0.0);
    m_anim->setEndValue(1.0);
    m_anim->start();
}

void GlassSheet::closeSheet(bool accepted) {
    if (!isVisible() || m_closing)
        return;
    onClosing();
    m_closing = true;
    m_pendingAccept = accepted;
    setAttribute(Qt::WA_TransparentForMouseEvents, true);

    m_anim->stop();
    m_anim->setDuration(tokens::motion::duration(tokens::motion::Fast));
    m_anim->setEasingCurve(tokens::motion::curve(tokens::motion::in()));
    m_anim->setStartValue(m_progress);
    m_anim->setEndValue(0.0);
    m_anim->start();
}

void GlassSheet::repositionOnResize() {
    if (!isVisible())
        return;
    updateCardGeometry(false);
    setProgress(m_progress);
}

void GlassSheet::updateCardGeometry(bool grabBackdrop) {
    if (!parentWidget())
        return;
    setGeometry(parentWidget()->rect());
    m_cardWidget->setFixedWidth(m_cardWidth);
    m_cardWidget->adjustSize();
    const int topGap = m_side ? tokens::space::HeaderHeight + 4 : 16;
    const int cardH = std::min(m_cardWidget->sizeHint().height(), std::max(260, height() - topGap - 16));
    const int x = m_side ? width() - 18 - m_cardWidth : std::max(12, (width() - m_cardWidth) / 2);
    const int y = m_side ? topGap : std::max(12, (height() - cardH) / 2);
    m_cardRect = QRect(x, y, m_cardWidth, cardH);
    m_cardWidget->setGeometry(m_cardRect);
    if (grabBackdrop && parentWidget()->isVisible() && !m_cardRect.isEmpty() && !tokens::reducedMotion()) {
        m_grabbingBackdrop = true;
        const QPixmap snap = parentWidget()->grab(m_cardRect);
        m_grabbingBackdrop = false;
        m_blurredBg = blurBackdropImage(snap.toImage(), 12);
    } else if (tokens::reducedMotion()) {
        m_blurredBg = QImage();
    }
}

QString GlassSheet::sheetStylesheet() const {
    return QStringLiteral(R"(
QLabel#finilySheetSection { color: %1; background: transparent; font-size: 11px; font-weight: 700; letter-spacing: 0.4px; }
QLabel#finilySheetTitle { color: %2; background: transparent; }
QLineEdit#finilySheetInput {
    background: %4; border: 1.3px solid %5; border-radius: 14px;
    color: %2; padding: 8px 14px; font-size: 14px; font-weight: 500;
    selection-background-color: %9; selection-color: %2;
}
QLineEdit#finilySheetInput:focus { border: 1.5px solid %3; }
QToolButton#finilySheetCancel {
    background: %4; border: 1px solid %5; border-radius: 16px;
    color: %2; padding: 0px 14px;
}
QToolButton#finilySheetCancel:hover { border: 1px solid %3; }
QToolButton#finilySheetConfirm {
    background: %3; border: 1px solid %3; border-radius: 16px;
    color: %8; padding: 0px 16px;
}
QToolButton#finilySheetConfirm:hover { background: %7; }
QToolButton#finilySheetPill {
    background: %4; border: 1.2px solid %5; border-radius: 10px;
    color: %2; padding: 5px 8px; font-size: 12px; font-weight: 600;
}
QToolButton#finilySheetPill:hover { border: 1.2px solid %3; }
QToolButton#finilySheetPill:checked { background: %6; border: 1.5px solid %3; color: %2; font-weight: 700; }
QToolButton#finilySheetPill:disabled { color: %1; border: 1px solid %5; }
QToolButton#finilySheetTheme {
    background: %4; border: 1.2px solid %5; border-radius: 11px;
    color: %2; padding: 6px 4px; font-size: 11px; font-weight: 600;
}
QToolButton#finilySheetTheme:checked { background: %6; border: 1.8px solid %3; font-weight: 700; }
QToolButton#finilySheetSwatch { background: transparent; border: none; border-radius: 15px; padding: 0px; }
)")
        .arg(m_pal.muted.name(QColor::HexArgb))
        .arg(m_pal.text.name(QColor::HexArgb))
        .arg(m_pal.accent.name(QColor::HexArgb))
        .arg(m_dark ? QStringLiteral("rgba(255,255,255,14)") : QStringLiteral("rgba(255,255,255,165)"))
        .arg(m_pal.border.name(QColor::HexArgb))
        .arg(m_pal.tint.name(QColor::HexArgb))
        .arg(m_pal.accentHover.name(QColor::HexArgb))
        .arg(m_pal.accentFg.name(QColor::HexArgb))
        .arg(m_dark ? QStringLiteral("rgba(255,255,255,60)") : QStringLiteral("rgba(24,24,27,36)"));
}

QLabel* GlassSheet::makeSectionLabel(const QString& text) {
    auto* l = new QLabel(text, m_cardWidget);
    l->setObjectName(QStringLiteral("finilySheetSection"));
    l->setFont(sheetFont(11, QFont::Bold));
    return l;
}

QLineEdit* GlassSheet::makeNameField(const QString& placeholder, const QString& initialText) {
    auto* edit = new QLineEdit(m_cardWidget);
    edit->setObjectName(QStringLiteral("finilySheetInput"));
    edit->setFont(sheetFont(14, QFont::Medium));
    edit->setFixedHeight(38);
    edit->setPlaceholderText(placeholder);
    edit->setText(initialText);
    connect(edit, &QLineEdit::returnPressed, this, [this] {
        if (validateBeforeAccept())
            onConfirmed();
    });
    return edit;
}

QWidget* GlassSheet::makeColorSwatchRow(int initialIndex, const std::function<void(int)>& onSelect,
                                        QButtonGroup** outGroup) {
    auto* holder = new QWidget(m_cardWidget);
    auto* row = new QHBoxLayout(holder);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(6);
    auto* group = new QButtonGroup(holder);
    group->setExclusive(true);
    const auto& palette = coverColors();
    const auto& names = coverColorNames();
    for (int i = 0; i < palette.size(); ++i) {
        auto* btn = new QToolButton(holder);
        btn->setObjectName(QStringLiteral("finilySheetSwatch"));
        btn->setFixedSize(30, 30);
        btn->setIconSize(QSize(28, 28));
        btn->setIcon(makeSwatchIcon(palette.at(i), m_pal.accent));
        btn->setToolTip(names.value(i));
        btn->setCheckable(true);
        btn->setChecked(i == initialIndex);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFocusPolicy(Qt::NoFocus);
        group->addButton(btn, i);
        row->addWidget(btn);
        connect(btn, &QToolButton::clicked, this, [onSelect, i] { onSelect(i); });
    }
    row->addStretch(1);
    if (outGroup)
        *outGroup = group;
    return holder;
}

QWidget* GlassSheet::makeSymbolIconRow(int initialIndex, const std::function<void(int)>& onSelect,
                                       QButtonGroup** outGroup) {
    auto* holder = new QWidget(m_cardWidget);
    auto* row = new QHBoxLayout(holder);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(6);
    auto* group = new QButtonGroup(holder);
    group->setExclusive(true);
    const auto& names = symbolNames();
    for (int i = 0; i < names.size(); ++i) {
        auto* btn = new QToolButton(holder);
        btn->setObjectName(QStringLiteral("finilySheetPill"));
        btn->setFixedSize(36, 34);
        btn->setIconSize(QSize(20, 20));
        btn->setIcon(symbolGlyph(i, m_pal.text));
        btn->setToolTip(names.at(i));
        btn->setCheckable(true);
        btn->setChecked(i == initialIndex);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFocusPolicy(Qt::NoFocus);
        group->addButton(btn, i);
        row->addWidget(btn);
        connect(btn, &QToolButton::clicked, this, [onSelect, i] { onSelect(i); });
    }
    row->addStretch(1);
    if (outGroup)
        *outGroup = group;
    return holder;
}

QWidget* GlassSheet::makeThemeCardRow(bool boardOrder, ThemeId initialTheme,
                                      const std::function<void(ThemeId)>& onSelect,
                                      QButtonGroup** outGroup) {
    auto* holder = new QWidget(m_cardWidget);
    auto* row = new QHBoxLayout(holder);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(7);
    auto* group = new QButtonGroup(holder);
    group->setExclusive(true);
    const QVector<QPair<QString, ThemeId>> themes = boardOrder
        ? QVector<QPair<QString, ThemeId>>{{QStringLiteral("Oscura"), ThemeId::DarkBoard},
                                           {QStringLiteral("Academia"), ThemeId::GreenBoard},
                                           {QStringLiteral("Blanco"), ThemeId::PaperLight},
                                           {QStringLiteral("Crema"), ThemeId::PaperCream}}
        : QVector<QPair<QString, ThemeId>>{{QStringLiteral("Blanco"), ThemeId::PaperLight},
                                           {QStringLiteral("Crema"), ThemeId::PaperCream},
                                           {QStringLiteral("Oscura"), ThemeId::DarkBoard},
                                           {QStringLiteral("Academia"), ThemeId::GreenBoard}};
    for (int i = 0; i < themes.size(); ++i) {
        auto* btn = new QToolButton(holder);
        btn->setObjectName(QStringLiteral("finilySheetTheme"));
        btn->setFont(sheetFont(11, QFont::DemiBold));
        btn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        btn->setIcon(makeThemeMiniIcon(themes.at(i).second));
        btn->setIconSize(QSize(62, 34));
        btn->setText(themes.at(i).first);
        btn->setFixedHeight(66);
        btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        btn->setCheckable(true);
        btn->setChecked(themes.at(i).second == initialTheme);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFocusPolicy(Qt::NoFocus);
        group->addButton(btn, int(themes.at(i).second));
        row->addWidget(btn);
        const ThemeId th = themes.at(i).second;
        connect(btn, &QToolButton::clicked, this, [onSelect, th] { onSelect(th); });
    }
    if (outGroup)
        *outGroup = group;
    return holder;
}

QWidget* GlassSheet::makePillRow(const QVector<QPair<QString, int>>& options, int initialValue,
                                 const std::function<void(int)>& onSelect,
                                 QButtonGroup** outGroup, QVector<QToolButton*>* outButtons) {
    auto* holder = new QWidget(m_cardWidget);
    auto* row = new QHBoxLayout(holder);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(7);
    auto* group = new QButtonGroup(holder);
    group->setExclusive(true);
    for (const auto& opt : options) {
        auto* btn = new QToolButton(holder);
        btn->setObjectName(QStringLiteral("finilySheetPill"));
        btn->setFont(sheetFont(12, QFont::DemiBold));
        btn->setText(opt.first);
        btn->setFixedHeight(32);
        btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        btn->setCheckable(true);
        btn->setChecked(opt.second == initialValue);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFocusPolicy(Qt::NoFocus);
        group->addButton(btn, opt.second);
        row->addWidget(btn);
        if (outButtons)
            outButtons->append(btn);
        const int val = opt.second;
        connect(btn, &QToolButton::clicked, this, [onSelect, val] { onSelect(val); });
    }
    if (outGroup)
        *outGroup = group;
    return holder;
}

void GlassSheet::paintEvent(QPaintEvent*) {
    if (m_grabbingBackdrop)
        return;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    if (!m_side) {
        const QColor scrim = m_dark ? QColor(8, 10, 16, int(125 * m_progress))
                                    : QColor(18, 22, 34, int(58 * m_progress));
        painter.fillRect(rect(), scrim);
    }

    const bool reduced = tokens::reducedMotion();
    const qreal slideX = m_closing ? 16.0 : 24.0;
    const qreal slideY = m_closing ? 10.0 : 14.0;
    const qreal dx = (!reduced && m_side) ? (1.0 - m_progress) * slideX : 0.0;
    const qreal dy = (!reduced && !m_side) ? (1.0 - m_progress) * slideY : 0.0;
    painter.setOpacity(m_progress);
    paintLiquidGlassPanel(painter, QRectF(m_cardRect).translated(dx, dy), tokens::radius::Card,
                          m_blurredBg, m_pal, m_dark);
}

void GlassSheet::mousePressEvent(QMouseEvent* event) {
    if (m_closing)
        return;
    if (event->button() == Qt::LeftButton && !m_cardRect.contains(event->pos()))
        closeSheet(false);
}

void GlassSheet::keyPressEvent(QKeyEvent* event) {
    if (m_closing)
        return;
    if (event->key() == Qt::Key_Escape) {
        closeSheet(false);
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

} // namespace finily
