#pragma once
#include "core/Types.h"
#include <optional>

namespace finily {
// Punta provisional: extrapola medio paso por delante del último punto para
// que la tinta no quede "detrás" del lápiz. Solo se usa al dibujar en pantalla;
// nunca se guarda en el trazo.
std::optional<StrokePoint> predictTip(const Stroke& stroke, double zoom);
// Región (en coordenadas del mundo) que cambia al llegar un punto nuevo:
// la cola del trazo, porque el afinado del final depende de la longitud.
QRectF liveDirtyRect(const Stroke& stroke, double zoom);
// Copia del trazo con la punta predicha añadida (solo para pintar).
Stroke withPredictedTip(const Stroke& stroke, double zoom);
// Quita los "ganchitos" que deja el sensor al apoyar o levantar el lápiz rápido:
// un tramo muy corto (en píxeles de pantalla) que dobla bruscamente justo donde la
// presión cae. Solo actúa con presión de hardware; devuelve true si recortó algo.
bool trimPenHooks(Stroke& stroke, double zoom);
// Remate al soltar: si el lápiz se levantó en movimiento (un "latigazo"), la presión
// del último tramo se limita con una rampa para que la punta salga afilada, como con
// tinta real. Si se levantó despacio, no cambia nada (punta redonda). Solo pluma con
// presión de hardware; devuelve true si cambió algo.
bool taperPenFlick(Stroke& stroke, double zoom);
// Suavizado centrado (sin retraso) del temblor del lápiz: cada punto se promedia con
// sus vecinos de antes y de después, pesados por distancia a lo largo del trazo. Como
// necesita "el futuro", los últimos puntos se ajustan cuando llegan los siguientes; la
// punta queda siempre exactamente bajo el lápiz. Los extremos no se mueven.
class InkSmoother {
public:
    static constexpr int kRadius = 3;            // vecinos a cada lado
    void reset(double sigmaWorld);              // sigma <= 0: desactivado
    void push(Stroke& stroke);                  // llamar tras añadir un punto
private:
    QVector<QPointF> m_raw;
    double m_sigma = 0;
};
} // namespace finily
