# Mapa de funciones: qué hacen las apps de referencia y qué le falta a Finily

Referencias: **Xournal++** (el que mencionas; en Linux), **OpenBoard** (pizarra de clase), **PDF Annotator**, **GoodNotes**, **Notability**, **Samsung Notes**, **Squid**, **OneNote**, **Concepts**, **Flexcil**, **Apple Freeform / Excalidraw** (lienzo libre).
Estado de Finily según README/ARCHITECTURE (verificar en código antes de dar algo por hecho):
✅ ya está · 🟡 parcial · ❌ falta.
Prioridad: **P1** (alto impacto, hacer pronto) · **P2** (muy valioso) · **P3** (pulido / opcional).

---

## 0. Decisiones del autor (mandan sobre el resto del documento)

**Meta:** Finily = el programa definitivo para **dictar clases** con **tableta gráfica** (no táctil).

**Descartado** (ignorar las filas que lo mencionen): rechazo de palma, gestos de dedos, botones del lápiz (ya existen), regla/escuadra/transportador/compás (mantener-para-enderezar + guías ya lo cubren), grabación de audio, lupa de escritura.

**Aprobado:** panel de miniaturas de páginas, lazo completo (color/grosor/agrupar/copiar entre páginas), plumas personalizables.

**Funciones propuestas para dictar clase** (prioridad sugerida):
1. Recorte rápido desde PDF/pantalla hacia la pizarra (enunciados de examen).
2. Menú radial en la punta del lápiz (plumas, colores, borrador, lazo).
3. Revelar solución por pasos (capa oculta de preparación).
4. Exportar para estudiantes: pizarra oscura → PDF fondo blanco, tinta oscura.
5. Anotar sobre cualquier ventana (overlay; en Hyprland/Wayland vía layer-shell).
6. Panel de miniaturas + guion de clase.
7. Duplicar página sin tinta; cortina para ocultar alternativas.
8. Vista del público vs. vista del profesor (2.º monitor).
9. Graficar al escribir `y = x² − 4`; datos automáticos en figuras; cuerpos 3D.
10. Borrar pizarra con historial; comprobar pasos; transmisión en vivo por QR/enlace.

---

## 0.1 Revisión de diseño (estado actual)

Lo que ya está bien: identidad coherente (cápsulas de cristal, tapas pastel de cuadernos, claro/oscuro), barra dividida en utilidades + dock, toasts, modo interfaz limpia (Tab).

Mejoras de diseño propuestas, por impacto:

| # | Problema observado | Mejora |
|---|---|---|
| D1 | Las portadas de la biblioteca muestran una hoja en blanco genérica, no el contenido | Miniatura real de la **primera página** (o última editada) en la tapa; se regenera al guardar |
| D2 | Cuadrícula alineada a la izquierda: en ventana ancha queda un gran vacío a la derecha | Centrar la cuadrícula o repartir columnas con ancho fluido; máximo de ancho de contenido |
| D3 | Nombres por defecto "Cuaderno sin título (5)…" se acumulan; renombrar está escondido en el menú contextual | Nombrar al crear (campo en línea) y renombrar con doble clic sobre el título |
| D4 | Sin carpetas, ordenamiento ni vista lista | Carpetas como "estantes", orden por reciente/nombre/fecha, favoritos fijados arriba |
| D5 | "Continuar borrador" poco claro y compite con "Importar PDF" y "Abrir" | Una fila "Continuar donde lo dejaste" con la miniatura del último cuaderno; acciones secundarias en menú |
| D6 | Botón de tema solo con icono de sol, sin etiqueta | Tooltip + selector Claro/Oscuro/Sistema |
| D7 | Editor: cromo agrupado en popovers (Trazo, Papel, Archivo) que obligan a abrir y cerrar | Menú radial en la punta del lápiz para lo frecuente; popovers solo para lo raro |
| D8 | Colores y plumas fijos (5 favoritos) | Plumas personalizables (guardar color+grosor+tipo), colores recientes |
| D9 | Sin vista general de páginas | Panel de miniaturas lateral colapsable (arrastrar para reordenar) |
| D10 | Todo el texto está en español incrustado, sin capa de traducción | Pasar los textos por `tr()` (ya preparado en parte) para permitir ES/EN |
| D11 | Archivos de UI muy grandes (`FloatingToolbar.cpp` 3172 líneas, `HomeScreen.cpp` 1336) | Dividir por componente (dock, popovers, pill de zoom/páginas) para poder iterar el diseño sin romper otras partes |

---

## 1. Estructura de contenido (cómo se organizan las notas)

Es donde más difieren las apps y lo que más define la experiencia.

| Modelo | Quién lo usa | Idea |
|---|---|---|
| Carpetas → Cuadernos → Páginas | GoodNotes, Notability, Samsung Notes, Squid | Carpetas anidadas; cada cuaderno es un documento con páginas ordenables |
| Cuaderno → Secciones → Páginas | OneNote | Un nivel extra: secciones con color (pestañas) |
| Archivo suelto `.xopp` | Xournal++ | Sin biblioteca; el sistema de archivos es la organización |
| Documento de diapositivas | OpenBoard | Actividades/documentos con páginas tipo slide |
| Lienzo infinito único | Concepts, Freeform | Sin páginas; todo en un espacio |

| Función | Detalle de cómo funciona | Finily | Prio |
|---|---|---|---|
| Biblioteca de cuadernos con portada | Estantería/cuadrícula con tapas | ✅ | |
| **Carpetas anidadas** | Arrastrar cuadernos a carpetas, migas de pan, mover/renombrar | ❌ | P1 |
| Favoritos / recientes / papelera | Papelera con restauración 30 días | ❌ | P1 |
| Etiquetas y colores de portada | Filtrar por etiqueta | 🟡 (búsqueda) | P2 |
| Búsqueda en vivo por título | | ✅ | |
| **Búsqueda dentro del contenido** | Busca en texto tipeado, en escritura a mano (OCR) y en PDF | ❌ | P2 |
| **Panel de miniaturas de páginas** | Barra lateral: reordenar por arrastre, duplicar, borrar, mover entre cuadernos | ❌ | P1 |
| Insertar página (antes/después, plantilla distinta) | | 🟡 | P1 |
| Índice / marcadores / outline | Marcadores de página, enlaces internos entre páginas | ❌ | P2 |
| Enlaces entre cuadernos / páginas | `[[enlace]]` estilo wiki (Notability, OneNote) | ❌ | P3 |
| Plantillas de cuaderno | Cornell, semanal, planner, pentagrama, milimetrado, isométrico | 🟡 (4 fondos) | P2 |
| Importar/exportar paquete de cuaderno | Un solo archivo para compartir/respaldar (`.finily`) | 🟡 (JSON) | P1 |
| Sincronización / respaldo | iCloud/Drive/WebDAV/Nextcloud/Syncthing | ❌ | P3 (carpeta sincronizable bastaría) |

**Recomendación estructural:** adoptar *Carpetas → Cuadernos → Páginas*. Guardar cada cuaderno como carpeta/zip con `manifest.json` + una página por archivo + `assets/` (imágenes y PDF). Así el autoguardado es por página (rápido), es sincronizable con Syncthing/Nextcloud y no se corrompe todo si falla una escritura.

---

## 2. Herramientas de escritura y dibujo

| Función | Cómo funciona en las apps | Finily | Prio |
|---|---|---|---|
| Pluma, bolígrafo, resaltador | Ancho por presión; resaltador multiplica por debajo | ✅ | |
| Presión e inclinación | Tilt cambia la forma del trazo (lápiz de grafito, caligrafía) | ✅ presión / 🟡 tilt | P3 |
| Estabilizador / suavizado | Slider de suavizado | ✅ | |
| **Lápiz de grafito / pincel / caligrafía** | Texturas y puntas (Concepts, Samsung) | ❌ | P3 |
| **Rotuladores de la barra personalizables** | Guardar 3–10 "pens" con color+grosor+tipo, pulsación larga para editar | 🟡 (tintas rápidas) | P1 |
| Paleta de colores + selector + cuentagotas | Colores recientes, paletas guardadas | 🟡 | P2 |
| Borrador por trazo / parcial | Más: borrar solo resaltador, ignorar tipo, tamaño | ✅ | |
| Goma trasera del lápiz y botón del lápiz | Botón lateral = borrar o lazo (configurable) | 🟡 goma / ❌ botones | P1 |
| **Rechazo de palma / "solo lápiz"** | Los dedos solo mueven; el lápiz escribe | ❌ (importante en tablet) | P1 |
| Gestos táctiles | 2 dedos = zoom/pan; 2 dedos tap = deshacer; 3 dedos tap = rehacer | ❌ | P1 |
| Hold-to-shape (mantener para enderezar) | | ✅ | |
| Figuras, flechas, polilínea, ejes | Más: polígonos regulares, tablas | ✅ | |
| **Regla, escuadra, transportador, compás** | Superposición movible/rotable con la que se traza pegado al borde (OpenBoard, Xournal++ las tiene en clase) | ❌ | P1 (para clases) |
| Cuadrícula con *snap* / ajuste a ángulos | | 🟡 (guías) | P2 |
| Lazo: mover, escalar, rotar, duplicar | Más: cambiar color/grosor de lo seleccionado, agrupar, alinear, orden Z, bloquear | 🟡 | P1 |
| Selección rectangular además del lazo | | ❌ | P2 |
| Copiar/pegar entre páginas y cuadernos | | 🟡 | P1 |
| Texto tipeado | Fuente, tamaño, negrita, listas, viñetas | 🟡 | P2 |
| **Escritura → texto (OCR)** | Samsung/GoodNotes/Notability convierten manuscrito a texto | 🟡 (tiene "pasar a texto") | P2 |
| **Matemática manuscrita → LaTeX/gráfica** | Samsung Notes, GoodNotes, MyScript | ✅ ventaja diferencial de Finily | |
| Puntero láser | Estela que desaparece | ✅ | |
| Tablas, listas de tareas, cuadros de texto | OneNote/Notability | ❌ | P3 |
| Pegar imágenes / recortar / rotar / voltear | Recorte con asas, transparencia, bloquear | 🟡 | P2 |
| Lupa (zoom de escritura) | Ventana ampliada para escribir pequeño (GoodNotes "zoom window") | ❌ | P2 |
| Reconocer formas y tachados ("scratch-out") | Rayar sobre algo lo borra (GoodNotes/Notability) | ❌ | P3 |
| Atajo de dibujo con dos pasos ("mano alzada → figura") | | ✅ | |

---

## 3. PDF e imágenes (PDF Annotator, Xournal++, GoodNotes, Flexcil)

| Función | Detalle | Finily | Prio |
|---|---|---|---|
| Abrir PDF y anotar encima | | ✅ | |
| Exportar PDF anotado (vectorial) | | ✅ | |
| **Resaltar/subrayar texto real del PDF** | Selecciona texto y lo subraya/tacha (Flexcil, PDF Annotator) | ❌ | P1 |
| **Seleccionar y copiar texto del PDF** | | ❌ | P2 |
| Índice (TOC) y miniaturas del PDF | | ❌ | P1 |
| Buscar texto dentro del PDF | | ❌ | P2 |
| Añadir/borrar/rotar/reordenar páginas del PDF | Insertar página en blanco entre páginas | 🟡 | P2 |
| Fusionar PDFs | | ❌ | P3 |
| Sellos, firmas guardadas, formularios | PDF Annotator: sellos y firma reutilizable | ❌ | P3 |
| **Modo dos páginas / lado a lado (PDF + notas)** | Flexcil: PDF a un lado, cuaderno al otro | ❌ | P2 |
| Anotaciones estándar (compatibles con otros lectores) | Exportar como anotaciones PDF y no solo aplanadas | ❌ | P3 |
| Exportar a PNG/SVG/imágenes por página | | 🟡 | P2 |
| Exportar con/sin fondo, rango de páginas | | ❌ | P2 |

---

## 4. Pizarra de clase / presentación (OpenBoard, Samsung Flow, Explain Everything)

| Función | Detalle | Finily | Prio |
|---|---|---|---|
| Modo presentación (ocultar UI) | | ✅ | |
| Páginas tipo diapositiva 16:9 | | ✅ | |
| **Cortina / foco (spotlight)** | Tapa la pantalla y revela una zona | ❌ | P3 |
| **Pantalla completa en segundo monitor/proyector** | Vista del público separada del panel del profesor | ❌ | P2 |
| **Biblioteca de recursos / stickers** | Imágenes, figuras de física/mate, fichas | ❌ | P3 |
| **Fondos de pizarra** | Cuaderno pautado, mapa, pentagrama, ejes cartesianos, papel milimetrado, hoja de examen | 🟡 | P2 |
| Herramientas de clase: cronómetro, dado, contador | OpenBoard apps | ❌ | P3 |
| Calculadora / graficadora integrada (GeoGebra/Desmos) | Gráfica una función tecleada o manuscrita | ❌ | P2 (casa con el modo ecuación) |
| Pegar video/audio/web | OpenBoard | ❌ | P3 |
| Grabación de clase (audio + trazos sincronizados) | **Notability y OneNote**: tocas un trazo y reproduce el audio de ese momento | ❌ | P2 (muy diferencial) |
| Exportar clase a video (replay de trazos) | | ❌ | P3 |

---

## 5. Lienzo y navegación

| Función | Finily | Prio |
|---|---|---|
| Lienzo infinito, zoom/pan | ✅ | |
| Minimapa / "ir a" contenido | ❌ | P3 |
| Scroll vertical continuo de páginas A4 (como GoodNotes) | 🟡 (verificar) | P1 |
| Modo página-por-página con transición | ✅ | |
| Estirar página / ampliar hoja al llegar al borde | ❌ | P2 |
| Deshacer/rehacer ilimitado y persistente | 🟡 (verificar si se guarda en disco) | P2 |
| Bloqueo de zoom, ajustar a ancho | ❌ | P2 |

---

## 6. Calidad de uso (tablet, configuración, accesibilidad)

| Función | Detalle | Prio |
|---|---|---|
| Configuración de gestos y botones del lápiz | P1 |
| Perfiles de tableta y mapeo de pantalla (multi-monitor) | P2 |
| Atajos personalizables | P2 |
| Idioma ES/EN | P2 |
| Modo oscuro/claro y tema de papel | ✅ |
| Barra flotante reubicable (izq/der/arriba/abajo) y compacta | 🟡 (izq por defecto) P2 |
| Barra rápida con acciones (deshacer, rehacer, página, zoom) | ✅ |
| Autoguardado + **recuperación tras cierre inesperado** | 🟡 verificar P1 |
| Historial de versiones por cuaderno | P3 |
| Bloqueo/cifrado de cuadernos con contraseña | P3 |
| Rendimiento: 10 000+ trazos sin lag (caché por tiles, culling) | P1 |

---

## 7. Lo que ya es ventaja de Finily (cuidar y potenciar)

- Entrada nativa Wayland con `QTabletEvent` a 200 Hz.
- Reconocimiento de matemática manuscrita **offline** (sin Python/ONNX).
- "Nivelar letras" y alineación por renglón.
- Hold-to-shape + figuras con nodos editables + raíz auto-ajustable.
- Binario C++ ligero, sin cuentas ni nube.

**Propuesta de nicho:** *la mejor pizarra/cuaderno para estudiar y enseñar matemática y ciencias en Linux*. Combinar modo ecuación + regla/escuadra/transportador + graficar funciones + grabación sincronizada con audio. Ninguna de las apps libres lo hace bien.

---

## 8. Hoja de ruta sugerida

**Fase A — Fundamentos (lo que más se nota al usar la tablet)**
1. Rechazo de palma + gestos táctiles (2 dedos zoom/pan, tap deshacer).
2. Botones del lápiz configurables (borrador / lazo).
3. Panel de miniaturas de páginas (reordenar, duplicar, borrar).
4. Lazo completo: color/grosor de la selección, agrupar, copiar entre páginas.
5. Plumas personalizables en la barra (guardar presets).

**Fase B — Organización**
6. Carpetas anidadas + favoritos + papelera.
7. Formato de archivo por páginas (`manifest.json` + `pages/` + `assets/`) y exportar/importar `.finily`.
8. Recuperación tras cierre inesperado.

**Fase C — PDF y clase**
9. Resaltar/subrayar texto real del PDF, TOC y miniaturas.
10. Regla, escuadra, transportador, compás.
11. Más plantillas de fondo y creación de plantillas propias.
12. Vista PDF + cuaderno lado a lado.

**Fase D — Diferenciadores**
13. Grabación de audio sincronizada con trazos.
14. Búsqueda dentro del contenido (OCR manuscrito + texto PDF).
15. Graficar funciones desde el modo ecuación.
16. Lupa de escritura, stickers/recursos, segundo monitor.

---

## 9. Cómo completar este mapa con precisión

Esto parte de mi conocimiento de cada app. Para pasar de "qué hace" a "cómo se siente" conviene: (1) instalar Xournal++ y OpenBoard en tu Arch (`pacman -S xournalpp`, OpenBoard en AUR) y anotar atajos y flujos; (2) en tu tablet, abrir GoodNotes/Samsung Notes y grabar pantalla de: crear carpeta, mover página, usar lazo, gestos de dedos, regla; (3) lo anotas en este archivo marcando ✅/🟡/❌. Si me pasas capturas o un listado de lo que ves en tu tablet, lo integro.
