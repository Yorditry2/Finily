#include "ui/home/UpdateBanner.h"

#include "ui/DesignTokens.h"
#include "ui/UiIcons.h"

#include <QFontMetricsF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>

#include <algorithm>
#include <cmath>

namespace finily {

UpdateBanner::UpdateBanner(QWidget* parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("finilyUpdateCard"));
    setAttribute(Qt::WA_StyledBackground, false);
    setMouseTracking(true);
    setFocusPolicy(Qt::NoFocus);
    setMinimumWidth(0);
    hide();
}

QFont UpdateBanner::uiFont(qreal px, int weight) const {
    QFont f = font();
    f.setPixelSize(std::max(1, int(std::lround(px))));
    f.setWeight(QFont::Weight(weight));
    return f;
}

void UpdateBanner::applyTheme(const HomePalette& pal, bool dark) {
    m_pal = pal;
    m_dark = dark;
    update();
}

void UpdateBanner::showUpdate(const QString& version, bool canInstallInApp, const QString& summary) {
    m_version = version.trimmed();
    m_canInstall = canInstallInApp;
    m_summary = summary.trimmed();
    m_error.clear();
    m_progress = 0;
    m_state = State::Ready;
    relayout(width());
    show();
    update();
}

void UpdateBanner::setDownloadProgress(int percent) {
    m_progress = std::clamp(percent, 0, 100);
    m_state = State::Downloading;
    relayout(width());
    if (!isVisible())
        show();
    update();
}

void UpdateBanner::showReadyToInstall(const QString& version) {
    if (!version.trimmed().isEmpty())
        m_version = version.trimmed();
    m_error.clear();
    m_state = State::ReadyToInstall;
    relayout(width());
    show();
    update();
}

void UpdateBanner::showError(const QString& msg) {
    m_error = msg.trimmed().isEmpty() ? QStringLiteral("No se pudo verificar la descarga.") : msg.trimmed();
    m_state = State::Error;
    relayout(width());
    show();
    update();
}

void UpdateBanner::resetState() {
    m_progress = 0;
    m_error.clear();
    if (m_version.isEmpty()) {
        hideBanner();
        return;
    }
    m_state = State::Ready;
    relayout(width());
    update();
}

void UpdateBanner::hideBanner() {
    m_state = State::Hidden;
    m_hoveredHit = -1;
    setCursor(Qt::ArrowCursor);
    setToolTip({});
    hide();
}

QSize UpdateBanner::sizeHint() const {
    return {194, std::max(0, m_totalHeight)};
}

QSize UpdateBanner::minimumSizeHint() const {
    return {0, std::max(0, m_totalHeight)};
}

void UpdateBanner::relayout(int availableWidth) {
    m_hits.clear();
    m_titleRect = {};
    m_percentRect = {};
    m_bodyRect = {};
    m_pbarRect = {};
    m_titleText.clear();
    m_bodyText.clear();
    m_percentText.clear();

    if (m_state == State::Hidden) {
        m_totalHeight = 0;
        setFixedHeight(0);
        updateGeometry();
        return;
    }

    const int w = availableWidth > 20 ? availableWidth : 194;
    const qreal cardX = 2.0;
    const qreal cardW = std::max<qreal>(140.0, w - 4.0);
    const qreal padX = 11.0;
    const qreal padY = 10.0;
    const qreal innerW = std::max<qreal>(80.0, cardW - 2.0 * padX);
    const qreal leftX = cardX + padX;

    const QFont fTitle = uiFont(11.5, QFont::DemiBold);
    const QFont fBody = uiFont(11.0, QFont::Normal);
    const QFont fBtn = uiFont(11.5, QFont::DemiBold);
    const QFont fPct = uiFont(11.0, QFont::Medium);
    const QFontMetricsF fmTitle(fTitle);
    const QFontMetricsF fmBody(fBody);
    const QFontMetricsF fmBtn(fBtn);
    const QFontMetricsF fmPct(fPct);

    bool hasDismiss = false;
    bool hasPercent = false;

    switch (m_state) {
    case State::Ready:
        m_titleText = m_version.isEmpty()
            ? QStringLiteral("Actualización disponible")
            : QStringLiteral("Finily %1 disponible").arg(m_version);
        m_bodyText = m_summary;
        hasDismiss = true;
        break;
    case State::Downloading:
        m_titleText = m_progress >= 100
            ? QStringLiteral("Verificando %1").arg(m_version)
            : QStringLiteral("Descargando %1").arg(m_version);
        m_percentText = QStringLiteral("%1 %").arg(m_progress);
        hasPercent = true;
        break;
    case State::ReadyToInstall:
        m_titleText = QStringLiteral("Lista para instalar");
        m_bodyText = QStringLiteral("Se aplicará al cerrar Finily. Tus cuadernos están guardados.");
        break;
    case State::Error:
        m_titleText = QStringLiteral("No se pudo actualizar");
        m_bodyText = m_error;
        hasDismiss = true;
        break;
    case State::Hidden:
        break;
    }

    qreal y = 1.0 + padY;
    m_iconRect = QRectF(leftX, y + 1.0, 14.0, 14.0);

    qreal rightReserve = 0.0;
    if (hasDismiss) {
        rightReserve = 20.0;
        m_hits.append({QRectF(leftX + innerW - 18.0, y - 2.0, 20.0, 20.0), {}, HitAction::Dismiss, false, true});
    } else if (hasPercent) {
        const qreal pctW = std::ceil(fmPct.horizontalAdvance(m_percentText)) + 4.0;
        rightReserve = pctW + 4.0;
        m_percentRect = QRectF(leftX + innerW - pctW, y - 1.0, pctW, 18.0);
    }

    const qreal titleX = leftX + 14.0 + 7.0;
    const qreal titleW = std::max<qreal>(40.0, innerW - 21.0 - rightReserve);
    const qreal titleH = std::max<qreal>(
        16.0, std::ceil(fmTitle.boundingRect(QRectF(0, 0, titleW, 120), Qt::TextWordWrap, m_titleText).height()));
    m_titleRect = QRectF(titleX, y, titleW, titleH);
    y += titleH;

    if (!m_bodyText.isEmpty()) {
        y += 6.0;
        const qreal bodyH = std::ceil(
            fmBody.boundingRect(QRectF(0, 0, innerW, 200), Qt::TextWordWrap, m_bodyText).height());
        m_bodyRect = QRectF(leftX, y, innerW, bodyH);
        y += bodyH;
    }

    if (m_state == State::Downloading) {
        y += 7.0;
        const qreal cancelSize = 18.0;
        const qreal barW = std::max<qreal>(40.0, innerW - cancelSize - 6.0);
        m_pbarRect = QRectF(leftX, y + (cancelSize - 4.0) * 0.5, barW, 4.0);
        m_hits.append({QRectF(leftX + innerW - cancelSize, y, cancelSize, cancelSize), {}, HitAction::Cancel, false, true});
        y += cancelSize;
    } else {
        auto addButtonPair = [&](const QString& lbl1, HitAction act1, const QString& lbl2, HitAction act2) {
            y += 8.0;
            const qreal btnH = 26.0;
            if (lbl2.isEmpty()) {
                const qreal w1 = std::min(innerW, std::ceil(fmBtn.horizontalAdvance(lbl1)) + 20.0);
                m_hits.append({QRectF(leftX, y, w1, btnH), lbl1, act1, true, false});
                y += btnH;
                return;
            }
            const qreal raw1 = std::ceil(fmBtn.horizontalAdvance(lbl1));
            const qreal raw2 = std::ceil(fmBtn.horizontalAdvance(lbl2));
            constexpr qreal gap = 6.0;
            if (raw1 + 16.0 + raw2 + 16.0 + gap <= innerW) {
                const qreal pad = (raw1 + 20.0 + raw2 + 20.0 + gap <= innerW) ? 20.0 : 14.0;
                const qreal w1 = raw1 + pad;
                const qreal w2 = raw2 + pad;
                m_hits.append({QRectF(leftX, y, w1, btnH), lbl1, act1, true, false});
                m_hits.append({QRectF(leftX + w1 + gap, y, w2, btnH), lbl2, act2, false, false});
                y += btnH;
            } else {
                m_hits.append({QRectF(leftX, y, innerW, btnH), lbl1, act1, true, false});
                y += btnH + 5.0;
                m_hits.append({QRectF(leftX, y, innerW, btnH), lbl2, act2, false, false});
                y += btnH;
            }
        };

        if (m_state == State::Ready) {
            addButtonPair(m_canInstall ? QStringLiteral("Actualizar") : QStringLiteral("Descargar"),
                          HitAction::Install, QStringLiteral("Novedades"), HitAction::Notes);
        } else if (m_state == State::ReadyToInstall) {
            addButtonPair(QStringLiteral("Reiniciar ahora"), HitAction::RestartNow,
                          QStringLiteral("Después"), HitAction::Later);
        } else if (m_state == State::Error) {
            addButtonPair(QStringLiteral("Reintentar"), HitAction::Install, QString(), HitAction::Install);
        }
    }

    const qreal cardH = (y + padY) - 1.0;
    m_cardRect = QRectF(cardX, 1.0, cardW, cardH);
    m_totalHeight = int(std::ceil(cardH + 9.0)); // 1 px arriba + 8 px separación con Papelera
    setFixedHeight(m_totalHeight);
    updateGeometry();
}

int UpdateBanner::hitIndexAt(const QPointF& pos) const {
    for (int i = 0; i < m_hits.size(); ++i) {
        if (m_hits.at(i).rect.contains(pos))
            return i;
    }
    return -1;
}

void UpdateBanner::paintEvent(QPaintEvent*) {
    if (m_state == State::Hidden || m_cardRect.isEmpty())
        return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const tokens::Palette tk = tokens::paletteFor(m_dark);

    // Sombra suave bajo la tarjeta (.upd).
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(15, 23, 42, m_dark ? 78 : 20));
    p.drawRoundedRect(m_cardRect.translated(0.0, 2.0), 12.0, 12.0);

    const QColor border = m_dark ? QColor(255, 255, 255, 22) : QColor(15, 23, 42, 18);
    p.setPen(QPen(border, 1.0));
    p.setBrush(tk.surface);
    p.drawRoundedRect(m_cardRect.adjusted(0.5, 0.5, -0.5, -0.5), 12.0, 12.0);

    QString iconName = QStringLiteral("download");
    QColor iconColor = tk.text;
    if (m_state == State::ReadyToInstall) {
        iconName = QStringLiteral("check");
    } else if (m_state == State::Error) {
        iconName = QStringLiteral("alert");
        iconColor = tk.danger;
    }
    paintUiIcon(&p, m_iconRect, iconName, iconColor);

    p.setFont(uiFont(11.5, QFont::DemiBold));
    p.setPen(tk.text);
    p.drawText(m_titleRect, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, m_titleText);

    if (!m_percentText.isEmpty()) {
        p.setFont(uiFont(11.0, QFont::Medium));
        p.setPen(tk.faint);
        p.drawText(m_percentRect, Qt::AlignRight | Qt::AlignVCenter, m_percentText);
    }

    if (!m_bodyText.isEmpty()) {
        p.setFont(uiFont(11.0, QFont::Normal));
        p.setPen(tk.muted);
        p.drawText(m_bodyRect, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, m_bodyText);
    }

    if (m_state == State::Downloading && !m_pbarRect.isEmpty()) {
        p.setPen(Qt::NoPen);
        p.setBrush(tk.field);
        p.drawRoundedRect(m_pbarRect, 2.0, 2.0);
        const qreal frac = std::clamp(m_progress / 100.0, 0.0, 1.0);
        if (frac > 0.0) {
            QPainterPath clip;
            clip.addRoundedRect(m_pbarRect, 2.0, 2.0);
            p.save();
            p.setClipPath(clip);
            p.fillRect(QRectF(m_pbarRect.left(), m_pbarRect.top(), m_pbarRect.width() * frac, m_pbarRect.height()),
                       tk.accent);
            p.restore();
        }
    }

    p.setFont(uiFont(11.5, QFont::DemiBold));
    for (int i = 0; i < m_hits.size(); ++i) {
        const HitButton& hb = m_hits.at(i);
        const bool hovered = (i == m_hoveredHit);
        if (hb.iconOnly) {
            if (hovered) {
                p.setPen(Qt::NoPen);
                p.setBrush(tk.hover);
                p.drawRoundedRect(hb.rect, 6.0, 6.0);
            }
            const qreal sz = 11.5;
            const QRectF ir(hb.rect.center().x() - sz * 0.5, hb.rect.center().y() - sz * 0.5, sz, sz);
            paintUiIcon(&p, ir, QStringLiteral("x"), hovered ? tk.text : tk.faint);
            continue;
        }
        p.setPen(Qt::NoPen);
        if (hb.primary)
            p.setBrush(hovered ? m_pal.accentHover : tk.accent);
        else
            p.setBrush(hovered ? tk.hover : tk.field);
        p.drawRoundedRect(hb.rect, 8.0, 8.0);
        p.setPen(hb.primary ? tk.accentFg : tk.icon);
        p.drawText(hb.rect, Qt::AlignCenter, hb.label);
    }
}

void UpdateBanner::mouseMoveEvent(QMouseEvent* event) {
    const int idx = hitIndexAt(event->position());
    if (idx != m_hoveredHit) {
        m_hoveredHit = idx;
        setCursor(idx >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        if (idx >= 0) {
            const HitAction act = m_hits.at(idx).action;
            if (act == HitAction::Dismiss)
                setToolTip(QStringLiteral("Omitir esta versión"));
            else if (act == HitAction::Cancel)
                setToolTip(QStringLiteral("Cancelar descarga"));
            else
                setToolTip({});
        } else {
            setToolTip({});
        }
        update();
    }
    QWidget::mouseMoveEvent(event);
}

void UpdateBanner::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        const int idx = hitIndexAt(event->position());
        if (idx >= 0) {
            switch (m_hits.at(idx).action) {
            case HitAction::Install:
                emit installRequested();
                break;
            case HitAction::Notes:
                emit releaseNotesRequested();
                break;
            case HitAction::Cancel:
                emit cancelRequested();
                break;
            case HitAction::Dismiss:
                hideBanner();
                emit dismissed();
                break;
            case HitAction::RestartNow:
                emit restartNowRequested();
                break;
            case HitAction::Later:
                hideBanner();
                emit laterRequested();
                break;
            }
            return;
        }
    }
    QWidget::mouseReleaseEvent(event);
}

void UpdateBanner::leaveEvent(QEvent* event) {
    if (m_hoveredHit >= 0) {
        m_hoveredHit = -1;
        setCursor(Qt::ArrowCursor);
        setToolTip({});
        update();
    }
    QWidget::leaveEvent(event);
}

void UpdateBanner::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (m_state != State::Hidden && event->size().width() != event->oldSize().width())
        relayout(event->size().width());
}

} // namespace finily
