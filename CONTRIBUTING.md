# Contribuir a Finily

¡Gracias por ayudar a hacer crecer **Finily**! Este documento resume cómo compilar, probar y mantener la arquitectura limpia al añadir nuevas funciones.

## Requisitos

* Compilador **C++20** (`g++` o `clang++`)
* **CMake** `>= 3.20` (opcionalmente `ninja`)
* **Qt 6**: `Core`, `Gui`, `Widgets`, `Pdf`, `Svg` y `Test`

## Compilar, probar e instalar en desarrollo

```bash
cmake -B build
cmake --build build
ctest --test-dir build --output-on-failure
install -m 755 build/finily "$HOME/.local/bin/finily"
```

## Arquitectura en capas

* `src/core/` — Tipos de datos puros (`Stroke`, `ShapeItem`, `ImageItem`, `TextItem`, `PageData`, temas y enums). No depende de Qt Widgets.
* `src/engine/` — Motores independientes de la interfaz (`InkEngine`, `LiveStroke`, `GeometryEngine`, `RegionGeometry`, `LassoEngine`, `HandwritingEngine`, `PdfEngine`, `DocumentSerializer`), compilados como librería estática `finily_engine` y cubiertos por `tests/test_engines.cpp`.
* `src/ui/` — Ventana principal (`MainWindow`), biblioteca (`HomeScreen`), lienzo (`CanvasWidget`) y barra flotante (`FloatingToolbar`).

### Reglas críticas (Wayland + Tableta Wacom)

1. `CanvasWidget` y `FloatingToolbar` deben seguir siendo hermanos dentro de `m_container` en `MainWindow` para que los eventos de tableta se propaguen correctamente.
2. Los popovers deben ser hijos directos de `FloatingToolbar` (nunca `Qt::Popup`) y estar registrados en `updateOverlayMask()`.
3. Toda cápsula flotante usa `GlassCapsule` pintada con `QPainter` (no `QWidget` plano con fondo QSS) para no romperse con temas del sistema (KDE/Breeze).

## Estilo y pruebas

* Usa el archivo `.clang-format` de la raíz para código nuevo.
* Toda función nueva en `src/engine/` debe incluir casos en `tests/test_engines.cpp` y pasar `ctest --test-dir build --output-on-failure` sin warnings nuevos.
