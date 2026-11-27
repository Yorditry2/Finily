# Traspaso del rediseño de Inicio (rama `redesign/home`)

Lee primero `AGENTS.md` y `DESIGN.md`. Los mockups aprobados están en `mockups/redesign/` (00 y 05–11; ábrelos en el navegador). Regla del usuario: la app debe verse **literalmente** como los mockups.

## Hecho (commits en `redesign/home`)
| Fase | Commit | Contenido |
|---|---|---|
| 0 | 25021f3 | Guarda el trabajo previo |
| 1 | bb7bcfb | `src/ui/DesignTokens.h` (paleta, radios, espacios, motion), fuente Inter embebida |
| 2–3 | dade112 | Barra lateral y cabecera; objetos de biblioteca (`src/ui/home/LibraryObjects`, `ObjectCard`) |
| 4 | ecbdeae | Menú `+ Nuevo`, tarjetas laterales Nueva pizarra/carpeta, modal Nuevo cuaderno |
| 5 | 79b22d5 | Menú contextual propio, renombrar en línea, avisos con Deshacer, papelera, diálogo propio |
| 6 | 910e3b7 | Búsqueda agrupada + filtros (`src/ui/home/LibraryViews`), estados vacíos, Ajustes (`src/ui/home/SettingsView`) |

## Pendiente
### Fase 7 — Animaciones y vidrio (mockup `11-animaciones-inicio.html`, sección «Movimiento» de DESIGN.md)
- Duraciones y curvas en `tokens::motion` (120/200/320, tema 420; out/in/spring). Usar `QPropertyAnimation`.
- Hover/press de objetos; apertura de menú `+ Nuevo` y tarjetas laterales; entrar a carpeta (zoom desde la carpeta); mover a papelera (vuelo al icono + contador + reflujo FLIP + aviso); cambio Claro↔Oscuro con revelado circular desde el botón luna; colapso de barra lateral; búsqueda que se despliega desde la lupa; diálogo con scrim + desenfoque 2 px.
- Coste: animar solo transform/opacidad; desenfoque calculado una vez al abrir sobre captura a 1/4; nunca sobre el lienzo. Modo reducido: fundidos de 80 ms y vidrio sólido (aún falta la opción en Ajustes).

### Detalles conocidos de fases anteriores
- Ajustes: el índice marca «Acerca de» al saltar a una sección cuando el scroll llega al final (`SettingsView.cpp`, handler de `valueChanged`). Recordar la sección pulsada.
- Botones de estados vacíos (`PillButton` en `LibraryViews.cpp`) no parecen usar Inter.
- Nueva pizarra: solo 4 colores de fondo; faltan 6 + «+» con popover de color propio. Nuevo cuaderno: faltan formatos A5/Carta.
- Menú contextual: se quitaron opciones sin función (Duplicar, Favoritos, Etiquetas, Exportar PDF, Abrir en pestaña nueva, menú del espacio vacío). Implementarlas y volver a añadirlas.
- Sin comprobar a mano en la app real: Deshacer/restaurar, popover «Dentro de», editar carpeta, modo oscuro de las tarjetas de fase 4.
- QSS `#finilyEmpty` en `src/ui/home/HomeTheme.cpp` ya no se usa.

### Después
- Editor (FloatingToolbar, popovers, menú radial, `QColorDialog` → propio). Logo al final (`branding/`).
- Al terminar: fusionar `redesign/home` en `main`.

## Cómo verificar sin abrir ventanas
Ganchos solo de desarrollo (en `src/main.cpp` y `src/ui/HomeScreen.cpp`):
```bash
XDG_CONFIG_HOME=<cfg> FINILY_LIBRARY_DIR=<biblioteca> QT_QPA_PLATFORM=offscreen \
FINILY_SNAPSHOT_FOLDER=Apuntes FINILY_SNAPSHOT_SHEET=<hoja> FINILY_SNAPSHOT=salida.png build/finily
```
Hojas: `menu board notebook folder ctxdoc ctxfolder rename toast trash confirm search:<texto> filter filtered settings settings-storage`. Con `XDG_CONFIG_HOME` vacío se ve en claro con biblioteca vacía; `home/darkMode=true` en `Finily/Finily.conf` para oscuro.

Compilar, probar e instalar: ver `AGENTS.md` §1 (siempre copiar a `~/.local/bin/finily`). Al añadir SVG en `resources/icons/` hay que volver a ejecutar `cmake -B build`.
