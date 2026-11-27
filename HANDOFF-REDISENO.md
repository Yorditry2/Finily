# Traspaso del rediseño de Inicio (rama `redesign/home`)

Lee primero `AGENTS.md` y `DESIGN.md`. Los mockups aprobados están en `mockups/redesign/` (00 y 05–11; ábrelos en el navegador). Regla del usuario: la app debe verse **literalmente** como los mockups.

## Hecho (rama `redesign/home`)
| Fase | Commit / Estado | Contenido |
|---|---|---|
| 0 | `25021f3` | Guarda el trabajo previo |
| 1 | `bb7bcfb` | `src/ui/DesignTokens.h` (paleta, radios, espacios, motion, 5 acentos, modo reducido), fuente Inter embebida |
| 2–3 | `dade112` | Barra lateral y cabecera; objetos de biblioteca (`src/ui/home/LibraryObjects`, `ObjectCard`) |
| 4 | `ecbdeae` | Menú `+ Nuevo`, tarjetas laterales Nueva pizarra/carpeta, modal Nuevo cuaderno |
| 5 | `79b22d5` | Menú contextual propio, renombrar en línea, avisos con Deshacer, papelera, diálogo propio |
| 6 | `910e3b7` | Búsqueda agrupada + filtros (`src/ui/home/LibraryViews`), estados vacíos, Ajustes (`src/ui/home/SettingsView`) |
| 7 | Implementado y verificado | Paridad completa con mockups `00` y `05–11`: Liquid Glass (`1/4` blur + realce chroma + brillo 1 px), `HomeMotion` (revelado circular Claro↔Oscuro, zoom carpeta, vuelo a papelera + FLIP, arrastre interno, dropzone), selector libre `BoardColorPopover`, formatos `A5`/`Carta`, favoritos + etiquetas persistentes (`.finily-library.json`), importación PDF no bloqueante, menú vacío y multiselección, `UpdateBanner` en pie del sidebar, 6 secciones de Ajustes funcionales, y verificación visual por capturas |

## Pendiente
- **Editor** (`FloatingToolbar`, popovers, menú radial, reemplazar `QColorDialog` por selector propio).
- **Logo final** (`branding/`).
- Al terminar: fusionar `redesign/home` en `main`.

## Cómo verificar sin abrir ventanas
Los ganchos de desarrollo `FINILY_SNAPSHOT*` (en `src/main.cpp`, `src/ui/MainWindow.cpp` y `src/ui/HomeScreen.cpp`) están **desactivados por defecto** (`FINILY_DEV_SNAPSHOTS=OFF`). Para usarlos, configura CMake con `-DFINILY_DEV_SNAPSHOTS=ON`:
```bash
cmake -B build -G Ninja -DFINILY_DEV_SNAPSHOTS=ON && cmake --build build
XDG_CONFIG_HOME=<cfg> FINILY_LIBRARY_DIR=<biblioteca> QT_QPA_PLATFORM=offscreen \
FINILY_SNAPSHOT_FOLDER=Apuntes FINILY_SNAPSHOT_SHEET=<hoja> FINILY_SNAPSHOT=salida.png build/finily
```
Hojas: `menu board board-color notebook folder move ctxdoc ctxfolder ctxempty multi rename toast trash favs folders tag collapsed update-card search:<texto> filter filtered settings settings-inicio settings-lapiz settings-storage confirm dropzone`. Config en `Finily/Finily.conf` usa `[home] themeMode=0|1|2`.

Compilar, probar e instalar: ver `AGENTS.md` §1 (siempre copiar a `~/.local/bin/finily`). Al añadir SVG en `resources/icons/` hay que volver a ejecutar `cmake -B build`.
