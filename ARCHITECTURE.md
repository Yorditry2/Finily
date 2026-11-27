# Finily — Arquitectura Nativa Linux (C++20 + Qt 6)

**Finily** es una pizarra digital y cuaderno de notas nativo para Linux (Wayland/X11) y Windows, construido con **C++20 + Qt 6 (Core, Gui, Network, Widgets, Pdf, Svg)**, diseñado para escritura a 200 Hz reales vía `QTabletEvent`, geometría inteligente con enlaces/imán y dictado o exportación de clases.

## Principios de diseño
1. **Entrada nativa Wacom (`QTabletEvent`):** Presión hardware real (`0.0–1.0`), coordenadas sub-píxel en Wayland (`zwp_tablet_v2`), suavizado centrado sin retraso (`InkSmoother`) y detección de goma trasera / botones del Pad (`/dev/input/js*` con reconexión automática).
2. **Separación estricta Núcleo / Motor / UI:**
   - `src/core/`: Tipos compartidos (`Types.h`), utilidades sin dependencias de Widgets (`Utils.h`: ajustes centralizados, rutas de autoguardado, geometría de texto multilínea y ajuste de hoja), y protocolos tipados de marcas, resaltado, resortes físicos y tablas (`ShapeProtocols.h`).
   - `src/engine/` (`finily_engine`, sin `Qt6::Widgets`):
     - `NotebookDocument.{h,cpp}` — Modelo de documento del cuaderno, historial Undo/Redo, gestión de páginas y serialización/autoguardado por sesión con epochs de seguridad.
     - `InkEngine.{h,cpp}` y `LiveStroke.{h,cpp}` — Tinta líquida, puntas de lápiz/resaltador, estabilizador Gaussiano por velocidad, renderizado incremental `LiveInkRenderer` y borrador por trazo o parcial.
     - `GeometryEngine.{h,cpp}`, `GeometryInternal.h`, `GeometryEditing.cpp` y `RegionGeometry.{h,cpp}` — Primitivas 2D y sólidos 3D, guías inteligentes e imantado, auto-forma, marcas geométricas, detección topológica de regiones y borrador parcial de geometría.
     - `HandwritingEngine.{h,cpp}` y `MathCnnWeights.h` — Reconocimiento matemático offline y Modo Ecuación.
     - `LassoEngine.{h,cpp}` — Selección por lazo libre o rectangular, transformación afín y nivelación/ordenado de escritura.
     - `PdfEngine.{h,cpp}` — Importación de PDF y exportación vectorial con `Qt6::Pdf`.
     - `DocumentSerializer.{h,cpp}` — Persistencia `.finily` v1–v5, lectura rápida de metadatos y caché de portadas con bloqueo seguro entre hilos.
     - `StickerLibrary.{h,cpp}`, `StickerCatalogInternal.h`, `StickerCatalogGeometry.cpp`, `StickerCatalogScience.cpp` — Catálogo modular de stickers geométricos/científicos y biblioteca de usuario.
     - `ImageVectorizer.{h,cpp}` y `VisionExtractor.{h,cpp}` — Vectorización local instantánea y extracción asistida por IA con autenticación por cabecera HTTP.
     - `UpdateManager.{h,cpp}` — Verificación HTTPS restringida a orígenes confiables, comparación SemVer y descarga verificada por SHA-256.
   - `src/ui/` (Qt Widgets):
     - `MainWindow.{h,cpp}` y `HomeScreen.{h,cpp}` — Ventana principal con `QStackedWidget` (biblioteca y editor con pestañas), sincronización de renombrado/borrado y guardado seguro al salir.
     - `CanvasWidget.{h,cpp}`, `CanvasWidgetInternal.h`, `CanvasDocument.cpp`, `CanvasInput.cpp`, `CanvasRender.cpp`, `CanvasSelection.cpp` — Lienzo interactivo descompuesto por responsabilidad sobre `NotebookDocument`, caché estático + capa activa en vivo y menú radial.
     - `FloatingToolbar.{h,cpp}`, `FloatingToolbarInternal.h`, `ToolbarIcons.{h,cpp}`, `ToolbarPopovers.cpp`, `IconLibrary.{h,cpp}` y `src/ui/popovers/{FilePopover,PaperPopover,ShapesPopover,StickersPopover,StrokePopover,ThumbnailPanel}.{h,cpp}` — Overlay flotante con cápsulas `GlassCapsule` pintadas con `QPainter` (inmunes a temas del sistema Qt6) y popovers como hijos directos registrados en `setMask()` (requisito Wayland/Wacom).
3. **Compilación y pruebas modulares:** CMake + Ninja (`cmake -B build -G Ninja -DFINILY_BUILD_TESTS=ON && cmake --build build && ctest --test-dir build --output-on-failure`) con 6 suites QtTest aisladas bajo `tests/`.
