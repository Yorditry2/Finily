#pragma once

#include "core/Types.h"

#include <QPointF>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

namespace finily {

struct RecognitionResult {
    bool matched = false;
    QString label;
    double score = 0.0; // [0.0, 1.0], higher = more confident
    QVector<QVector<QPointF>> canonicalStrokes; // Normalized in [0, 1] x [0, 1]
};

class HandwritingEngine {
public:
    static HandwritingEngine& instance();

    // Recognize a single character cluster (1..3 raw strokes in world coordinates).
    // If mathAndDigitsOnly is true, restricts to digits, math symbols, and user custom templates.
    RecognitionResult recognizeCluster(const QVector<QVector<QPointF>>& rawStrokes,
                                       bool mathAndDigitsOnly = false) const;

    // Auto-beautify the most recent 1..3 strokes in `strokes` if they form a high-confidence
    // digit, math symbol, or custom user template. Returns true if strokes were replaced.
    bool beautifyRecentStrokes(QVector<Stroke>& strokes,
                               QString* recognizedLabel = nullptr) const;

    // Recognize all Lasso-selected strokes (grouped into lines & characters) into a clean string.
    QString recognizeStrokesToText(const QVector<Stroke>& strokes,
                                   const QSet<QString>& selectedIds) const;

    // Save user's own drawn strokes as a persistent personal template for `label`.
    // Used both for recognition AND as the beautified vector replacement!
    bool saveCustomTemplate(const QString& label, const QVector<QVector<QPointF>>& worldStrokes);
    bool removeCustomTemplate(const QString& label);
    QStringList customTemplateLabels() const;

private:
    struct CloudPoint {
        double x = 0.0;
        double y = 0.0;
        int strokeId = 0;
    };

    struct GlyphTemplate {
        QString label;
        bool isMathOrDigit = true;
        bool isCustom = false;
        int strokeCount = 1;
        double aspectRatio = 1.0; // width / height clamped to [0.15, 6.0]
        QVector<CloudPoint> cloud;
        QVector<QVector<QPointF>> canonicalStrokes;
    };

    HandwritingEngine();
    void buildBuiltinTemplates();
    void loadCustomTemplates();
    void saveCustomTemplatesToDisk() const;
    QString templatesFilePath() const;

    void addTemplate(const QString& label,
                     bool isMathOrDigit,
                     const QVector<QVector<QPointF>>& sampleStrokes,
                     const QVector<QVector<QPointF>>& canonicalStrokes,
                     bool isCustom = false);

    static QVector<CloudPoint> normalizeToCloud(const QVector<QVector<QPointF>>& strokes,
                                                int sampleCount,
                                                double* outAspect = nullptr);
    static QVector<QVector<QPointF>> normalizeStrokes01(const QVector<QVector<QPointF>>& strokes);
    static double cloudDistance(const QVector<CloudPoint>& a, const QVector<CloudPoint>& b);

    QVector<GlyphTemplate> m_templates;
};

} // namespace finily
