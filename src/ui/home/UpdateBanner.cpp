#include "ui/home/UpdateBanner.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <algorithm>

namespace finily {

UpdateBanner::UpdateBanner(QWidget* parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("finilyUpdateBanner"));
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedHeight(44);

    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(14, 6, 12, 6);
    row->setSpacing(10);

    m_label = new QLabel(this);
    m_label->setObjectName(QStringLiteral("finilyUpdateText"));
    row->addWidget(m_label, 1);

    m_notesBtn = new QToolButton(this);
    m_notesBtn->setObjectName(QStringLiteral("finilyUpdateGhost"));
    m_notesBtn->setText(QStringLiteral("Ver cambios"));
    m_notesBtn->setFixedHeight(28);
    m_notesBtn->setCursor(Qt::PointingHandCursor);
    m_notesBtn->setFocusPolicy(Qt::NoFocus);
    connect(m_notesBtn, &QToolButton::clicked, this, &UpdateBanner::releaseNotesRequested);
    row->addWidget(m_notesBtn);

    m_primaryBtn = new QToolButton(this);
    m_primaryBtn->setObjectName(QStringLiteral("finilyUpdatePrimary"));
    m_primaryBtn->setFixedHeight(28);
    m_primaryBtn->setCursor(Qt::PointingHandCursor);
    m_primaryBtn->setFocusPolicy(Qt::NoFocus);
    connect(m_primaryBtn, &QToolButton::clicked, this, &UpdateBanner::installRequested);
    row->addWidget(m_primaryBtn);

    m_cancelBtn = new QToolButton(this);
    m_cancelBtn->setObjectName(QStringLiteral("finilyUpdateGhost"));
    m_cancelBtn->setText(QStringLiteral("Cancelar"));
    m_cancelBtn->setFixedHeight(28);
    m_cancelBtn->setCursor(Qt::PointingHandCursor);
    m_cancelBtn->setFocusPolicy(Qt::NoFocus);
    connect(m_cancelBtn, &QToolButton::clicked, this, &UpdateBanner::cancelRequested);
    m_cancelBtn->hide();
    row->addWidget(m_cancelBtn);

    m_dismissBtn = new QToolButton(this);
    m_dismissBtn->setObjectName(QStringLiteral("finilyUpdateClose"));
    m_dismissBtn->setText(QStringLiteral("×"));
    m_dismissBtn->setToolTip(QStringLiteral("Omitir esta versión"));
    m_dismissBtn->setFixedSize(26, 26);
    m_dismissBtn->setCursor(Qt::PointingHandCursor);
    m_dismissBtn->setFocusPolicy(Qt::NoFocus);
    connect(m_dismissBtn, &QToolButton::clicked, this, &UpdateBanner::dismissed);
    row->addWidget(m_dismissBtn);

    hide();
}

void UpdateBanner::showUpdate(const QString& version, bool canInstallInApp) {
    m_canInstall = canInstallInApp;
    if (m_label) m_label->setText(tr("Finily v%1 disponible").arg(version));
    resetState();
    show();
}

void UpdateBanner::setDownloadProgress(int percent) {
    const int clamped = std::clamp(percent, 0, 100);
    if (m_primaryBtn) {
        m_primaryBtn->setEnabled(false);
        m_primaryBtn->setText(clamped >= 100 ? tr("Verificando SHA-256…")
                                             : tr("Descargando %1%…").arg(clamped));
    }
    if (m_cancelBtn) m_cancelBtn->show();
}

void UpdateBanner::resetState() {
    if (m_primaryBtn) {
        m_primaryBtn->setEnabled(true);
        m_primaryBtn->setText(m_canInstall ? tr("Actualizar ahora") : tr("Ver descarga"));
    }
    if (m_cancelBtn) m_cancelBtn->hide();
}

} // namespace finily
