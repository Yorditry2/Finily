# DESIGN.md — Contexto y Especificación de Rediseño UI/UX de Finily

Este documento consolida todas las decisiones de diseño aprobadas, descartadas, tokens visuales y estado actual del rediseño de **Finily** para que cualquier agente IA o sesión futura retome exactamente donde quedamos, sin repetir discusiones ni reintroducir opciones rechazadas.

> **Nota para implementadores:** No tocar archivos C++ ni Qt6 hasta completar la aprobación visual de las pantallas y etapas definidas abajo. Cuando llegue el momento de implementar, revisar siempre primero las restricciones de arquitectura nativa en [`AGENTS.md`](AGENTS.md) (hermanos `CanvasWidget`/`FloatingToolbar`, cero `Qt::Popup`/`QComboBox` por Wayland + Wacom, cápsulas pintadas con `QPainter` en `GlassCapsule`, y copia final del binario a `~/.local/bin/finily`).

---

## 1. Propósito y Reglas de Trabajo

- **Objetivo:** Rediseñar Finily para que luzca moderno, minimalista, cohesivo y altamente personalizable en **Linux (Wayland/X11 + tableta Wacom)** y **Windows**, conservando el stack nativo **C++20 + Qt6 Widgets** (sin reescritura de lenguaje).
- **Orden obligatorio acordado con el usuario:**
  1. **Por pantalla:** cerrar completamente **Inicio (`HomeScreen`)** antes de pasar al **Editor** (`FloatingToolbar`, popovers, menú radial).
  2. **Por etapas dentro de cada pantalla:**
     - **Etapa 1:** Distribución y estructura (jerarquía, flujos, qué va y qué sobra).
     - **Etapa 2:** Colores, superficies y objetos (paletas, contraste, iconografía).
     - **Etapa 3:** Animaciones y microinteracciones (curvas, duraciones, transiciones).
     - **Implementación C++/Qt6:** embeber fuente `Inter`, crear `DesignTokens.h` / `Motion.h` y aplicar cambios solo tras aprobación visual.
- **Disciplina de iteración visual:**
  - Crear bocetos HTML ligeros y enfocados únicamente en el componente en revisión.
  - Trabajar por defecto en **Modo Claro** (comparar Oscuro solo cuando se solicite).
  - Evitar subagentes pesados para bocetos exploratorios rápidos.

---

## 2. Identidad Visual y Principios Rectores

1. **Estudio Minimalista + Personalizable:**
   - **Identidad por defecto neutra:** paleta **"Tinta"** (`#18181B` sobre `#FFFFFF`) en Modo Claro y **"Marfil sobre Grafito aterciopelado"** (`#F4F4F5` sobre `#1A1B22`) en Modo Oscuro.
   - **Nunca usar azul iOS (`#007AFF`) como acento por defecto.** El color de acento, tema y preferencias visuales se podrán personalizar después en `⚙ Ajustes`.
2. **Cero emojis en toda la UI:**
   - Prohibidos emojis tipo `📁`, `🏷️`, etc. Toda la interfaz usa exclusivamente **SVG vectoriales finos monocromáticos** (`stroke-width: 1.5–2`).
3. **Visual antes que texto:**
   - Las plantillas, medidas de trama y gamas de colores deben comunicarse viéndolas (muestras reales, reglas visuales, círculos), sin textos redundantes ni pestañas con palabras innecesarias.
4. **Cero relleno ni botones duplicados:**
   - Cada control debe tener una función única, evidente y no repetida.
5. **Objetos sin cajas duras:**
   - En la biblioteca, las carpetas, pizarras infinitas, cuadernos y PDFs prescinden de recuadros grises `1px solid`; se distinguen por su silueta física, proporción, degradado sutil y sombra difusa.
6. **Base neutra porque todo será reemplazable (pedido expreso del usuario):**
   - Más adelante habrá **personalización avanzada**: aspecto de carpetas, iconos propios, temas completos, paquetes. Por eso el diseño base debe ser lo más **neutro** posible: sin patrones decorativos (se quitó el fondo de puntos tras la carpeta en `06`), dibujos de estados vacíos en gris de línea fina, acento Tinta.
   - Implementación: iconos, colores de carpeta y portadas se definen como **tablas de datos** (como `IC`, `FICON`, `FCOL`, `COVERS` en `mockups/redesign/_shell.js`), nunca incrustados en el código de pintado, para poder sustituirlos por paquetes.
7. **Ventanas propias, nunca del sistema:**
   - Cero `QMenu`, `QInputDialog`, `QMessageBox`, `QProgressDialog`, `QColorDialog` (salen con estilo KDE/Windows y rompen la regla Wayland/Wacom de `AGENTS.md`). Única excepción: `QFileDialog` nativo para elegir archivos/carpetas.
   - Confirmación solo para lo irreversible (`Eliminar para siempre`, `Vaciar papelera`). Lo recuperable usa aviso inferior con «Deshacer» (6 s, `Ctrl+Z`).

---

## 3. Decisiones Descartadas (NO volver a proponer)

Para evitar retrocesos en nuevas sesiones, queda **expresamente descartado**:

- `Recientes` como apartado en el sidebar (es redundante con `Documentos` ordenado por reciente).
- `Abrir .finily…` en el sidebar.
- Botón `+` duplicado junto a `Carpetas` en el sidebar (el único punto de creación es `+ Nuevo`).
- Fila de texto `Tema oscuro` en el sidebar (solo icono `☾/☀` al pie del sidebar).
- Rail estrecho de iconos al minimizar el sidebar (al colapsar debe ocultarse por completo a **`0 px`**).
- Doble barra superior estilo iPad (`topbar` + `subbar`) → sustituida por **una sola cabecera limpia**.
- Toggle de vista cuadrícula/lista `[⊞|☰]` en la cabecera (pausado/en espera por decisión del usuario; podría reconsiderarse más adelante si aporta valor).
- Mezclar carpetas sueltas dentro de `Documentos` (se aprobó **Propuesta 1: Apartados separados**).
- Nombres de texto sobre las 4 muestras de plantilla (`Liso`, `Cuadrícula`, etc.) o pestañas de texto (`Pizarra | Papel | Pasteles`) en el selector de colores.
- Trazos o rayones falsos (p. ej. garabato azul con punto verde) dentro de la vista previa de «Nueva pizarra».
- Selector de color libre desplegado en vertical **dentro** de la tarjeta lateral de «Nueva pizarra» (estira y aprieta los controles).
- **Teñir las 4 muestras de plantilla con el color de fondo seleccionado:** decisión expresa del usuario tras revisar la `v2` → **las muestras de plantilla deben mantener un color neutro fijo**; el color elegido ya se previsualiza arriba en el canvas de la pizarra.
- Usar un panel lateral estrecho (`344 px`) para **«Nuevo cuaderno»** (no escala para portadas grandes ni para un catálogo amplio / futuro marketplace comunitario de plantillas).

---

## 4. Tokens Visuales Aprobados (Base para `src/ui/DesignTokens.h`)

- **Tipografía:** `Inter` (pesos `400`, `500`, `600`, `700`; deberá ir embebida en Qt para render idéntico en Linux y Windows).

### Paleta Base

| Token lógico | Modo Claro ("Tinta") | Modo Oscuro ("Marfil / Grafito") | Uso principal |
|---|---|---|---|
| `bg.canvas` | `#FFFFFF` | `#1A1B22` | Área principal de la biblioteca |
| `bg.sidebar` | `#F6F6F4` | `#131419` | Panel lateral izquierdo |
| `border.subtle` | `rgba(15, 23, 42, 0.06)` | `rgba(255, 255, 255, 0.06)` | Divisores de sidebar y cabeceras |
| `border.popover` | `rgba(15, 23, 42, 0.08)` | `rgba(255, 255, 255, 0.08)` | Borde fino de tarjetas/popovers flotantes |
| `text.primary` | `#18181B` | `#F4F4F5` | Títulos, nombres, etiquetas activas |
| `text.secondary` | `#71717A` | `#9397A5` | Fechas, kickers, subtítulos |
| `icon.default` | `#3F3F46` | `#D4D4D8` | Iconos de navegación y acciones |
| `accent.solid` | `#18181B` | `#F4F4F5` | Botón CTA `+ Nuevo` / `Crear pizarra` |
| `accent.fg` | `#FFFFFF` | `#14151A` | Texto e icono sobre botón CTA |
| `accent.soft` | `rgba(24, 24, 27, 0.08)` | `rgba(244, 244, 245, 0.10)` | Fondo de ítem activo en sidebar |
| `tree.guide` | `rgba(24, 24, 27, 0.13)` | `rgba(255, 255, 255, 0.14)` | Línea guía vertical de subcarpetas |

### Radios, Sombras y Carpetas

- **Radios:** ventana/tarjeta flotante `18 px`, popover de color `16 px`, popover `+ Nuevo` `14 px`, botones CTA y muestras de plantilla `11 px`, ítems de navegación `8–9 px`, píldoras y chips `999px`.
- **Sombras:** tarjetas flotantes `0 20px 46px -12px rgba(15,23,42,0.18), 0 5px 14px -3px rgba(15,23,42,0.07)`; objetos de grilla en claro `0 10px 22px -8px rgba(15,23,42,0.16), 0 3px 8px -3px rgba(15,23,42,0.07)`.
- **Carpetas 2 tonos (SVG Bézier con icono fino centrado):**
  - **Rosa:** cuerpo `#F2A4B9 → #E78FA8` / solapa `#D57892` (claro) · `#DC89A1` (oscuro).
  - **Arena:** cuerpo `#EDD1B8 → #E1C0A2` / solapa `#CCA887` (claro) · `#D9B694` (oscuro).
  - **Salvia:** cuerpo `#A6CEBB → #92BFA9` / solapa `#77A38E` (claro) · `#82B9A1` (oscuro).

---

## 5. Estado y Especificación por Pantalla / Flujo

| Área / Flujo | Etapa actual | Estado | Notas clave |
|---|---|---|---|
| **Inicio — Estructura, Sidebar, Cabecera y Objetos** | Etapas 1 y 2 | **APROBADO** | Base lista para animaciones y C++ |
| **Inicio — Menú `+ Nuevo`** | Etapas 1 y 2 | **APROBADO** | Popover limpio bajo `+ Nuevo` |
| **Inicio — Tarjeta «Nueva pizarra infinita»** | Etapas 1 y 2 | **APROBADO CON AJUSTE** | Ajuste aplicado: muestras de plantilla con fondo neutro fijo |
| **Inicio — «Nuevo cuaderno» (+ futura sección Plantillas)** | Etapa 1 | **APROBADO: Opción A** (`05`) | Modal galería ancho 880×600 centrado (decisión del usuario 2026-11-26) |
| **Inicio — «Nueva carpeta»** | Etapas 1 y 2 | **GUSTÓ, falta decidir colores** (`06`) | Tarjeta 344 px, héroe neutro sin puntos, 8 colores, 14 iconos, «Dentro de» |
| **Inicio — Clic derecho y ventanas propias** | Etapa 1 | **APROBADO** (`07`) | Menús propios, renombrar en sitio, Mover a…, Deshacer, diálogo irreversible, importar, arrastrar |
| **Inicio — Estados vacíos** | Etapas 1 y 2 | **APROBADO** (`08`) | Línea gris neutra, una sola salida, sin duplicar + Nuevo |
| **Inicio — Buscar, filtrar, Favoritos, Etiquetas, Papelera, Actualización** | Etapa 1 | **APROBADO** (`09`) | Búsqueda en cabecera, cápsula de filtro, tarjeta de actualización en pie del sidebar |
| **Inicio — `⚙ Ajustes` básicos** | Etapa 1 | **APROBADO** (`10`) | Apariencia, Inicio, Lápiz y tableta, Almacenamiento, Actualizaciones; avanzado «próximamente» |
| **Inicio — Animaciones** | Etapa 3 | **APROBADO** | `11-animaciones-inicio.html`: 3 duraciones (120/200/320 ms), 3 curvas (salida/entrada/muelle), vidrio discreto |
| **Editor — barra superior, iconos y menú de pluma** | Etapas 1 y 2 | **APROBADO PARA IMPLEMENTAR** (2026-11-27) | Ver §7. Resto del editor (radial, otros popovers) pendiente |

### 5.1 Pantalla de Inicio (`HomeScreen`) — Aprobado
- **Sidebar (`~212–216 px`):**
  - Cabecera: `Finily` + botón `[◧]` (colapsar barra).
  - Navegación (Propuesta 1 — apartados separados):
    1. `Documentos`: **solo archivos** (pizarras, cuadernos, PDFs), ordenados por reciente por defecto. Sin carpetas mezcladas.
    2. `Favoritos` (`☆`).
    3. `Carpetas`: apartado propio clicable; debajo muestra el árbol anidado **contraído por defecto (`›`)** para mantener limpieza visual. Al expandir usa sangría real `+18 px` y línea guía vertical `1 px` por nivel (soporte de varios niveles de subcarpetas).
    4. `Etiquetas`: **progresiva / opcional** — si el usuario tiene 0 etiquetas o la oculta desde `⚙`, la sección desaparece por completo del sidebar.
  - Pie del sidebar: `Papelera` + divisor sutil + **únicamente 2 iconos**: `[ ☾/☀ ]` y `[ ⚙ ]`.
- **Sidebar colapsado:** desaparece por completo (`0 px`, sin rail residual); el icono `[◧]` se ubica a la izquierda del título/breadcrumb y la grilla pasa de 4 a 5 columnas.
- **Cabecera (1 sola fila limpia):**
  - Izquierda: título actual (`Documentos`, `Favoritos`, o breadcrumb clicable en carpetas anidadas: `Carpetas › Apuntes › Semestre 1`).
  - Derecha: `🔍` (buscar) · `≡` (filtrar tipo y orden) · **`+ Nuevo`** (píldora sólida `#18181B`, único disparador de creación).
- **Grilla de objetos:**
  - Alineación sobre una línea base óptica inferior común con `Nombre ⌄` y fecha centrados debajo.
  - Representaciones visuales: carpetas Bézier 2 tonos, pizarras panorámicas (~`116×84 px`) con trama de puntos y marca `∞`, cuadernos verticales 3:4 (~`80×104 px`) con lomo redondeado, brillo y canto de hojas + formato (`A4`, `16:9`), y PDFs con insignia roja `PDF` y trazos de resaltado.

### 5.2 Popover `+ Nuevo` — Aprobado
- Flota anclado bajo el botón `+ Nuevo` (`~240 px`, vidrio translúcido / borde sutil):
  1. `Nota rápida` (`Ctrl+N`) — destacada arriba.
  2. `Pizarra infinita`
  3. `Cuaderno`
  4. *(separador hairline)*
  5. `Carpeta`
  6. `Importar PDF`

### 5.3 Flujo «Nueva pizarra infinita» — Actualizado (modal centrado)
- **Contenedor:** Modal centrado de dos columnas (`720 × 440 px`, bordes `18 px`, misma gramática que Nuevo cuaderno).
- **Columna izquierda:** Vista previa limpia en vivo (`BoardPreview`) + nombre editable con línea inferior + chips `Sin carpeta ⌄` y `# Etiqueta` + botón ancho `Crear pizarra`.
- **Columna derecha (`×` arriba):**
  1. **Fondo:** 6 circulitos de acceso rápido + 7.º botón de anillo cromático (`+`) que abre el popover flotante `BoardColorPopover` a un lado (`248 px`, paleta visual, cuadro SV, tono y `#HEX`).
  2. **Plantilla:** 4 muestras puramente visuales (`Liso`, `Cuadrícula`, `Puntos`, `Rayado`) en tono neutro fijo.
  *(La trama en `mm` se movió al modal de **Cuaderno**, porque la pizarra infinita permite zoom libre).*

### 5.4 Flujos «Nuevo cuaderno» y «Nueva carpeta» — Actualizados
1. **«Nuevo cuaderno» (`880 × 600 px` centrado):**
   - **Izquierda:** vista previa estable (en vertical lado a lado; en horizontal escalonada para no encogerse), título, desplegable Liquid Glass con medidas reales (`A4`, `A5`, `Carta`, `Legal`, `1:1`, `16:9`) + selector ancho **`Vertical | Horizontal`**, chips de carpeta/etiqueta y `Crear cuaderno`.
   - **Derecha:** **Portada** (8 puntos de color en cabecera + portada base, subir imagen próximamente, más portadas próximamente), **Hoja** (6 tonos curados de papel + selector libre `+`, 4 plantillas y tienda próximamente) y **Trama** (4 muestras visuales `4 / 5 / 7 / 10 mm` a juego con `Hoja`).
2. **«Nueva carpeta» (`680 × 410 px` centrado):**
   - Mismo diseño modal centrado de dos columnas: izquierda vista previa Bézier de carpeta + nombre + selector `Dentro de` + `Crear carpeta`; derecha `Color` (8 tonos) e `Icono` (14 iconos).
3. **`⚙ Ajustes`:** ahora solo **básicos** (ver `10`). La personalización avanzada (aspecto de carpetas, iconos propios, temas extra, paquetes) va en un apartado «Personalizar» marcado «pronto».
4. **Editor:** una vez cerrado Inicio completo, repetir `Estructura → Superficies → Animaciones` para `FloatingToolbar`, popovers y menú radial.

---

### Movimiento (Etapa 3 — propuesta en `11-animaciones-inicio.html`)
- **Duraciones:** rápida 120 ms (hover, pulsar, cerrar), base 200 ms (menús, búsqueda, avisos, diálogos), lenta 320 ms (tarjetas laterales, entrar a carpeta, sidebar, papelera); cambio de tema 420 ms.
- **Curvas:** salida `cubic-bezier(.22,1,.36,1)` para lo que entra; entrada `cubic-bezier(.4,0,1,1)` para lo que sale (más corto); muelle `cubic-bezier(.34,1.45,.64,1)` solo en detalles pequeños (insignias, soltar, objeto recién creado). En Qt: `QEasingCurve::BezierSpline`.
- **Recetas:** hover = objeto sube 3 px; pulsar = 96 %; menú crece 96→100 % desde el botón con filas escalonadas 18 ms; tarjeta lateral entra 24 px desde la derecha; entrar a carpeta = zoom desde la carpeta (grilla sale a 108 %, contenido entra desde 92 %) + título desliza; papelera = el objeto vuela en curva al ícono, contador con muelle, resto se recoloca (FLIP); tema = revelado circular desde el botón luna/sol; sidebar = funde contenido y luego ancho → 0; búsqueda = campo se despliega desde la lupa y la grilla filtra con fundido + recolocación; diálogo = velo + blur 2 px + 96→100 %.
- **Costo:** solo se animan posición/escala/opacidad. Vidrio: blur calculado una vez al abrir sobre captura a 1/4; nunca sobre el lienzo ni durante trazo. «Reducir animaciones» (y sin GPU) = fundidos de 80 ms y vidrio sólido.
- **Referencias consideradas:** magicui, anime.js y Motion (web). En Qt6 no se usan directamente; se replican con `QPropertyAnimation`/`QVariantAnimation` + `QParallelAnimationGroup` usando estos tokens (`DesignTokens.h`: `Motion::Fast/Base/Slow`, `Motion::Out/In/Spring`).

## 6. Archivos Visuales de Referencia y Cómo Retomar

Los mockups HTML aprobados/canónicos están guardados en [`mockups/redesign/`](mockups/redesign/) *(nota: `mockups/` está ignorado en `.gitignore`)*:

| Archivo en `mockups/redesign/` | Qué documenta | Estado |
|---|---|---|
| [`01-inicio-claro-oscuro.html`](mockups/redesign/01-inicio-claro-oscuro.html) | Identidad Claro ("Tinta") + Oscuro ("Marfil/Grafito"), objetos sin bordes, cabecera despejada | Aprobado (base visual) |
| [`02-inicio-jerarquia-carpetas.html`](mockups/redesign/02-inicio-jerarquia-carpetas.html) | Propuesta 1: `Documentos` vs `Carpetas`, árbol multinivel con sangría `+18 px` y guías, `Etiquetas` opcional | Aprobado (jerarquía) |
| [`03-inicio-nuevo-y-sidebar-colapsado.html`](mockups/redesign/03-inicio-nuevo-y-sidebar-colapsado.html) | Cabecera de 1 sola fila, menú flotante `+ Nuevo`, estado del sidebar colapsado a `0 px` | Aprobado |
| [`04-nueva-pizarra-tarjeta.html`](mockups/redesign/04-nueva-pizarra-tarjeta.html) | Tarjeta derecha `344 px` + popover de color flotante a la izquierda + regla `mm` + plantillas en color neutro fijo | Aprobado con ajuste de plantillas fijas aplicado |
| [`00-inicio-base.html`](mockups/redesign/00-inicio-base.html) | **Versión canónica de 01–04** rehecha sobre `_shell`: Inicio claro/oscuro, menú + Nuevo, dentro de carpeta, sidebar colapsado, Nueva pizarra (+ selector de color, + oscuro) | Aprobado (01–04 quedan como histórico) |
| [`05-nuevo-cuaderno-galeria.html`](mockups/redesign/05-nuevo-cuaderno-galeria.html) | «Nuevo cuaderno»: A modal ancho (elegida) vs B vista dedicada | Aprobado A |
| [`06-nueva-carpeta-tarjeta.html`](mockups/redesign/06-nueva-carpeta-tarjeta.html) | «Nueva carpeta»: tarjeta 344 px, colores, iconos, «Dentro de» | Gustó; decidir colores nuevos |
| [`07-menu-contextual-y-ventanas.html`](mockups/redesign/07-menu-contextual-y-ventanas.html) | Clic derecho (documento, carpeta, vacío, múltiple), renombrar, mover, papelera, diálogo, importar, arrastrar | En revisión |
| [`08-estados-vacios.html`](mockups/redesign/08-estados-vacios.html) | Primera vez, carpeta vacía, sin resultados, Favoritos, Papelera, etiqueta | En revisión |
| [`09-buscar-filtrar-y-vistas.html`](mockups/redesign/09-buscar-filtrar-y-vistas.html) | Búsqueda, filtro, Favoritos, etiqueta, Papelera, aviso de actualización | En revisión |
| [`10-ajustes-basicos.html`](mockups/redesign/10-ajustes-basicos.html) | Ajustes básicos + hueco «Personalizar · pronto» | En revisión |
| [`11-animaciones-inicio.html`](mockups/redesign/11-animaciones-inicio.html) | Animaciones de Inicio (en vivo, con «Lento ×4» y «Reducir animaciones») + muestra del vidrio claro/oscuro | Aprobado |

El Modo Oscuro se obtiene añadiendo la clase `dark` al marco (`frameHTML({dark:true})`): solo redefine tokens, igual que debe hacerlo `DesignTokens.h`.

Los mockups `00` y `07`+ comparten `_shell.css` / `_shell.js` (tokens, sidebar, cabecera, objetos, menús, tarjetas, avisos); editar ahí para cambiar todos a la vez.

### Decisiones tomadas (2026-11-26, delegadas por el usuario)
1. «Nuevo cuaderno»: **opción A** (galería en ventana centrada 880×600).
2. Colores de carpeta: **los 8** (rosa, arena, salvia, cielo, lavanda, durazno, limón, grafito).
3. Etiquetas: **solo documentos**. Las carpetas ya se distinguen por color e icono.
4. Clic derecho en espacio vacío: **sin «Nueva carpeta aquí»**. + Nuevo dentro de una carpeta crea ahí mismo.
5. Filtros: el **orden** se recuerda entre sesiones (es una preferencia); tipo/fecha/etiqueta se **reinician** al cerrar la app.
6. Logo: **se decide al final**, cuando la app esté terminada. Mientras, cuadrado Tinta con lápiz.
7. Editor: ~~no se toca todavía~~ → actualizado 2026-11-27: ver §7.

### Siguiente paso inmediato al abrir una nueva sesión
1. Recoger las respuestas de arriba y ajustar `05`–`10`.
2. Etapa 3: animaciones de Inicio — aprobada (`11`).
3. Editor: barra flotante, popovers (trazo, papel, figuras, stickers, archivo), miniaturas, pills de zoom/páginas/pestañas, menú radial, menú de selección, editor de texto y selector de color propio.

---

## 7. Editor — Instrucciones de implementación (actualizado 2026-11-27)

> **Verdad Oficial Funcional del Editor:** Antes de modificar o diseñar cualquier herramienta del editor, leer [`EDITOR_SPEC.md`](EDITOR_SPEC.md). Contiene las decisiones funcionales aprobadas herramienta por herramienta (reemplaza las decisiones previas de botones/bolitas de grosor y colores duplicados en paneles de §7.3).

El usuario aprobó **mantener la estructura actual** del editor (fila de pestañas + fila de herramientas centrada, fondo verde pizarra) y **solo pulirla**. Se descartó: cápsula flotante inferior, fondo grafito por defecto, iconos dibujados a mano, Lucide y Solar.

Bocetos de referencia en `mockups/editor/`:
- `01-barra-pulida.html` — tamaños (botón «Antes/Después»). Ignorar sus iconos Lucide.
- `02-iconos-phosphor.html` — selector de iconos; la combinación elegida está abajo.
- `03-menu-pluma.html` — **OBSOLETO**, no usar. El panel de Pinceles aprobado es §7.3 (`04c-panel-pinceles-punta.html`, variante D1).
- Plan de implementación de todo el rediseño aprobado: [`IMPLEMENTAR_EDITOR.md`](IMPLEMENTAR_EDITOR.md).

### 7.1 Iconos de herramientas (Phosphor duotone, ya descargados)
SVG listos en `mockups/editor/icons-phosphor-duotone/` (`fill="currentColor"` + capa `opacity=".2"`, compatibles con `paintUiIcon`, que sustituye `currentColor`).
1. Crear pack `resources/icons/tools/` y copiar ahí los 11 SVG (el glob `resources/icons/*/*.svg` de `CMakeLists.txt` ya los incluye). Nombres = los usados en la tabla de `src/ui/editor/EditorChrome.cpp:33-43`:
   `sel`=cursor · `hand`=hand · `pen`=pen-nib · `hl`=highlighter · `er`=eraser · `fill`=paint-bucket · `lasso`=selection (cuadro punteado) · `shapes`=triangle · `text`=text-aa · `stickers`=sticker · `wand`=magic-wand.
2. En `EditorChrome.cpp` (~línea 898 y ~1262) pintar los iconos de herramienta con `paintUiIcon(&p, ir, h.iconName, fg, QStringLiteral("tools"))`.
3. Pluma y resaltador: dejar de usar `paintDynamicPenIcon`/`paintDynamicHighlighterIcon`; pintar el SVG + **rayita de color** debajo (10×3 px, radio 1.5, a 4 px del borde inferior del botón) con `m_penNibColor` / `m_hlBarColor`.
4. Herramientas sin icono elegido (`el`, `laser`, undo/redo, etc.): usar el icono Phosphor duotone equivalente (`https://api.iconify.design/ph/<nombre>-duotone.svg`, p. ej. `arrow-u-up-left`, `arrow-u-up-right`, `cursor-click`) para que todo sea coherente.

### 7.2 Proporciones de la barra (`EditorTopBar`, `EditorChrome.cpp:469/501/574-645`)
| | Antes | Después |
|---|---|---|
| Fila pestañas | 38 px | 42 px |
| Fila herramientas (`setFixedHeight(84)` total) | 46 px | 56 px → total **98** |
| Botón herramienta | 28–34 px | **40 px** |
| Icono (`icSz` 15/17) | 15–17 px | **22 px** |
| Radio botón (`drawRoundedRect(..., 9, 9)`) | 9 | **12** |
| Texto pestaña / zoom | ~11 px | **13 px**, zoom con cifras tabulares |
| Separador entre grupos | — | 1 px, 24 px alto, `rgba(255,255,255,.07)`, margen 9 px |
| Triangulito de submenú | — | 3.5 px, opacidad 35 % |
- Activo: relleno de acento actual (verde `#7fc4a0`) con icono oscuro; hover `rgba(255,255,255,.06)`.
- Grupos: [cursor, mano] | [pluma, resaltador, borrador, relleno] | [lazo] | [figuras, texto, stickers, varita].
- Cuadrícula del lienzo por defecto más suave (~3–4 % opacidad).
- Toast «Guardado ✓» (`EditorToast`): mover arriba al centro, ~14 px bajo la barra, píldora; nunca abajo sobre el dibujo.

### 7.3 Panel de Pluma (`src/ui/editor/EditorToolPanels.cpp`, ~líneas 90–340) — Estructura aprobada 2026-11-27
Referencia visual: `mockups/editor/04c-panel-pinceles-punta.html`, variante **D1**. Descartadas: 04 (A/B/C), 04b (herramienta entera), 04c D2/D3 y 04d (estuche de pie).
Ancho ~360 px, radio 18, padding 8. Una **lista vertical de 4 filas** (Pluma, Bolígrafo, Grafito, Tiza), cada fila de 58 px:
1. **Punta** en un cuadro de 46 px (radio 12, fondo `rgba(255,255,255,.04)`; en la activa, acento al 16 %): solo la punta recortada e inclinada unos −38°, como al escribir, y teñida del color de tinta. Nunca se muestra la herramienta entera.
2. **Nombre**, 13 px (`#a1a1aa`; en la activa, `#f4f4f5` semibold).
3. **Trazo de muestra** con la textura de la punta, el color actual y **el grosor que recuerda esa punta**.
- La fila activa lleva fondo de acento al 11 % y una barrita de acento de 3 px a la izquierda.
- **Sin «mm» ni números en ninguna parte.** El grosor se ve en el trazo de muestra y en el slider de cuña de la barra.
- **Grosor y color solo en la barra superior** (EDITOR_SPEC). Al elegir otra punta, el slider de la barra salta al grosor recordado de esa punta.
- Movimiento (provisional, Etapa 3): abrir en 200 ms out con filas escalonadas cada 18 ms; cerrar en 120 ms in; al elegir, el trazo de la fila se vuelve a dibujar en 320 ms. El panel no se cierra al elegir.

### 7.3b Panel de Resaltador — Estructura aprobada 2026-11-27
Referencia visual: `mockups/editor/05-panel-resaltador.html`, variante **R1**. Descartadas: R2 (3 intensidades) y R3 (opacidad en la barra).
Ancho ~300 px, radio 18, padding 10. Una sola punta (sin Biselado, Redondo ni Recto). De arriba abajo:
1. **Muestra**: texto manuscrito sobre papel (`La fotosíntesis ocurre en el cloroplasto.`) con una palabra resaltada en el color, grosor y opacidad actuales, mezclada en modo *multiply*.
2. **Opacidad**: etiqueta «Opacidad» y un slider cuyo riel es un degradado del color, de transparente a lleno, sobre un fondo de damero. Sin «%» ni números. Rango 15–90 %.
- La muestra se actualiza en vivo al mover el slider o al cambiar el grosor o el color en la barra.
- **Grosor y color solo en la barra.** El botón del Resaltador lleva un indicador de color.
- Movimiento (provisional): el panel se abre como el de Pinceles, y el resaltado de la muestra se dibuja de izquierda a derecha en 320 ms.

### 7.3c Panel de Borrador — Estructura aprobada 2026-11-27
Referencia visual: `mockups/editor/06-panel-borrador.html`, variante **B3**. Descartadas: B1 (tarjetas ilustradas + confirmación) y B2 (mantener presionado).
Ancho ~316 px. **El tamaño solo está en la barra**: slider de cuña cuyo círculo muestra el tamaño real del borrador. De arriba abajo:
1. **Modo**: segmentado de texto `Trazo completo | Solo lo que toco`. El indicador se desliza 200 ms con curva *spring*.
2. **Qué borra**: lista de interruptores `Tinta`, `Resaltador` y `Figuras`, cada uno con su icono. El estado apagado es gris; el prendido, acento.
3. **Borrar toda la página**: enlace discreto en gris que se pone rojo al pasar el cursor. Borra **de inmediato**, sin confirmar, y aparece un aviso abajo al centro: «Se borraron las anotaciones de la página» con el botón **Deshacer** y una barra de cuenta regresiva de 5 s. Ctrl+Z también lo deshace.
- El texto y las imágenes nunca se borran con el borrador.

### 7.3d Panel de Relleno — Estructura aprobada 2026-11-27
Referencia visual: `mockups/editor/07-panel-relleno.html`, variante **F3**. Descartadas: F1 (6 tramas con nombre) y F2 (muestra arriba).
Ancho ~300 px y panel mínimo, porque la propia hoja hace de muestra. De arriba abajo:
1. **Tramas**: una fila de 5 parches cuadrados de 40 px (`Sólido`, `Diagonal`, `Puntos`, `Cruzado`, `Cuadrícula`), cada uno pintado con el color y la intensidad actuales. **Se elimina «Suave»**, que equivale a Sólido con poca intensidad. Los parches no llevan nombre: aparece en un tooltip tras 400 ms. El activo tiene un borde de acento de 2 px y crece a 1.04 con curva *spring* en 200 ms.
2. **Intensidad**: el mismo slider del Resaltador (§7.3b), con el color sobre un fondo a cuadros. Sin números.
- **El color solo está en la barra.** El botón de Relleno lleva un indicador de color.
- Movimiento (provisional): el panel se abre en 200 ms y se cierra en 120 ms. Al rellenar, la región aparece en 120 ms con un fundido en forma de onda que parte del punto tocado.

### 7.3e Panel de Lazo — Estructura aprobada 2026-11-27
Referencia visual: `mockups/editor/08-panel-lazo-laser.html`, variante **L1**. Descartadas: L2 (una sola opción) y L3 (sin panel, filtrar en la cápsula).
- Solo hay selección libre. No hay selector `Libre | Rectángulo`.
- El panel tiene un título «Qué selecciona» y una lista de interruptores: `Escritura`, `Figuras` y `Texto e imágenes`, cada uno con su icono. Es el mismo componente del Borrador (§7.3c).
- El último interruptor encendido no se puede apagar: si se intenta, la fila tiembla (240 ms).
- La cápsula inferior de la selección indica cuántos elementos se seleccionaron.

### 7.3f Láser — sin panel (decidido 2026-11-27)
El Láser queda con un solo modo, **Estela** (EDITOR_SPEC §2.9), así que no tiene panel desplegable. El segundo clic en su botón no hace nada. Se descartan el selector `Punto | Estela` y las variantes K1 y K2.

### 7.3g Panel de Figuras — M1 «fila + burbuja» (aprobado 2026-11-27)
Referencia visual interactiva: `mockups/editor/10e-panel-figuras-submenus.html`, variante **M1**. Descartadas: G1/G2/G3 (`10-panel-figuras.html`, catálogo completo demasiado grande), H1/H2/H3 (`10b`, compactar fichas no basta), J1/J2/J3 (`10c`), K1/K2 (`10d`, el submenú estiraba el panel y las variantes en texto se veían cargadas), M2 (deslizar la fila) y M3 (franja debajo). Código actual: `EditorShapesPanel` (`src/ui/editor/EditorToolPanels.h:242`, lista `ShapeBtnSpec` en `EditorToolPanels.cpp` ~997–1020).
- **Panel = una sola fila de 6 botones de 46 px** (radio 12, separación 4, padding 6) **solo con el dibujo, sin texto:** `Línea · Círculo · Triángulo · Cuadrilátero · Polígono · 3D`. Debajo hay una línea con el nombre (12 px): `Familia · **Variante**`, que cambia al instante al pasar el puntero. El panel nunca cambia de tamaño.
- **Cada botón dibuja la última variante usada** de su familia (si se eligió el trapecio, el botón Cuadrilátero muestra un trapecio). Un triangulito de 5 px en la esquina inferior derecha indica que la familia tiene variantes.
- **Burbuja de variantes:** al tocar un botón, ese botón queda activo y sale una burbuja aparte, centrada bajo el botón, a 10 px debajo del panel, con un pico de 12 px que apunta al botón. Muestra las variantes solo como dibujos de 42 px; la elegida lleva fondo `#2e323d`. 3D usa una cuadrícula de 5 columnas (2 filas). La burbuja es hija de `FloatingToolbar`, es una `EditorGlassCard` y está registrada en `updateOverlayMask()` (§3 de AGENTS.md).
- **Cierre de la burbuja:** al elegir una variante, al tocar fuera o al volver a tocar el mismo botón. Al tocar otra familia, la burbuja se desliza hasta su nuevo pico sin cerrarse.
- **Variantes (idénticas a la cápsula C3, §7.5):** Línea ▸ Sin punta, Flecha, Doble flecha, Cota · Círculo ▸ Círculo, Elipse · Triángulo ▸ Libre, Isósceles, Rectángulo, Equilátero · Cuadrilátero ▸ Rectángulo, Cuadrado, Rombo, Paralelogramo, Trapecio, Trapezoide · Polígono ▸ 5, 6, 7, 8 lados · 3D ▸ Cubo, Prisma, Pirámide, Tetraedro, Octaedro, Cilindro, Cono, Esfera, Tronco de cono, Tronco de pirámide. Los dibujos de las variantes salen de `paintShapeVariantGlyph` (§7.5); los de 3D salen de `resources/icons/editor/shp-*.svg`.
- **Qué sale del panel:** Polilínea, Curva, Ejes, Raíz, Llave, Ángulo y Resorte pasan a **Elementos** (ELEMENTOS_SPEC.md: Geometría / Física). Se eliminan los botones sueltos de Flecha, Elipse y Ángulo recto, el doble clic para ver variantes y el enlace «Más figuras…».
- **Tiempos:**
  - El panel se abre en 200 ms (out) y se cierra en 120 ms (in).
  - La burbuja sale en 200 ms con spring, escalando de 0.9 a 1 desde el pico, con las variantes escalonadas 15 ms. Se cierra en 100 ms (in). Al cambiar de familia se desliza en 160 ms (out).
  - La selección de un botón cambia en 120 ms.
  - El panel no se cierra al elegir; se cierra al empezar a dibujar.
- **Estado persistido** (`appSettings()`): la última familia y la última variante de cada familia.

### 7.5 Cápsula contextual de selección — C3 «dos pisos» (aprobada 2026-11-27, lista para implementar)
Referencia visual interactiva: `mockups/editor/09-capsula-seleccion.html`, variante **C3**. Usa los botones de arriba para cambiar el tipo de selección. Se descartaron C1 (una fila larga con dibujos) y C2 (desplegables, cambiar forma cuesta 2 toques).
Código actual: `src/ui/editor/EditorContextCapsules.{h,cpp}` (`EditorContextCapsule::syncFromCanvas()` en ~:96) y su posición en `FloatingToolbar::positionContextCapsule()` (`src/ui/FloatingToolbar.cpp:1000`).

**Por qué (no revertir):** hoy son 13 controles del mismo peso en una fila, hay abreviaturas («Trap.» frente a «Trapz.»), «Ángulos» parece una variante y el color y el grosor no se parecen a los de la barra. C3 separa **lo común** (siempre igual y en el mismo sitio, para la memoria muscular) de **lo propio de la figura**, que va con dibujo y nombre completo.

#### 7.5.1 Estructura
La cápsula es un widget contenedor transparente con **dos `EditorGlassCard` apiladas** (rol `ContextCapsule`), centradas en horizontal entre sí y separadas **6 px** en vertical:
- **Piso inferior (siempre):** es la cápsula principal y tiene la misma posición que hoy (`positionContextCapsule`, anclada por su borde inferior).
- **Piso superior (solo figuras con variantes):** va encima del inferior. Si no hay variantes, no existe y el contenedor mide una sola fila. El borde inferior de la cápsula queda fijo, así que crece hacia arriba.

Medidas de ambos pisos: radio **16**, padding **5**, separación entre controles **2**, botones **34×34** con radio **10** e icono de **20 px**. El estado activo es el acento al 16 % más un borde interior de 1,5 px `#7fc4a0`. El hover es blanco al 6 %. El divisor mide 1×22, es `rgba(255,255,255,.08)` y tiene 5 px de margen a cada lado.

#### 7.5.2 Piso inferior (común)
Orden fijo de izquierda a derecha:

| # | Control | Detalle |
|---|---|---|
| 0 | *(solo selección con Lazo de varios elementos)* contador | Texto atenuado 12 px, «12 elementos» (§7.3e). Va antes del color, con 8 px a cada lado. |
| 1 | **Color** | Botón 34×34 con un círculo de 20 px del color actual y doble anillo (2 px del color de la cápsula + 1,5 px blanco al 35 %). **Sin la palabra «Color».** Al tocarlo emite `selectionColorRequested` (igual que hoy). El popover que se abre muestra solo los **5 colores de la barra + anillo arcoíris**, aparece **encima** de la cápsula y se cierra al elegir. |
| 2 | **Grosor** | **Cuña mini** de 78×26: un triángulo de 4→74 px en x que se ensancha de 2 a 18 px de alto, con la parte recorrida al 55 % y el resto al 13 % de blanco. El pulgar es un círculo relleno del color de la selección, con borde blanco de 2 px y diámetro 8→18 según el valor (**el tamaño del pulgar ES el grosor, sin números**). Rango 0.5–16, paso 0.5. Es el mismo componente que la cuña de la barra superior (EDITOR_SPEC §3.1 A.2): créalo como widget reutilizable `EditorWedgeSlider` en `EditorTheme.{h,cpp}` con `setRange/setValue/setThumbColor/setCompact(bool)`. |
| — | divisor | |
| 3 | **Estilo** | Segmentado oscuro (fondo `#12141a`, radio 11, padding 2) con 3 botones de 32×30 usando los iconos `line-solid`, `line-dashed` y `line-dotted` y los tooltips «Sólida», «Guiones» y «Puntos». El activo usa el fondo `#2e323d`, sin borde de acento. **En escritura (trazos)** se cambia por la ficha `wand` + «Embellecer y ordenar». En **texto, imagen, ejes, ángulo, polilínea y sólido 3D** se mantiene el contenido propio de hoy en esta posición, pero con las medidas nuevas. |
| — | divisor | |
| 4 | **Duplicar** | icono `copy`, tooltip «Duplicar» |
| 5 | **Eliminar** | icono `trash`, `setDanger(true)`, tooltip «Eliminar» |
| 6 | **⋯ Más** | El mismo menú de hoy (`addMoreButton`). |

Se eliminan: el chip «Color» con texto, el `EditorSlider` plano de grosor y el bloque `m_fineThicknessOpen` («Grosor exacto…» ya está eliminado en EDITOR_SPEC §3.1 C.4).

#### 7.5.3 Piso superior (variantes de la figura)
Se compone de **fichas** de 34 px de alto, con padding de 7 px a la izquierda y 10 px a la derecha. Cada ficha lleva un **glifo dibujado de 20 px y el nombre completo** en 12.5 px. Ninguna abreviatura. Se cambia de forma con un toque: se llama a `applySelectedShapeVariant(fam, val)`.

| Familia (`classifyShapeFamily`) | Fichas (variante del motor) | Después de un divisor |
|---|---|---|
| `Quad` | Rectángulo (0) · Cuadrado (1) · Rombo (2) · Paralelogramo (3) · Trapecio (4; el 5 también la marca activa) · Trapezoide (6) | ficha **Ángulos** |
| `Triangle` | Libre (0) · Isósceles (1) · Rectángulo (2) · Equilátero (3) | ficha **Ángulos** |
| `Polygon` | 5 lados (5) · 6 lados (6) · 7 lados (7) · 8 lados (8) | ficha **Ángulos** |
| `Circle` | Círculo (0; activa también con 1 Arco y 2 Sector) · Elipse (3) | **Porción** (si la variante no es 3): icono pizza de 20 px que se repinta con el ángulo + `EditorSlider` de 90 px, rango 15–360 y paso 15 → `setSelectedCircleSweepDegrees` |
| Figura abierta (línea, flecha, cota) | Sin punta (`ending None`) · Flecha (`Arrow`) · Doble flecha (`DoubleArrow`) · Cota (`pattern Dimension`) | — |
| Sólido 3D / otras | *(no hay piso superior)* | |

- **Glifos:** píntalos con `QPainter` a partir de los vértices; no hacen falta SVG nuevos. Así son idénticos a los del panel de Figuras (EDITOR_SPEC: «variantes idénticas entre panel y cápsula»). Crea `paintShapeVariantGlyph(QPainter&, QRectF, ShapeFamily, int variant, QColor)` en `EditorTheme.cpp`: escala los vértices a la caja con un margen de 2.5 px, usa trazo de 1.7 px con uniones redondas y rellena con el mismo color al 14 %. Vértices de referencia, tomados del mockup en coordenadas de lienzo y normalizados al ajustar:
  - Rectángulo `(330,150)(570,150)(570,290)(330,290)` · Cuadrado `(380,140)(520,140)(520,280)(380,280)` · Rombo `(450,125)(565,215)(450,305)(335,215)` · Paralelogramo `(380,150)(590,150)(530,290)(320,290)` · Trapecio `(395,150)(505,150)(585,290)(315,290)` · Trapezoide `(370,160)(545,130)(590,290)(330,275)`
  - Triángulo libre `(335,290)(575,290)(420,135)` · Isósceles `(340,295)(560,295)(450,125)` · Rectángulo `(360,295)(570,295)(360,140)` · Equilátero `(355,300)(545,300)(450,135.5)`
  - Polígonos regulares de n lados con un vértice arriba. Círculo y elipse llevan **punto central** de 1.3 px (rx/ry de la elipse = 10/6.5 sobre 24). Las puntas de línea reutilizan `end-arrow`, `end-double-arrow` y `end-dimension`; para «Sin punta» sirve `line-solid`.
- **Ángulos** es un interruptor, no una variante: lleva el icono `shp-angle` + «Ángulos» y se ve activo cuando la figura ya tiene sus marcas. Hace falta **un getter nuevo** `bool CanvasWidget::selectedShapeHasAngles() const` en `src/ui/CanvasSelection.cpp`, junto a `toggleSelectedShapeAngles()` (:1402), con la misma búsqueda `isAutoVertexAngle` + tolerancia `6/zoom` que usa el toggle. Agrégalo a la firma (`m_lastSignature`) para que la ficha se refresque.
- **Cota** fuerza la línea sólida: mientras la cota está activa, los 3 botones de Estilo del piso inferior se muestran deshabilitados (`setEnabledState(false)`, al 35 %) con el tooltip «La cota siempre es sólida».
- **En el lienzo:** el círculo y el sector siempre muestran su punto central (ya decidido).

#### 7.5.4 Movimiento (tokens de §7 / motion 120·200·320)
No hay infraestructura de animación en `src/ui/editor/`. Usa `QVariantAnimation` en el contenedor y pinta la opacidad en `EditorGlassCard::paintEvent` con una propiedad `revealProgress` (0–1). No uses `QGraphicsOpacityEffect`, porque rompe el desenfoque y es lento con Wayland.

| Momento | Animación |
|---|---|
| Aparecer al seleccionar | 200 ms con curva out `(.22,1,.36,1)`: sube 12 px, aparece con fundido y escala de 0.98 a 1. El piso superior arranca **40 ms después** que el inferior. |
| Desaparecer al deseleccionar | 120 ms con curva in `(.4,0,1,1)`: baja 8 px y se desvanece. Ambos pisos a la vez. |
| Cambiar de tipo de selección (p. ej. trazo → figura) | El ancho de cada piso se interpola en 200 ms (out) y el contenido hace un fundido cruzado de 120 ms. Si aparece o desaparece el piso superior, usa sus animaciones de aparecer y desaparecer. |
| Elegir variante | La ficha cambia a activa en 120 ms. **Morph de la figura en el lienzo:** los vértices se interpolan en 200 ms con spring `(.34,1.45,.64,1)`. Es opcional en la 1.ª entrega: si se deja para después, el cambio es instantáneo, que es lo que ocurre hoy. |
| Popover de color | Se abre en 160 ms desde abajo (+6 px, escala 0.97 a 1, origen abajo al centro) y se cierra en 100 ms. Se cierra al elegir. |
| Ángulos al encender | Los arcos se dibujan de 0 al ángulo en 200 ms, escalonados 30 ms por vértice. El ángulo recto se dibuja como un cuadradito. Es opcional en la 1.ª entrega. |
| Arrastrar grosor o porción | Sin animación: la figura sigue al dedo. |

#### 7.5.5 Reglas técnicas
- Los dos pisos son hijos de `FloatingToolbar` (o un único contenedor hijo de él) y **los dos deben registrarse en `updateOverlayMask()`** (AGENTS.md §3.1). El popover de color también es hijo directo de `FloatingToolbar`, nunca `Qt::Popup`.
- `positionContextCapsule()` centra **el más ancho de los dos pisos** dentro del hueco disponible y alinea el otro al mismo centro. La `y` se calcula con el borde inferior del piso inferior, igual que hoy.
- La firma `m_lastSignature` debe incluir: familia + variante + patrón + punta + el estado de ángulos + el barrido del círculo redondeado a 15°. Durante el arrastre de un slider **no** se reconstruyen los controles; solo se repinta el icono pizza.
- Para aceptarlo: con un trapecio seleccionado se ven 2 pisos y el de arriba muestra 6 fichas con nombre completo + «Ángulos». Con trazos de lazo se ve 1 piso con el contador. Al pasar de uno a otro, la cápsula no salta de sitio: el borde inferior queda fijo. Compila, pasan los tests y se instala según `AGENTS.md` §1.

### 7.6 Menú «⋯ Más» de la cápsula — P1 (aprobado 2026-11-27)
Referencia visual interactiva: `mockups/editor/11-menu-mas.html`, variante **P1**. Descartadas: P2 (lista agrupada de 8 filas) y P3 (tira de iconos sin nombre). Código actual: `addMoreButton()` en `src/ui/editor/EditorContextCapsules.cpp` (~:50–70, ids 1..10).
- **Se quitan del menú `Duplicar`** (ya está en la cápsula) **y `Pegar`** (no actúa sobre la selección: se queda en Ctrl+V y en el clic derecho sobre el lienzo vacío).
- **Estructura** (tarjeta `EditorGlassCard` hija de `FloatingToolbar`, radio 14, padding 6, ancla en la esquina del ⋯, alineada a su borde derecho y encima de la cápsula):
  1. **Accesos rápidos:** dos botones grandes lado a lado, `Cortar` y `Copiar` (icono 19 px arriba y texto 11 px abajo, fondo blanco 4 %, radio 10, mínimo 62 px de ancho).
  2. Separador.
  3. **Fila `Voltear`:** texto a la izquierda y, a la derecha, dos botones de icono de 34×30: `flip-h` (Horizontal) y `flip-v` (Vertical), con tooltip.
  4. **Fila `Orden`:** dos botones de icono: `front` (Traer al frente) y `back` (Enviar al fondo), con tooltip.
  5. Separador.
  6. **`Agrupar` / `Desagrupar`** con el atajo `Ctrl+G` alineado a la derecha en gris tenue (`#6b6e7a`, 11.5 px).
  7. **`Bloquear` / `Desbloquear`** (icono `lock` / `unlock`). Un objeto bloqueado muestra un candado de 22 px en su esquina superior derecha.
- **Comportamiento:**
  - Los botones de Voltear y de Orden **no cierran el menú**, para poder repetirlos.
  - Cortar, Copiar, Agrupar y Bloquear sí lo cierran.
  - El menú también se cierra al tocar fuera, con Esc o al volver a tocar ⋯, que queda en estado activo mientras el menú está abierto.
- **Tiempos:** se abre en 160 ms (out), subiendo 6 px y escalando de 0.97 a 1 desde la esquina del ⋯; se cierra en 100 ms (in). Las acciones se ejecutan al soltar.

### 7.7 Cápsula de Texto — T1 (aprobada 2026-11-27)
Referencia visual interactiva: `mockups/editor/12-capsula-texto.html`, variante **T1**. Descartadas: T2 (estilos con nombre Título/Subtítulo/Normal/Nota) y T3 (barrita flotante sobre el texto al escribir). Código actual: rama `isText` de `EditorContextCapsule::syncFromCanvas()` y `EditorInlineTextBar` (`src/ui/editor/EditorContextCapsules.{h,cpp}`).
- **Una sola cápsula** abajo, con la misma forma y posición **al escribir y al seleccionar**. `EditorInlineTextBar` (con el segmentado Inter/Manuscrita/Mono) desaparece y se fusiona en esta cápsula. Texto no tiene panel en la barra superior (EDITOR_SPEC §B.9).
- **Orden, escribiendo:** `color ●` (popover de 5 colores + arcoíris, igual que §7.5) · | · `Fuente ▾` (el nombre de la fuente activa escrito en esa misma fuente) · | · `−` `24` `+` (paso 2, rango 10–96, número en tabular semibold) · | · `B` `I` (botones-interruptor; el activo usa el estilo activo de §7.5).
- **Seleccionado:** son los mismos controles y se añaden al final `| Duplicar · Eliminar (rojo) · ⋯` (menú P1, §7.6).
- **No hay «Grosor»** en ningún caso: se quita `addColorAndWidth` de la rama de texto.
- **Popover de Fuente:** es una lista de filas de 40 px y cada fila muestra el nombre escrito en su fuente. La activa lleva fondo `#2e323d` y un ✓ en acento. Por ahora tiene 3 fuentes (Inter, Manuscrita, Mono); la lista está preparada para más (EDITOR_SPEC §2.7). Se cierra al elegir.
- **Cuadro de texto** (EDITOR_SPEC §2.7):
  - Es multilínea: Enter da un salto de línea, y Esc o un clic fuera termina.
  - Escribiendo: borde azul continuo y un tirador lateral derecho (8×18) para el ancho de ajuste.
  - Seleccionado: borde punteado y 4 esquinas para escalar.
- **Tiempos:**
  - La cápsula aparece en 200 ms (out), subiendo 12 px.
  - Al pasar de Escribiendo a Seleccionado no se recrea: anima el ancho (200 ms) y Duplicar, Eliminar y ⋯ entran con un fundido de 120 ms.
  - Al cambiar de tamaño, el texto se anima en 200 ms. Mantener `−`/`+` pulsado repite cada 80 ms después de 400 ms.
  - Los popovers se abren en 160 ms y se cierran en 100 ms.
- **Recordar** (`appSettings()`): el último color, fuente, tamaño, negrita y cursiva se usan para el próximo texto. El tamaño no se calcula a partir del grosor del lápiz.

### 7.8 Rueda radial — se queda con colores (decidido 2026-11-27)
- La rueda (`P+P` / botón del lápiz) **sigue mostrando colores, no herramientas.** Se descartan W1/W2/W3 de `mockups/editor/13-rueda-radial.html` (anillo de herramientas, medio anillo, recientes).
- Implementación actual de referencia: `src/ui/CanvasRender.cpp:552-647` (dibujo, hover, `commitRadialSelection`), `openRadialMenu` en `src/ui/CanvasWidget.cpp:1356`.

### 7.4 Reglas
- Respetar `AGENTS.md` §3 (paneles hijos de `FloatingToolbar`, `EditorGlassCard`, registrar en `updateOverlayMask()`).
- Ajustar `updateOverlayMask()` y posiciones de popovers al nuevo alto de barra (98 px).
- Compilar, pasar tests e instalar en `~/.local/bin/finily` (comando en `AGENTS.md` §1).
