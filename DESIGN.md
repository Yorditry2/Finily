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
| **Editor (`FloatingToolbar`, popovers, menú radial)** | Sin iniciar | **PENDIENTE** | Se diseña después de cerrar Inicio completo |

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

### 5.3 Flujo «Nueva pizarra infinita» — Aprobado con ajuste final aplicado
- **Contenedor:** Tarjeta flotante derecha (`344 px`, bordes `18 px`, Híbrido B + C), **sin oscurecer** el resto de la biblioteca con scrim pesado.
- **Cabecera Preview Live (`174 px`, `<canvas>` HiDPI):**
  - Muestra en vivo el fondo, la plantilla y la trama real de la pizarra **sin rayones falsos**.
  - Arriba: píldora `∞ Pizarra` + botón `×`. Abajo: título editable en línea (`Pizarra sin título` + icono lápiz fino) con contraste automático según luminancia del fondo.
  - La cuadrícula/puntos/rayado escala centrado desde `(w/2, h/2)` sin saltos laterales e incluye una cota arquitectónica sutil (`X mm`) sobre una celda real.
- **1. Fondo:**
  - 6 circulitos de acceso rápido (`#161922`, `#122B24`, `#1E293B`, `#FFFFFF`, `#FBF7EA`, `#F6E2EA`) + 7.º botón de anillo cromático (`+`).
  - Al pulsar `+`, se abre a la izquierda de la tarjeta un **popover hermano flotante (`256 px`)** para nunca estirar ni apretar la tarjeta: 3 filas cromáticas visuales (pizarras oscuras, papeles neutros y pasteles suaves, sin pestañas ni etiquetas de texto), cuadro SV amplio, barra de tono y campo `#HEX` editable.
- **2. Plantilla:**
  - 4 muestras altas (`58 px`) puramente visuales (`Liso`, `Cuadrícula`, `Puntos`, `Rayado`) con SVGs vectoriales nítidos y cero texto.
  - **Regla fija:** las 4 muestras mantienen un fondo neutro constante (`#F6F6F4` / `#FFFFFF` activo con trazos oscuros `#18181B`), **sin cambiar al color de fondo elegido** para la pizarra.
- **3. Trama en `mm`:**
  - Control visual tipo **regla milimetrada táctil** (`3` a `12 mm`, paso `0.5 mm`) con marcas grabadas cada `0.5 mm`, paradas clicables (`3 · 5 · 8 · 10 · 12`) y stepper inline (`− 5 mm +`). Se atenúa cuando la plantilla activa es `Liso`.
- **4. Organización y acción final:**
  - Chips con SVG fino `Sin carpeta ⌄` y `# Etiqueta` + botón ancho `Crear pizarra`.

### 5.4 Próximos Flujos y Vistas Pendientes
1. **«Nuevo cuaderno» + Futura Sección de Plantillas / Marketplace local (SIGUIENTE PASO):**
   - El usuario señaló que los cuadernos necesitan **vistas previas grandes** tanto para portadas como para muchas plantillas enriquecidas, ya que Finily incluirá una sección/marketplace comunitario de plantillas descargables para uso local.
   - Por ello **NO** usará la tarjeta derecha angosta de `344 px`, sino una **galería amplia** (decidir con el usuario entre modal ancho centrado vs vista completa dedicada).
2. **«Nueva carpeta»:** reutilizar tarjeta flotante derecha compacta (nombre, paleta de color circular e icono central fino).
3. **`⚙ Ajustes`:** ahora solo **básicos** (ver `10`). La personalización avanzada (aspecto de carpetas, iconos propios, temas extra como `Crema`/`Negro puro`, paquetes) va en un apartado «Personalizar» marcado «pronto».
4. **Etapa 3 (Animaciones de Inicio):** especificar físicamente cada transición (colapso de sidebar, apertura de popovers, hover de carpetas/cuadernos, entrada de tarjeta derecha y apertura hacia el lienzo).
5. **Editor:** una vez cerrado Inicio completo, repetir `Estructura → Superficies → Animaciones` para `FloatingToolbar`, popovers y menú radial.

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
7. Editor: **no se toca todavía**.

### Siguiente paso inmediato al abrir una nueva sesión
1. Recoger las respuestas de arriba y ajustar `05`–`10`.
2. Etapa 3: animaciones de Inicio — aprobada (`11`).
3. Editor: barra flotante, popovers (trazo, papel, figuras, stickers, archivo), miniaturas, pills de zoom/páginas/pestañas, menú radial, menú de selección, editor de texto y selector de color propio.
