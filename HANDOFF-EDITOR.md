# Traspaso del diseño del Editor

Lee primero `AGENTS.md`, `DESIGN.md` y `HANDOFF-REDISENO.md` (Inicio ya está hecho en la rama `redesign/home`).

## Estado
El **diseño** del Editor está aprobado por el usuario, pero **no hay nada implementado en C++**. El usuario dijo que lo que no le guste lo ajustará después.

## Fuentes de verdad, por prioridad
1. `mockups/redesign/22-editor-arreglos.html` es el **mockup definitivo** y es interactivo. Los botones de arriba recorren las escenas:
   - 1–9: herramientas y paneles.
   - A–I: archivo, menú de pestaña, variantes de figura, marcas y resaltar tramo, Lazo ⋯ Más, reconocimiento de figuras, cápsula de imagen, cápsula de ejes y pad Wacom.
   - Claro/Oscuro y «Barra: Arriba/Izquierda/Flotante».
   - También se abre con `#escena`, por ejemplo `22-editor-arreglos.html#wacom` o `#pen&left`.
2. `EDITOR-FUNCIONES.md` asigna un sitio a cada una de las 125 funciones del editor: Barra, Panel, Cápsula, «⋯ Más», ⚙, Implícito o Quitar. **La sección final, «Cambios tras la revisión final», manda sobre las tablas.**
3. Los mockups 12–21 son **historia**: propuestas descartadas o intermedias. No los implementes.

## Decisiones clave (no reabrir sin preguntar)
- La barra tiene **2 filas**, como un navegador.
  - Fila 1: Inicio, pestañas (punto ámbar si no se guardó) y pantalla completa.
  - Fila 2: Deshacer y Rehacer · herramientas · grosores y colores de la herramienta activa · Páginas · Hoja · Zoom · Exportar · ⚙.
- Orden de herramientas: Puntero, Mano | Pluma, Resaltador, Borrador, Relleno | Lazo | Figuras, Texto, Elementos, Láser.
- Al tocar otra vez una herramienta se abre su **panel**. Hay **un solo ⚙** con 9 secciones: Escritura, Resaltador, Figuras, Láser, Paneles, Páginas, Tableta y táctil, IA, Gestos y atajos.
- **Colores y grosores van por herramienta.** Pluma y Resaltador recuerdan los suyos. El control de grosor es el mismo en Pluma, Resaltador y Borrador.
- Las puntas de Pluma son Pluma, Bolígrafo, Grafito y **Tiza**. Hay más en la Biblioteca.
- Herramientas que siguen separadas:
  - Relleno, con Región (6 tramas) y Tramo/Arco.
  - Lazo (muchas cosas) y Seleccionar (una).
  - Láser, con Punto o Estela.
- Elementos (stickers, fórmulas, imagen, PDF, tabla) va en un **panel a la izquierda**, al estilo de Canva. Páginas va en un panel a la derecha.
- El estilo de una figura se cambia en su **cápsula flotante**. Desaparece el diálogo «Propiedades…».
- La **Biblioteca** es un modal que reúne puntas, figuras, elementos, paletas y atajos 1–5, y deja sitio para paquetes futuros.
- Nada de ventanas del sistema (`QColorDialog`, `QMenu`, `QInputDialog`). Todo son cápsulas `GlassCapsule` hijas de `FloatingToolbar`, por la regla de Wayland y Wacom de `AGENTS.md`. Nada de emojis.

## Trabajo de motor nuevo (no es solo UI)
1. Filtro «Qué agarra» del Lazo: Escritura, Figuras, Texto e imágenes.
2. Láser: modo Punto, color elegible y duración de 1, 2 o 4 s. Hoy es siempre una estela roja de 1,2 s.
3. Interruptores para cada forma en `recognizeFreehandShape()` y para cada gesto en `tryCommitFreehandGeometry()`.
4. Mapeo configurable de los 4 botones del pad (hoy fijos en `src/ui/MainWindow.cpp` ~1100) y de los 2 botones laterales del lápiz, más Invertir.
5. Borrador con objetivos combinables: Tinta, Resaltador y Figuras.
6. Atajos 1–5 que guarden la combinación completa de herramienta, punta, color y grosor.
7. Cambiar el tipo de una figura después de dibujarla (variantes desde la cápsula).

## Pendiente
- Diseñar el modo presentación y la vista de páginas a pantalla completa.
- Elegir el logo final (`branding/`), al terminar.
- Implementar el Editor por fases, siguiendo el estilo de `HANDOFF-REDISENO.md`: una fase por commit en `redesign/home`, verificada con capturas y con el binario copiado a `~/.local/bin/finily`.
