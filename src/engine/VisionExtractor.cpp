#include "engine/VisionExtractor.h"

#include "core/Utils.h"
#include "engine/GeometryEngine.h"

#include <QBuffer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>

#include <algorithm>
#include <cmath>

namespace finily {

namespace {

constexpr const char* kApiKeySetting = "ai/geminiApiKey";
constexpr const char* kModelSetting = "ai/geminiModel";
constexpr const char* kDefaultModel = "gemini-2.5-flash";

QPointF mapNormPoint(double nx, double ny, const QRectF& targetRect) {
    const double clampedX = std::clamp(nx, -50.0, 1050.0);
    const double clampedY = std::clamp(ny, -50.0, 1050.0);
    return QPointF(targetRect.left() + (clampedX / 1000.0) * targetRect.width(),
                   targetRect.top() + (clampedY / 1000.0) * targetRect.height());
}

bool readPointPair(const QJsonValue& val, const QRectF& targetRect, QPointF* out) {
    if (!val.isArray())
        return false;
    const QJsonArray arr = val.toArray();
    if (arr.size() < 2 || !arr[0].isDouble() || !arr[1].isDouble())
        return false;
    const double nx = arr[0].toDouble();
    const double ny = arr[1].toDouble();
    if (!std::isfinite(nx) || !std::isfinite(ny))
        return false;
    *out = mapNormPoint(nx, ny, targetRect);
    return true;
}

QString stripMarkdownFences(QString text) {
    text = text.trimmed();
    if (text.startsWith(QLatin1String("```"))) {
        text.remove(0, 3);
        if (text.endsWith(QLatin1String("```")))
            text.chop(3);
        const int firstNewline = text.indexOf(QLatin1Char('\n'));
        if (firstNewline >= 0) {
            text = text.mid(firstNewline + 1);
        } else {
            text = text.trimmed();
            if (text.startsWith(QLatin1String("json"), Qt::CaseInsensitive)) {
                const QChar nextCh = text.size() > 4 ? text.at(4) : QChar();
                if (nextCh.isNull() || nextCh.isSpace() || nextCh == QLatin1Char('{') ||
                    nextCh == QLatin1Char('[')) {
                    text.remove(0, 4);
                }
            }
        }
        text = text.trimmed();
    }
    return text;
}

QString sanitizeErrorText(QString text) {
    static const QRegularExpression urlQueryRe(
        QStringLiteral(R"((https?://[^\s?#"']+)\?[^\s"'<>)\]]*)"),
        QRegularExpression::CaseInsensitiveOption);
    text.replace(urlQueryRe, QStringLiteral("\\1"));
    static const QRegularExpression keyQueryRe(
        QStringLiteral(R"(\?key=[^\s&"'<>)\]]*(?:&[^\s"'<>)\]]*)?)"),
        QRegularExpression::CaseInsensitiveOption);
    text.replace(keyQueryRe, QString());
    return text;
}

void snapExtractedVertices(QVector<ShapeItem>& shapes, double snapRadius) {
    if (snapRadius <= 0.5)
        return;
    QVector<QPointF> anchors;
    auto snapOrRegister = [&](QPointF& pt) {
        double bestDist = snapRadius;
        int bestIdx = -1;
        for (int i = 0; i < anchors.size(); ++i) {
            const double d = std::hypot(pt.x() - anchors[i].x(), pt.y() - anchors[i].y());
            if (d <= bestDist) {
                bestDist = d;
                bestIdx = i;
            }
        }
        if (bestIdx >= 0) {
            pt = anchors[bestIdx];
        } else {
            anchors.push_back(pt);
        }
    };

    // Primero registramos vértices de polígonos/triángulos/rectángulos, luego líneas y ángulos
    for (int pass = 0; pass < 2; ++pass) {
        for (ShapeItem& shape : shapes) {
            const bool isPrimary = (shape.kind == ShapeKind::Triangle ||
                                    shape.kind == ShapeKind::Polyline ||
                                    shape.kind == ShapeKind::Rectangle);
            if ((pass == 0 && !isPrimary) || (pass == 1 && isPrimary))
                continue;
            if (shape.kind == ShapeKind::Circle || shape.kind == ShapeKind::Ellipse)
                continue;
            if (shape.kind == ShapeKind::RightAngle && shape.vertices.size() >= 3) {
                // Solo imantar el vértice del ángulo recto (índice 1)
                snapOrRegister(shape.vertices[1]);
                continue;
            }
            for (QPointF& v : shape.vertices)
                snapOrRegister(v);
        }
    }
}

QString buildExtractionPrompt() {
    return QStringLiteral(
        "Eres un extractor experto de geometría y texto matemático para una pizarra digital.\n"
        "Analiza la imagen (problema de geometría, física o matemáticas) y devuelve ÚNICAMENTE un objeto JSON válido con:\n"
        "1) \"shapes\": lista de figuras geométricas editables.\n"
        "2) \"texts\": lista de etiquetas (vértices A, B, C, ángulos 30°, x, α, medidas) y enunciados.\n\n"
        "Sistema de coordenadas: x en [0, 1000] de izquierda a derecha, y en [0, 1000] de arriba a abajo, relativo a toda la imagen.\n"
        "Esquema exacto:\n"
        "{\n"
        "  \"shapes\": [\n"
        "    {\"kind\": \"triangle\", \"points\": [[x1,y1],[x2,y2],[x3,y3]]},\n"
        "    {\"kind\": \"rectangle\", \"points\": [[x1,y1],[x2,y2]]},\n"
        "    {\"kind\": \"polyline\", \"points\": [[x1,y1],...], \"closed\": true},\n"
        "    {\"kind\": \"line\", \"points\": [[x1,y1],[x2,y2]], \"dashed\": false},\n"
        "    {\"kind\": \"circle\", \"center\": [cx,cy], \"radius\": r},\n"
        "    {\"kind\": \"arc\", \"center\": [cx,cy], \"p1\": [x1,y1], \"p2\": [x2,y2]},\n"
        "    {\"kind\": \"right_angle\", \"points\": [[arm1x,arm1y],[vx,vy],[arm2x,arm2y]]}\n"
        "  ],\n"
        "  \"texts\": [\n"
        "    {\"text\": \"A\", \"x\": 120, \"y\": 80, \"size\": \"small\"}\n"
        "  ]\n"
        "}\n\n"
        "Reglas importantes:\n"
        "- Usa exactamente las mismas coordenadas [x,y] para vértices compartidos entre figuras y segmentos internos.\n"
        "- Usa \"triangle\" para triángulos, \"polyline\" (closed:true) para cuadriláteros/trapecios/polígonos, \"circle\" para circunferencias (radius en escala horizontal 0..1000), \"line\" para segmentos/diagonales/alturas/radios/cuerdas, \"right_angle\" para símbolos de 90°.\n"
        "- Si una línea es punteada o discontinua, pon \"dashed\": true.\n"
        "- En \"texts\", usa símbolos Unicode limpios (°, α, β, θ, π, √, ², ³, ∠, ⊥, ∥, △) y \"size\": \"small\" para etiquetas de la figura, \"medium\" para enunciados, \"large\" para títulos.\n"
        "- No incluyas explicaciones ni texto fuera del JSON.");
}

} // namespace

bool isValidGeminiModelName(const QString& modelName) {
    if (modelName.isEmpty() || modelName.size() > 64)
        return false;
    if (modelName.contains(QLatin1String("..")))
        return false;
    auto isAlphaNum = [](QChar ch) {
        const ushort u = ch.unicode();
        return (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') || (u >= '0' && u <= '9');
    };
    if (!isAlphaNum(modelName.at(0)))
        return false;
    for (qsizetype i = 1; i < modelName.size(); ++i) {
        const QChar ch = modelName.at(i);
        if (!isAlphaNum(ch) && ch != QLatin1Char('.') && ch != QLatin1Char('_') &&
            ch != QLatin1Char('-')) {
            return false;
        }
    }
    return true;
}

std::optional<QNetworkRequest> buildVisionRequest(const QString& modelName, const QString& apiKeyValue) {
    const QString trimmedKey = apiKeyValue.trimmed();
    if (!isValidGeminiModelName(modelName) || trimmedKey.isEmpty())
        return std::nullopt;
    if (trimmedKey.contains(QLatin1Char('\r')) || trimmedKey.contains(QLatin1Char('\n')) ||
        trimmedKey.contains(QLatin1Char('\0'))) {
        return std::nullopt;
    }

    const QUrl url(QStringLiteral("https://generativelanguage.googleapis.com/v1beta/models/%1:generateContent")
                       .arg(modelName));
    if (!url.isValid() || url.hasQuery() || url.hasFragment())
        return std::nullopt;

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setRawHeader("x-goog-api-key", trimmedKey.toUtf8());
    req.setTransferTimeout(60000);
    return req;
}

std::optional<ExtractedContent> parseVisionExtraction(const QByteArray& jsonBytes,
                                                      const QRectF& targetRect,
                                                      const QColor& defaultInk,
                                                      double defaultStrokeSize) {
    if (jsonBytes.trimmed().isEmpty() || targetRect.width() <= 1.0 || targetRect.height() <= 1.0)
        return std::nullopt;

    QJsonParseError err{};
    QJsonDocument doc = QJsonDocument::fromJson(jsonBytes, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return std::nullopt;

    QJsonObject root = doc.object();

    // Si viene envuelto en la respuesta REST de Gemini (candidates[0].content.parts[0].text), desenvolverlo.
    if (root.contains(QLatin1String("candidates")) && root.value(QLatin1String("candidates")).isArray()) {
        const QJsonArray candidates = root.value(QLatin1String("candidates")).toArray();
        if (candidates.isEmpty() || !candidates[0].isObject())
            return std::nullopt;
        const QJsonObject contentObj = candidates[0].toObject().value(QLatin1String("content")).toObject();
        const QJsonArray parts = contentObj.value(QLatin1String("parts")).toArray();
        QString combinedText;
        for (const QJsonValue& partVal : parts) {
            if (!partVal.isObject())
                continue;
            const QJsonObject partObj = partVal.toObject();
            if (partObj.value(QLatin1String("thought")).toBool(false))
                continue;
            const QString t = partObj.value(QLatin1String("text")).toString();
            if (!t.isEmpty())
                combinedText += t;
        }
        QString cleanText = stripMarkdownFences(combinedText);
        if (cleanText.isEmpty())
            return std::nullopt;
        QJsonParseError innerErr{};
        QJsonDocument innerDoc = QJsonDocument::fromJson(cleanText.toUtf8(), &innerErr);
        if (innerErr.error != QJsonParseError::NoError || !innerDoc.isObject()) {
            const int firstBrace = cleanText.indexOf(QLatin1Char('{'));
            const int lastBrace = cleanText.lastIndexOf(QLatin1Char('}'));
            if (firstBrace >= 0 && lastBrace > firstBrace) {
                cleanText = cleanText.mid(firstBrace, lastBrace - firstBrace + 1);
                innerDoc = QJsonDocument::fromJson(cleanText.toUtf8(), &innerErr);
            }
        }
        if (innerErr.error != QJsonParseError::NoError || !innerDoc.isObject())
            return std::nullopt;
        root = innerDoc.object();
    }

    const bool hasShapesArray = root.value(QLatin1String("shapes")).isArray();
    const bool hasTextsArray = root.value(QLatin1String("texts")).isArray();
    if (!hasShapesArray && !hasTextsArray)
        return std::nullopt;

    ExtractedContent result;
    const double strokeW = std::clamp(defaultStrokeSize, 1.5, 6.0);

    const QJsonArray shapesArr = root.value(QLatin1String("shapes")).toArray();
    for (const QJsonValue& val : shapesArr) {
        if (!val.isObject())
            continue;
        const QJsonObject obj = val.toObject();
        const QString kindStr = obj.value(QLatin1String("kind")).toString().trimmed().toLower();
        const bool dashed = obj.value(QLatin1String("dashed")).toBool(false);

        ShapeItem shape;
        shape.id = freshId();
        shape.color = defaultInk.isValid() ? defaultInk : QColor(QStringLiteral("#f8fafc"));
        shape.size = strokeW;
        shape.opacity = 1.0;
        shape.linePattern = dashed ? LinePattern::Dashed : LinePattern::Solid;

        if (kindStr == QLatin1String("line") || kindStr == QLatin1String("segment")) {
            const QJsonArray pts = obj.value(QLatin1String("points")).toArray();
            QPointF p0, p1;
            if (pts.size() >= 2 && readPointPair(pts[0], targetRect, &p0) &&
                readPointPair(pts[1], targetRect, &p1)) {
                shape.kind = ShapeKind::Line;
                shape.vertices = {p0, p1};
            }
        } else if (kindStr == QLatin1String("triangle")) {
            const QJsonArray pts = obj.value(QLatin1String("points")).toArray();
            QPointF p0, p1, p2;
            if (pts.size() >= 3 && readPointPair(pts[0], targetRect, &p0) &&
                readPointPair(pts[1], targetRect, &p1) && readPointPair(pts[2], targetRect, &p2)) {
                shape.kind = ShapeKind::Triangle;
                shape.closed = true;
                shape.vertices = {p0, p1, p2};
            }
        } else if (kindStr == QLatin1String("rectangle") || kindStr == QLatin1String("rect") ||
                   kindStr == QLatin1String("square")) {
            const QJsonArray pts = obj.value(QLatin1String("points")).toArray();
            QPointF p0, p1;
            if (pts.size() >= 2 && readPointPair(pts[0], targetRect, &p0) &&
                readPointPair(pts[1], targetRect, &p1)) {
                shape.kind = ShapeKind::Rectangle;
                shape.closed = true;
                shape.vertices = {QPointF(std::min(p0.x(), p1.x()), std::min(p0.y(), p1.y())),
                                  QPointF(std::max(p0.x(), p1.x()), std::max(p0.y(), p1.y()))};
            }
        } else if (kindStr == QLatin1String("circle")) {
            QPointF center;
            const QJsonValue radiusVal = obj.value(QLatin1String("radius"));
            const double rNorm = radiusVal.isDouble() ? radiusVal.toDouble() : 0.0;
            if (readPointPair(obj.value(QLatin1String("center")), targetRect, &center) &&
                std::isfinite(rNorm) && rNorm > 1.0 && rNorm <= 2000.0) {
                const double rWorld = (rNorm / 1000.0) * targetRect.width();
                shape.kind = ShapeKind::Circle;
                shape.closed = true;
                shape.vertices = {center, QPointF(center.x() + rWorld, center.y())};
            }
        } else if (kindStr == QLatin1String("arc")) {
            QPointF center, p1, p2;
            if (readPointPair(obj.value(QLatin1String("center")), targetRect, &center) &&
                readPointPair(obj.value(QLatin1String("p1")), targetRect, &p1) &&
                readPointPair(obj.value(QLatin1String("p2")), targetRect, &p2)) {
                shape.kind = ShapeKind::Arc;
                shape.vertices = {center, p1, p2};
            }
        } else if (kindStr == QLatin1String("right_angle") || kindStr == QLatin1String("rightangle")) {
            const QJsonArray pts = obj.value(QLatin1String("points")).toArray();
            QPointF a1, v, a2;
            if (pts.size() >= 3 && readPointPair(pts[0], targetRect, &a1) &&
                readPointPair(pts[1], targetRect, &v) && readPointPair(pts[2], targetRect, &a2)) {
                shape.kind = ShapeKind::RightAngle;
                shape.vertices = {a1, v, a2};
            }
        } else if (kindStr == QLatin1String("polyline") || kindStr == QLatin1String("polygon")) {
            const QJsonArray pts = obj.value(QLatin1String("points")).toArray();
            QVector<QPointF> verts;
            for (const QJsonValue& pv : pts) {
                QPointF p;
                if (readPointPair(pv, targetRect, &p))
                    verts.push_back(p);
            }
            if (verts.size() >= 2) {
                shape.kind = ShapeKind::Polyline;
                shape.closed = obj.value(QLatin1String("closed")).toBool(kindStr == QLatin1String("polygon"));
                shape.vertices = verts;
            }
        }

        if (!shape.vertices.isEmpty())
            result.shapes.push_back(shape);
    }

    const double snapRadius = std::clamp(std::min(targetRect.width(), targetRect.height()) * 0.025, 6.0, 14.0);
    snapExtractedVertices(result.shapes, snapRadius);
    for (ShapeItem& shape : result.shapes)
        shape.bounds = computeShapeBounds(shape);

    const double scaleFactor = std::clamp(targetRect.width() / 420.0, 0.75, 1.45);
    const QJsonArray textsArr = root.value(QLatin1String("texts")).toArray();
    for (const QJsonValue& val : textsArr) {
        if (!val.isObject())
            continue;
        const QJsonObject obj = val.toObject();
        const QString text = obj.value(QLatin1String("text")).toString().trimmed();
        if (text.isEmpty())
            continue;
        const QJsonValue xVal = obj.value(QLatin1String("x"));
        const QJsonValue yVal = obj.value(QLatin1String("y"));
        if (!xVal.isDouble() || !yVal.isDouble())
            continue;
        const double nx = xVal.toDouble();
        const double ny = yVal.toDouble();
        if (!std::isfinite(nx) || !std::isfinite(ny))
            continue;
        const QString sizeStr = obj.value(QLatin1String("size")).toString().trimmed().toLower();
        double basePx = 20.0;
        if (sizeStr == QLatin1String("small"))
            basePx = 17.0;
        else if (sizeStr == QLatin1String("large"))
            basePx = 26.0;

        TextItem item;
        item.id = freshId();
        item.pos = mapNormPoint(nx, ny, targetRect);
        item.text = text;
        item.color = defaultInk.isValid() ? defaultInk : QColor(QStringLiteral("#f8fafc"));
        item.fontSize = std::clamp(basePx * scaleFactor, 13.0, 36.0);
        item.bounds = textBounds(item);
        result.texts.push_back(item);
    }

    return result;
}

VisionExtractor::VisionExtractor(QObject* parent)
    : QObject(parent),
      m_nam(new QNetworkAccessManager(this)) {}

QString VisionExtractor::apiKey() {
    return readAppSetting(QString::fromLatin1(kApiKeySetting)).toString().trimmed();
}

void VisionExtractor::setApiKey(const QString& key) {
    const QString settingKey = QString::fromLatin1(kApiKeySetting);
    const QString trimmed = key.trimmed();
    if (trimmed.isEmpty())
        removeAppSetting(settingKey);
    else
        writeAppSetting(settingKey, trimmed);
}

QString VisionExtractor::model() {
    const QString m = readAppSetting(QString::fromLatin1(kModelSetting)).toString().trimmed();
    if (!isValidGeminiModelName(m) || m == QLatin1String("gemini-3.8-flash"))
        return QString::fromLatin1(kDefaultModel);
    return m;
}

void VisionExtractor::setModel(const QString& model) {
    const QString trimmed = model.trimmed();
    const QString stored =
        (!isValidGeminiModelName(trimmed) || trimmed == QLatin1String("gemini-3.8-flash"))
            ? QString::fromLatin1(kDefaultModel)
            : trimmed;
    writeAppSetting(QString::fromLatin1(kModelSetting), stored);
}

QStringList VisionExtractor::supportedModels() {
    return {
        QStringLiteral("gemini-2.5-flash"),
        QStringLiteral("gemini-flash-latest"),
        QStringLiteral("gemini-2.5-pro"),
    };
}

void VisionExtractor::cancel() {
    if (QNetworkReply* reply = m_activeReply.data()) {
        m_activeReply.clear();
        disconnect(reply, nullptr, this, nullptr);
        reply->abort();
        reply->deleteLater();
    }
}

void VisionExtractor::extractFromImage(const QImage& image,
                                       const QRectF& targetRect,
                                       const QColor& defaultInk,
                                       double defaultStrokeSize) {
    if (image.isNull()) {
        emit extractionFailed(tr("La imagen seleccionada está vacía."));
        return;
    }
    const QString key = apiKey();
    if (key.isEmpty()) {
        emit extractionFailed(tr("Configura primero tu clave API de Gemini."));
        return;
    }

    cancel();

    // Redimensionar a <= 1024 px y comprimir en JPEG para respuesta rápida (~2-4 s)
    QImage scaled = image;
    constexpr int kMaxDim = 1024;
    if (scaled.width() > kMaxDim || scaled.height() > kMaxDim)
        scaled = scaled.scaled(kMaxDim, kMaxDim, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    QByteArray imgBytes;
    QString mimeType = QStringLiteral("image/jpeg");
    {
        QBuffer buf(&imgBytes);
        buf.open(QIODevice::WriteOnly);
        scaled.save(&buf, "JPEG", 86);
    }
    if (imgBytes.isEmpty()) {
        mimeType = QStringLiteral("image/png");
        QBuffer buf(&imgBytes);
        buf.open(QIODevice::WriteOnly);
        scaled.save(&buf, "PNG");
    }

    QJsonObject inlineData;
    inlineData.insert(QStringLiteral("mime_type"), mimeType);
    inlineData.insert(QStringLiteral("data"), QString::fromLatin1(imgBytes.toBase64()));

    QJsonArray parts;
    parts.append(QJsonObject{{QStringLiteral("text"), buildExtractionPrompt()}});
    parts.append(QJsonObject{{QStringLiteral("inline_data"), inlineData}});

    QJsonObject contentObj;
    contentObj.insert(QStringLiteral("parts"), parts);

    QJsonObject genConfig;
    genConfig.insert(QStringLiteral("responseMimeType"), QStringLiteral("application/json"));
    genConfig.insert(QStringLiteral("temperature"), 0.1);
    const QString activeModel = model();
    if (activeModel.contains(QLatin1String("flash"), Qt::CaseInsensitive)) {
        genConfig.insert(QStringLiteral("thinkingConfig"),
                         QJsonObject{{QStringLiteral("thinkingBudget"), 0}});
    }

    QJsonObject body;
    body.insert(QStringLiteral("contents"), QJsonArray{contentObj});
    body.insert(QStringLiteral("generationConfig"), genConfig);

    const QByteArray requestBody = QJsonDocument(body).toJson(QJsonDocument::Compact);
    sendRequest(activeModel, key, requestBody, targetRect, defaultInk, defaultStrokeSize, true);
}

void VisionExtractor::sendRequest(const QString& modelName,
                                  const QString& apiKeyValue,
                                  const QByteArray& requestBody,
                                  const QRectF& targetRect,
                                  const QColor& defaultInk,
                                  double defaultStrokeSize,
                                  bool allowFallback) {
    const auto reqOpt = buildVisionRequest(modelName, apiKeyValue);
    if (!reqOpt) {
        emit extractionFailed(tr("Configuración de modelo o clave API de Gemini inválida."));
        return;
    }

    QNetworkReply* reply = m_nam->post(*reqOpt, requestBody);
    m_activeReply = reply;

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, modelName, apiKeyValue, requestBody, allowFallback, targetRect, defaultInk,
             defaultStrokeSize] {
                if (m_activeReply != reply) {
                    reply->deleteLater();
                    return;
                }
                m_activeReply.clear();

                const QByteArray payload = reply->readAll();
                const QNetworkReply::NetworkError netErr = reply->error();
                const QString errStr = sanitizeErrorText(reply->errorString());
                const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                reply->deleteLater();

                if (netErr == QNetworkReply::OperationCanceledError)
                    return;

                if (netErr != QNetworkReply::NoError) {
                    QString detail = errStr;
                    const QJsonDocument errDoc = QJsonDocument::fromJson(payload);
                    if (errDoc.isObject()) {
                        const QString apiMsg = errDoc.object()
                                                   .value(QLatin1String("error"))
                                                   .toObject()
                                                   .value(QLatin1String("message"))
                                                   .toString();
                        if (!apiMsg.isEmpty())
                            detail = sanitizeErrorText(apiMsg);
                    }
                    // Si el modelo rechaza thinkingConfig (HTTP 400), reintentar sin thinkingConfig
                    if (allowFallback && httpStatus == 400 &&
                        detail.contains(QLatin1String("thinking"), Qt::CaseInsensitive)) {
                        QJsonDocument doc = QJsonDocument::fromJson(requestBody);
                        if (doc.isObject()) {
                            QJsonObject rootObj = doc.object();
                            QJsonObject cfg = rootObj.value(QLatin1String("generationConfig")).toObject();
                            cfg.remove(QLatin1String("thinkingConfig"));
                            rootObj.insert(QStringLiteral("generationConfig"), cfg);
                            sendRequest(modelName, apiKeyValue,
                                        QJsonDocument(rootObj).toJson(QJsonDocument::Compact), targetRect,
                                        defaultInk, defaultStrokeSize, false);
                            return;
                        }
                    }
                    // Si el modelo guardado ya no existe (404), migrar automáticamente al modelo sugerido por la API
                    if (allowFallback && httpStatus == 404) {
                        QString fallbackModel = QString::fromLatin1(kDefaultModel);
                        const QLatin1String prefix("use models/");
                        const qsizetype useIdx = detail.indexOf(prefix);
                        if (useIdx >= 0) {
                            const qsizetype start = useIdx + prefix.size();
                            qsizetype end = start;
                            while (end < detail.size() &&
                                   (detail[end].isLetterOrNumber() || detail[end] == QLatin1Char('-') ||
                                    detail[end] == QLatin1Char('.') || detail[end] == QLatin1Char('_'))) {
                                ++end;
                            }
                            if (end > start)
                                fallbackModel = detail.mid(start, end - start);
                        }
                        if (!isValidGeminiModelName(fallbackModel) ||
                            fallbackModel == QLatin1String("gemini-3.8-flash")) {
                            fallbackModel = QString::fromLatin1(kDefaultModel);
                        }
                        if (fallbackModel == modelName)
                            fallbackModel = QStringLiteral("gemini-flash-latest");
                        setModel(fallbackModel);
                        sendRequest(fallbackModel, apiKeyValue, requestBody, targetRect, defaultInk,
                                    defaultStrokeSize, false);
                        return;
                    }
                    emit extractionFailed(
                        tr("Error de API (%1): %2")
                            .arg(httpStatus > 0 ? QString::number(httpStatus) : QStringLiteral("red"),
                                 detail));
                    return;
                }

                const auto parsed = parseVisionExtraction(payload, targetRect, defaultInk, defaultStrokeSize);
                if (!parsed) {
                    emit extractionFailed(
                        tr("La IA devolvió una respuesta que no se pudo interpretar como figuras."));
                    return;
                }
                if (parsed->shapes.isEmpty() && parsed->texts.isEmpty()) {
                    emit extractionFailed(tr("No se detectaron figuras ni texto extraíble en la imagen."));
                    return;
                }
                emit extractionFinished(*parsed);
            });
}

} // namespace finily
