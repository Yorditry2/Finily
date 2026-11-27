# Editor — dónde va cada función (borrador para decidir)

Base: inventario de 125 funciones del editor + lo ya decidido en los mockups 18–20.
Objetivo: que la barra quede limpia, que nada útil se pierda y que todo lo "mágico" se pueda entender y apagar.

## Leyenda
| Sitio | Qué significa |
|---|---|
| **Barra** | Botón visible en la barra del editor |
| **Panel** | Aparece al tocar otra vez la herramienta activa (mockup 20) |
| **Cápsula** | Aparece pegada a lo seleccionado / recién dibujado; lo mismo con clic derecho |
| **⋯ Más** | Menú «⋯» de la cápsula de selección (antes «Propiedades…» y menú contextual) |
| **⚙** | Ajustes del editor (un solo engrane global, a la derecha de la barra) |
| **Implícito** | Gesto o atajo. Funciona solo; se explica y se puede apagar en ⚙ › *Gestos y atajos* |
| **Quitar / Fusionar** | Sale tal como está hoy |

## Decisiones ya tomadas (mockups 18–20)
- Barra: Puntero · Mano | Pluma · Resaltador · Borrador · Relleno | Lazo | Figuras · Texto · Elementos · Láser | grosores · colores · arcoíris | 100 % · ⚙
- Un solo ⚙ global; los paneles no tienen engrane propio.
- Mismo control de grosor en Pluma, Resaltador y Borrador: 3–4 puntos, mantener pulsado = ajuste libre.
- Colores y grosores de la barra son **de la herramienta activa** (la pluma y el resaltador recuerdan los suyos).
- Resaltador: solo opacidad y grosor a la vista.
- Relleno se queda y crece: *Región* (6 tramas) y *Tramo* (tocar el lado y se pinta). Sin «entre curvas» ni «ángulo».
- Lazo y Seleccionar separados. Lazo: Libre/Rectángulo (selector pequeño) + qué agarra.
- Figuras: el panel elige *qué*; el trazo (continua, guiones, puntos, flechas, cota) va en la cápsula pegada a la figura.
- Stickers + fórmulas + imagen + PDF + tabla → **Elementos**, panel lateral izquierdo, con categorías que pueden crecer (tienda futura).
- Patrón **Biblioteca** para todo lo que crece: pinceles, figuras, elementos, paletas.
- Láser: Punto/Estela; color de la barra; duración en ⚙.
- Ventanas propias: nada de `QColorDialog` ni diálogos del sistema.

---

## I. Escritura y tinta
| # | Función | Sitio | Nota |
|---|---|---|---|
| 1–4 | Puntas Bolígrafo, Pluma fuente, Lápiz grafito, Tiza | Panel Pluma | Con trazo grande de muestra. En el mockup 20 la cuarta dice «Pincel»: corregir a **Tiza** |
| 5 | Resaltador | Barra | |
| 6 | Subrayado siempre recto | ⚙ Resaltador | «Enderezar al soltar» |
| 7 | Opacidad del resaltador | Panel Resaltador | Deslizador |
| 8 | Grosor con vista previa | Barra + panel | Control común |
| 9 | Estabilizador 0–9 | ⚙ Escritura | Bajo/Medio/Alto; con «Personalizado» para valor exacto |
| 10 | Predicción de punta | ⚙ Escritura | Encendido por defecto |
| 11 | Recorte de ganchos | Implícito, sin ajuste | Siempre activo, nadie lo quiere apagar |
| 12 | Remate afilado | ⚙ Escritura | Solo Pluma fuente |
| 13 | `Shift` = línea recta | Implícito | |
| 14 | `D` = trazo auxiliar en guiones | Implícito | |
| 15 | Rueda radial de colores (doble `P` / doble clic Wacom) | Implícito | Apagable en ⚙ Tableta |
| 16 | Bandeja de 4 colores en el dock | **Quitar** | La sustituyen los 6 colores por herramienta |
| 17 | Paleta de 7 colores + `+` | **Fusionar** | Con los colores de la barra + arcoíris (selector propio). Se mantiene que se adapten al tema |
| 18 | 5 pinceles favoritos (1–5) | Biblioteca › Favoritos | Las teclas 1–5 siguen; se gestionan en la biblioteca |
| 19 | Láser | Barra | |
| 20 | Estilo de puntero (lapicito/aro/cruz) | ⚙ Tableta | |

## II. Embellecer y reconocer
| # | Función | Sitio | Nota |
|---|---|---|---|
| 21 | Auto-embellecer al pausar (`M`) | ⚙ Escritura | Interruptor + tiempo de pausa; `M` sigue como atajo |
| 22 | `Ctrl+Z` recupera lo original | Implícito | |
| 23 | Embellecer y ordenar escritura | Cápsula (con lazo) | |
| 24 | Convertir a texto | Cápsula (con lazo) | |

## III. Auto-forma y gestos geométricos
Aquí va lo que pediste: que cada uno pueda decidir qué se convierte solo.

| # | Función | Sitio | Nota |
|---|---|---|---|
| 35 | Auto-forma al mantener (interruptor general) | ⚙ Figuras | + tiempo de espera (380 ms) |
| 25–33 | Qué reconoce: línea, **curva**, círculo, triángulo (y rectángulo/isósceles exacto), cuadrado/rectángulo, cuadrilátero, polígonos 5–8, `√`, ángulo en L | ⚙ Figuras › *Reconocer* | Una ficha por tipo. Quien no quiere curvas apaga «Curva» |
| 34 | Seguir ajustando sin levantar + imán | Implícito | |
| 36 | Arquito → ángulo automático | ⚙ Gestos sobre figuras | Interruptor |
| 37 | Esquinita → marca de 90° | ⚙ Gestos sobre figuras | |
| 38 | Flecha de un solo trazo | ⚙ Gestos sobre figuras | |
| 39 | `>` en extremo → punta de flecha | ⚙ Gestos sobre figuras | |
| 40 | Puntito → punto divisor | ⚙ Gestos sobre figuras | |
| 41 | Marcas de congruencia a mano | ⚙ Gestos sobre figuras | |
| 42 | Mantener sobre un lado → cápsula de marcas y resaltado | Cápsula | El **resaltado del tramo** se mueve a Relleno › Tramo; la cápsula se queda solo con las 8 marcas. Apagable en ⚙ |

## IV. Figuras
| # | Función | Sitio | Nota |
|---|---|---|---|
| 43–51 | Segmento, curva, arco, cota, resorte, polilínea (`L`), elipse, rectángulo, sector | Panel Figuras | |
| 52–54 | 5 cuadriláteros, 3 triángulos, polígonos 5–8 | Panel Figuras | **Reducir:** un icono por familia; mantener pulsado muestra las variantes |
| 55 | 10 sólidos 3D | Panel Figuras › 3D | Se ven 4; el resto en «Más figuras ›» (biblioteca) |
| 56 | Ejes + `f(x)` | Panel + Cápsula | El botón `fx` en la cápsula de los ejes |
| 57, 59, 60 | `√`, 90°, llave | Panel Figuras | |
| 58 | Ángulo con grados automáticos | Panel + Cápsula | La etiqueta se edita en la cápsula |
| 61 | 8 marcas de congruencia y pared rugosa | Cápsula del lado | |
| 62 | Tabla | **Mover** a Elementos | |
| 63 | Estilos de línea y flechas | Cápsula | Ya decidido |

## V. Guías e imán
| # | Función | Sitio | Nota |
|---|---|---|---|
| 64 | Imán a puntos clave | ⚙ Figuras | |
| 65 | Guías 0°/90° y perpendicular | ⚙ Figuras | Interruptores separados |
| 66 | Alineación X/Y | ⚙ Figuras | |
| 67 | Vínculos vivos | Implícito, sin ajuste | Es el corazón del motor |
| 68 | Editar vértices sin cambiar de herramienta; `Shift` + arrastrar | Implícito | |
| 69 | Interruptor guías e imán | ⚙ Figuras | Interruptor general de 64–66 |

## VI. Relleno
| # | Función | Sitio | Nota |
|---|---|---|---|
| 70 | Rellenar cualquier región cerrada | Panel Relleno › Región | |
| 71 | 6 tramas | Panel Relleno | Ya no hace falta un segundo clic |
| 72 | Hasta 3 capas / tocar otra vez quita | Implícito | |
| 73 | Relleno que sigue a su frontera | Implícito | |

## VII. Borrador
| # | Función | Sitio | Nota |
|---|---|---|---|
| 74–75 | Por trazo / parcial | Panel Borrador | |
| 76 | `Shift` = recortar entre cruces | Implícito | Lo menciona una ayuda pequeña en el panel |
| 77 | Qué borrar y 10 tamaños | Panel Borrador | Tres fichas (Tinta / Resaltador / Figuras) y control común de grosor. Las tres opciones de hoy salen de combinar las fichas |
| 106 | Borrar anotaciones de la página | Panel Borrador | Sin doble toque rojo: se borra y sale un aviso con «Deshacer» (regla de Inicio) |

## VIII. Selección y lazo
| # | Función | Sitio | Nota |
|---|---|---|---|
| 78 | Seleccionar (puntero) | Barra | |
| 79 | Lazo libre/rectángulo | Panel Lazo | |
| 80–81 | Mover/escalar/rotar; color y grosor al instante | Implícito | |
| 82 | Diálogo Propiedades | **Quitar** | Se reparte en la cápsula (borde, relleno, línea, etiqueta) y en un panel propio dentro de «⋯ Más» para el grosor exacto |
| 83 | Cortar, copiar, pegar, duplicar | ⋯ Más + atajos | |
| 84–87 | Voltear, orden, agrupar, bloquear | ⋯ Más | |
| 88 | Abrir espacio debajo | ⋯ Más | Candidato a revisar: ¿se usa? |
| 89 | Recorte a página nueva | ⋯ Más | Candidato a revisar |
| 90 | Clic derecho = menú | Implícito | |

## IX. Texto y Elementos
| # | Función | Sitio | Nota |
|---|---|---|---|
| 91 | Texto | Barra | Panel de texto mientras escribes (aprobado) |
| 92–93 | 43 stickers en 11 pestañas + buscador | Elementos | Las pestañas pasan a categorías por curso |
| 94 | 3 estilos de sticker | Elementos | Selector pequeño en la cabecera del panel |
| 95 | Mis elementos | Elementos | |

## X. Imágenes
| # | Función | Sitio | Nota |
|---|---|---|---|
| 96 | Insertar imagen / pegar / arrastrar | Elementos › Imagen + implícito | |
| 97 | Imágenes recientes | Elementos › Recientes | Sale del menú ⋯ |
| 98 | Vectorizar figura | Cápsula de la imagen | Sin emoji |
| 99 | Extraer con IA | Cápsula de la imagen | La clave API va en ⚙ › IA. Sin emoji |

## XI. Papel e interfaz
| # | Función | Sitio | Nota |
|---|---|---|---|
| 100–103 | Tema, plantilla, escala, formato | Panel Papel | Botón de página a la derecha de la barra |
| 104 | Barra a la izquierda/arriba/abajo | ⚙ Paneles | (o arrastrar, como en el mockup 14) |
| 105 | Dirección de páginas | ⚙ Páginas | |
| 107 | `Tab` interfaz limpia, `F11` | Modo presentación | **Pendiente de diseñar** |

## XII. Páginas, pestañas y archivos
| # | Función | Sitio | Nota |
|---|---|---|---|
| 108 | Cápsula de páginas | Abajo | Oculta en pizarra infinita |
| 109–110 | Miniaturas y gestión de páginas | Vista de páginas | **Pendiente de diseñar** |
| 111–112 | Pestañas, renombrar | Fila de pestañas | |
| 113 | Importar PDF | Elementos › Página de PDF + `Ctrl+O` | |
| 114 | Exportar PDF | Botón compartir | |
| 115 | Deshacer, guardar, autoguardado | Barra + implícito | |

## XIII. Cámara y Wacom
| # | Función | Sitio | Nota |
|---|---|---|---|
| 116–118 | Zoom y desplazamiento | Implícito + Mano en la barra | |
| 119 | Sensibilidad de presión | ⚙ Tableta | |
| 120–124 | Goma trasera, botones del lápiz, invertir | ⚙ Tableta | Con un dibujo del lápiz que muestre qué hace cada botón |
| 125 | Botones del pad | ⚙ Tableta | Hoy son fijos: permitir elegir qué hace cada botón |

---

## Secciones del ⚙ resultante
1. **Escritura** — suavizado, predicción, remate afilado, embellecer al pausar.
2. **Resaltador** — enderezar al soltar, punta redonda, detrás de la escritura.
3. **Figuras** — auto-forma (general + tiempo + qué reconoce), gestos sobre figuras, imán y guías.
4. **Láser** — duración de la estela.
5. **Paneles** — Elementos izquierda/derecha, posición de la barra, estilo de sticker por defecto.
6. **Páginas** — dirección de navegación.
7. **Tableta** — presión, puntero, botones del lápiz, goma, pad, rueda de colores.
8. **IA** — clave y modelo.
9. **Gestos y atajos** — lista de todo lo implícito con su interruptor (si lo tiene) y su tecla.

## Qué se quita o se fusiona
- Bandeja de 4 colores del dock (16) y paleta de 7 del popover (17) → colores por herramienta en la barra.
- Diálogo Propiedades (82) → cápsula + «⋯ Más».
- Doble toque rojo de «Borrar página» (106) → aviso con Deshacer.
- Resaltado de tramo de la cápsula de marcas (42) → Relleno › Tramo.
- Tabla (62) e imágenes recientes (97) → Elementos.
- Emojis en menús (98, 99).
- Revisar si se usan: 88 «Abrir espacio debajo» y 89 «Recorte a página nueva».

## Pendiente
- Modo presentación (107) y vista de páginas (109–110).
- Corregir la cuarta punta del mockup 20 («Pincel» → «Tiza»).

---

## Cambios tras la revisión final (mockup 22) — prevalecen sobre las tablas de arriba
- **#88 Abrir espacio debajo y #89 Enviar a página nueva:** se quedan, dentro de «⋯ Más» del Lazo.
- **#24:** se llama «Pasar a fórmula / texto», porque el motor reconoce sobre todo matemáticas.
- **#52–54 Variantes de figura:** se acabó el «mantener pulsado». Las figuras con variantes llevan una esquinita; al tocarlas otra vez se abren las variantes, y después de dibujar se cambian desde la cápsula. Familias: Triángulo, Rectángulo, Círculo (incluye Arco y Sector) y Polígono (5 a 8 lados). El **Resorte** va en Medidas.
- **#20 Grosor fino:** se abre al tocar otra vez el punto activo, o con clic derecho.
- **#42 Tramo:** la cápsula de marcas de arista tiene el botón «Resaltar tramo» con el color activo. Relleno › Tramo sigue existiendo.
- **Cápsula de figura:** solo muestra lo que aplica. Una figura cerrada tiene Sólida, Guiones y Puntos. Una línea o curva añade →, ↔ y Cota.
- **Archivo:** el botón Exportar abre un menú con PDF, Página como imagen, Guardar copia, Abrir otro archivo e Importar PDF. Clic derecho en una pestaña da Renombrar, Duplicar, Guardar copia, Mostrar en la biblioteca, Exportar y Cerrar.
- **Barra a la izquierda o flotante:** conserva grosores y colores. La segunda fila desaparece y sus botones pasan a la fila de pestañas.
- **⚙ Figuras:** «Figura perfecta al mantener» solo vive aquí. Cada forma reconocida y cada gesto tiene su casilla.
- **⚙ Tableta:** los 4 botones del pad y los 2 botones laterales del lápiz se eligen en desplegables propios. Además: Invertir botones del lápiz, Estilo de puntero (Lapicito, Aro o Cruz) y un interruptor para la rueda radial.
- **⚙ Páginas:** dirección Vertical u Horizontal, formato de la hoja nueva y número de página.
- **⚙ IA:** clave de API, modelo y «Probar conexión».
- **Biblioteca › Atajos 1–5:** cada número guarda la combinación completa de herramienta, punta, color y grosor.
- **Hoja en modo cuaderno:** tamaños A4 ↕, A4 ↔, Carta y 16:9.
