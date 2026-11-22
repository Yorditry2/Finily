# Finily — Arquitectura Nativa Linux (C++20 + Qt 6)

**Finily** es una pizarra digital y anotador nativo para Linux (Wayland/X11), construido con **C++20 + Qt 6 Widgets + QPainter + Poppler-Qt6**, diseñado para escritura ultra-fluida con tableta Wacom (200 Hz reales vía `QTabletEvent`), geometría inteligente estilo StarNote y dictado de clases.

## Principios de diseño
1. **Entrada nativa Wacom (`QTabletEvent`):** Presión hardware real (`0.0–1.0`), coordenadas flotantes sub-píxel a 200 Hz en Wayland (`zwp_tablet_v2`), detección automática de goma trasera.
2. **Separación Motor vs. UI:**
   - `src/core/Types.h` — Tipos y contratos compartidos (`StrokePoint`, `Stroke`, `ThemeConfig`, `PenPreset`, `Viewport`).
   - `src/engine/InkEngine.{h,cpp}` — Estabilizador Gaussiano por velocidad (inspirado en Xournal++), interpolación Catmull-Rom, renderizado `QPainter` de ancho variable, tipos de línea (sólida/guiones/punteada/flechas), borrador por trazo y parcial.
   - `src/ui/CanvasWidget.{h,cpp}` — Widget principal del lienzo con `QTabletEvent`, caché `QImage` estático + capa activa en vivo (`<0.5 ms`), zoom/pan, Undo/Redo y autoguardado JSON.
   - `src/ui/FloatingToolbar.{h,cpp}` — Barra flotante minimalista estilo StarNote/Freenotes + pill de zoom.
   - `src/ui/MainWindow.{h,cpp}` — Ventana nativa principal, atajos de teclado y coordinación entre barra y lienzo.
3. **Compilación instantánea:** CMake + Ninja (`cmake -B build -G Ninja && cmake --build build`).

## Hitos
- **Hito 1:** App nativa Qt 6 Linux, entrada Wacom 200 Hz con presión real, tinta estilo Xournal++, estabilizador Gaussiano, tipos de línea, borrador dual, temas/plantillas, Undo/Redo y autoguardado.
- **Hito 2:** Figuras editables con nodos (estilo StarNote), polilínea continua, raíz $\sqrt{\quad}$ auto-ajustable y líneas guía inteligentes.
- **Hito 3:** Lazo + Enderezar letra (estilo Samsung Notes), pegar imágenes (`Ctrl+V`), importar/exportar PDF (`poppler-qt6` / `Qt6Pdf`).
