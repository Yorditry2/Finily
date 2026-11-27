#pragma once

#include "core/Types.h"
#include "core/Utils.h"
#include "engine/GeometryEngine.h"
#include "engine/InkEngine.h"

#include <QByteArray>
#include <QColor>
#include <QDir>
#include <QPointF>
#include <QStandardPaths>
#include <QString>
#include <QTemporaryDir>
#include <QtGlobal>

#include <utility>

namespace finily::test {

// Aísla rutas de datos/configuración de Qt (`QStandardPaths` y `QSettings`) en
// un directorio temporal por prueba sin tocar `~/.local/share` ni `~/.config`.
class ScopedTestPaths {
public:
    ScopedTestPaths()
        : m_prevTestMode(QStandardPaths::isTestModeEnabled())
        , m_hadDataHome(qEnvironmentVariableIsSet("XDG_DATA_HOME"))
        , m_prevDataHome(qgetenv("XDG_DATA_HOME"))
        , m_hadConfigHome(qEnvironmentVariableIsSet("XDG_CONFIG_HOME"))
        , m_prevConfigHome(qgetenv("XDG_CONFIG_HOME")) {
        QStandardPaths::setTestModeEnabled(true);
        if (m_dir.isValid()) {
            const QString dataDir = m_dir.filePath(QStringLiteral("data"));
            const QString cfgDir = m_dir.filePath(QStringLiteral("config"));
            QDir().mkpath(dataDir);
            QDir().mkpath(cfgDir);
            qputenv("XDG_DATA_HOME", dataDir.toUtf8());
            qputenv("XDG_CONFIG_HOME", cfgDir.toUtf8());
        }
    }

    ~ScopedTestPaths() {
        QStandardPaths::setTestModeEnabled(m_prevTestMode);
        if (m_hadDataHome) {
            qputenv("XDG_DATA_HOME", m_prevDataHome);
        } else {
            qunsetenv("XDG_DATA_HOME");
        }
        if (m_hadConfigHome) {
            qputenv("XDG_CONFIG_HOME", m_prevConfigHome);
        } else {
            qunsetenv("XDG_CONFIG_HOME");
        }
    }

    ScopedTestPaths(const ScopedTestPaths&) = delete;
    ScopedTestPaths& operator=(const ScopedTestPaths&) = delete;

    bool isValid() const { return m_dir.isValid(); }
    QString path(const QString& relative) const { return m_dir.filePath(relative); }
    QString root() const { return m_dir.path(); }

private:
    QTemporaryDir m_dir;
    bool m_prevTestMode = false;
    bool m_hadDataHome = false;
    QByteArray m_prevDataHome;
    bool m_hadConfigHome = false;
    QByteArray m_prevConfigHome;
};

inline Stroke makeLineStroke(QPointF from, QPointF to, int steps = 8, double size = 2.5,
                             ToolType tool = ToolType::Pen, QColor color = Qt::white) {
    Stroke s;
    s.id = freshId();
    s.tool = tool;
    s.color = std::move(color);
    s.size = size;
    const int n = std::max(1, steps);
    for (int i = 0; i <= n; ++i) {
        const double t = double(i) / double(n);
        const QPointF p = from + (to - from) * t;
        s.points.push_back({p.x(), p.y(), 0.5, i * 10, 0.0, 0.0});
    }
    s.bounds = computeBounds(s.points, s.size);
    return s;
}

inline ShapeItem makeShape(ShapeKind kind, QVector<QPointF> vertices, const QString& label = QString(),
                           double size = 2.0, QColor color = Qt::white) {
    ShapeItem item;
    item.id = freshId();
    item.kind = kind;
    item.vertices = std::move(vertices);
    item.label = label;
    item.size = size;
    item.color = std::move(color);
    item.bounds = computeShapeBounds(item);
    return item;
}

inline TextItem makeText(QPointF pos, const QString& text, double fontSize = 22.0,
                         QColor color = Qt::white) {
    TextItem item;
    item.id = freshId();
    item.pos = pos;
    item.text = text;
    item.fontSize = fontSize;
    item.color = std::move(color);
    item.bounds = textBounds(item);
    return item;
}

} // namespace finily::test
