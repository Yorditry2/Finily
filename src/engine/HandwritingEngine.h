#pragma once

#include "core/Types.h"

#include <QColor>
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
    // If true, canonicalStrokes are ALREADY in world coordinates (e.g. exact-length √ or · or °).
    bool canonicalInWorld = false;
    QVector<QVector<QPointF>> canonicalStrokes;
};

class HandwritingEngine {
public:
    static HandwritingEngine& instance();

    // Recognize a single character cluster (1..3 raw strokes in world coordinates).
    RecognitionResult recognizeCluster(const QVector<QVector<QPointF>>& rawStrokes,
                                       bool mathAndDigitsOnly = false,
                                       const QRectF& neighborLeftBox = QRectF(),
                                       bool hasRightNeighbor = false) const;

    // Auto-beautify the cluster of strokes at indices [startIdx .. endIdx-1] (if endIdx < 0, uses strokes.size()).
    // Uses spatial context from surrounding strokes & shapes (never touches strokes over shapes like angle arcs!).
    // Returns true if strokes were replaced; sets *outDeltaCount to (newCount - oldCount) if non-null.
    bool beautifyCluster(QVector<Stroke>& strokes,
                         int startIdx,
                         int endIdx = -1,
                         const QColor& operatorColor = QColor(),
                         QString* recognizedLabel = nullptr,
                         int* outDeltaCount = nullptr,
                         const QVector<ShapeItem>& shapes = {}) const;

    // Recognize all Lasso-selected strokes (grouped into lines & characters) into a clean string.
    QString recognizeStrokesToText(const QVector<Stroke>& strokes,
                                   const QSet<QString>& selectedIds) const;

    // Save user's own drawn strokes as a persistent personal template for `label`.
    bool saveCustomTemplate(const QString& label, const QVector<QVector<QPointF>>& worldStrokes);
    bool removeCustomTemplate(const QString& label);
    QStringList customTemplateLabels() const;

    static bool isOperatorOrGrouping(const QString& label);

private:
    struct GlyphTemplate {
        QString label;
        bool isMathOrDigit = true;
        bool isCustom = false;
        int strokeCount = 1;
        double aspectRatio = 1.0;
        // Ordered resampled strokes normalized around centroid (for accurate directed matching)
        QVector<QVector<QPointF>> directedStrokes;
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

    // Structural geometric recognizer for math operators, brackets, braces, √, θ, °, ·
    RecognitionResult matchGeometric(const QVector<QVector<QPointF>>& rawStrokes,
                                     const QRectF& neighborLeftBox,
                                     bool hasRightNeighbor) const;

    static QVector<QVector<QPointF>> normalizeDirected(const QVector<QVector<QPointF>>& strokes,
                                                       double* outAspect = nullptr);
    static QVector<QVector<QPointF>> normalizeStrokes01(const QVector<QVector<QPointF>>& strokes);
    static double directedStrokeDistance(const QVector<QVector<QPointF>>& a,
                                         const QVector<QVector<QPointF>>& b);

    QVector<GlyphTemplate> m_templates;
};

} // namespace finily
