# Finily

**Pizarra digital nativa y anotador de PDF para Linux y Windows x64** (C++20 + Qt 6), diseñada para clases, apuntes y resolución de ejercicios con **tableta gráfica** (Wacom, Huion, XP-Pen, etc.) o **mouse**. En Linux es compatible con **Wayland** y **X11**.

## Características principales

- **Tinta líquida a 200 Hz:** soporte nativo de presión e inclinación por hardware (`QTabletEvent`), estabilizador gaussiano por velocidad con protección de esquinas, sensibilidad configurable y **4 puntas reales** (*Bolígrafo*, *Pluma fuente*, *Lápiz grafito* y *Tiza*), además de escritura fluida con mouse.
- **Biblioteca de cuadernos:** pantalla de inicio con modo claro/oscuro, miniaturas reales, búsqueda en vivo, formatos **Infinita**, **Hoja A4** y **Diapo 16:9**, temas de pizarra y papel, y autoguardado automático.
- **Herramientas completas:** Pluma fuente, Bolígrafo gel, Resaltador (2 modos minimalistas, mezcla *Multiply* sobre fondos claros/PDF y trazo recto al mantener), Borrador (**Por trazo** o **Parcial** con corte limpio en trazos/figuras y recorte exacto hasta intersecciones con `Shift`), Balde/relleno con **capas de profundidad** en sólidos 3D y regiones cerradas, Figuras geométricas + *Hold-to-Shape* + marcas de congruencia, Polilínea, Lazo (mover fluido, escalar, rotar, duplicar, nivelar letras y pasar a texto), Texto inline y Puntero láser.
- **Modo Ecuación (`∑`, tecla `M`):** modo opcional separado de la escritura libre con red neuronal convolucional embebida en C++ puro (100 % offline, sin dependencias de Python/ONNX) para embellecer números, signos matemáticos (auto-coloreados), grados y alinear métricas por renglón.
- **PDF e imágenes:** importa PDF multipágina o imágenes desde disco/portapapeles (`Ctrl+V`) y exporta tu clase completa en PDF vectorial (`Ctrl+E`).

## Dependencias (Linux)

- **Arch / EndeavourOS / Manjaro:**
  ```bash
  sudo pacman -S cmake ninja gcc qt6-base qt6-svg qt6-webengine # o qt6-pdf si está separado
  ```
  *(Nota: en Arch Linux `Qt6::Pdf` viene incluido en el paquete `qt6-webengine`.)*
- **Ubuntu / Debian (24.04+):**
  ```bash
  sudo apt install cmake ninja-build g++ qt6-base-dev qt6-svg-dev qt6-pdf-dev
  ```
- **Fedora:**
  ```bash
  sudo dnf install cmake ninja-build gcc-c++ qt6-qtbase-devel qt6-qtsvg-devel qt6-qtwebengine-devel
  ```

## Compilar y ejecutar en Linux

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/finily
```

Para ejecutar las pruebas unitarias (`finily_tests`):
```bash
ctest --test-dir build --output-on-failure
```

Instalación opcional para tu usuario:
```bash
install -m 755 build/finily "$HOME/.local/bin/finily"
```

## Instalador para Windows x64

Descarga `Finily-Setup-x64.exe` desde [Releases](../../releases). El instalador incluye Qt, se instala por usuario sin requerir permisos de administrador y añade Finily al menú Inicio. La integración directa de los botones físicos del pad Wacom usa el controlador de Linux.

En Windows, los cuadernos nuevos se guardan en `Documentos\Finily`. Si esa carpeta no admite escritura, Finily guarda los nuevos cuadernos en la biblioteca local de tu perfil. Las dos ubicaciones aparecen en la misma biblioteca de la aplicación.

Para compilar el instalador desde el código fuente, instala las dependencias en la terminal de [MSYS2 UCRT64](https://www.msys2.org/docs/environments/):

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,nsis,qt6-base,qt6-svg,qt6-pdf}
```

Desde el símbolo del sistema de Windows, ejecuta en la raíz del proyecto:

```bat
windows\Build-Installer.cmd C:\msys64\ucrt64
```

El instalador se genera en `build-windows\Finily-Setup-x64.exe`.

## Atajos de teclado

| Atajo | Acción |
| --- | --- |
| `P` / `B` / `H` / `E` | Pluma / Bolígrafo / Resaltador / Borrador |
| `S` / `L` / `A` / `T` / `K` | Figuras / Polilínea / Lazo / Texto / Láser |
| `M` | Alternar **Modo Ecuación (`∑`)** / Escritura libre |
| `Tab` | Ocultar / mostrar interfaz (modo presentación) |
| `Espacio` (mantener) | Mover lienzo (mano) |
| `Ctrl+Z` / `Ctrl+Shift+Z` | Deshacer / Rehacer |
| `Ctrl+S` / `Ctrl+O` / `Ctrl+E` | Guardar cuaderno / Importar PDF / Exportar PDF |
| `Ctrl+0` | Restablecer zoom |
| `Re Pág` / `Av Pág` | Página anterior / siguiente |

## Contribuir

¿Quieres ayudar a mejorar Finily? Consulta [CONTRIBUTING.md](CONTRIBUTING.md) para ver la arquitectura, el estilo de código y cómo compilar y correr los tests.

## Licencia

Finily es software libre publicado bajo la licencia **GNU General Public License v3.0** (GPL-3.0). Consulta el archivo [LICENSE](LICENSE) para más detalles.
