# Finily

**Pizarra digital nativa y anotador de PDF para Linux** (C++20 + Qt 6), diseñada para clases, apuntes y resolución de ejercicios con **tableta gráfica** (Wacom, Huion, XP-Pen, etc.) o **mouse** — compatible con **Wayland** y **X11**.

## Características principales

- **Tinta líquida a 200 Hz:** soporte nativo de presión e inclinación por hardware (`QTabletEvent`), estabilizador gaussiano por velocidad y perfiles de presión (*Ligera / Normal / Firme*), además de escritura fluida con mouse.
- **Biblioteca de cuadernos:** pantalla de inicio con modo claro/oscuro, búsqueda en vivo, formatos **Infinita**, **Hoja A4** y **Diapo 16:9**, temas de pizarra y papel, y autoguardado automático.
- **Herramientas completas:** Pluma fuente, Bolígrafo gel, Resaltador (compuesto debajo de la tinta), Borrador (por trazo o parcial), Figuras geométricas + *Hold-to-Shape*, Polilínea, Lazo (mover, escalar, rotar, duplicar, nivelar letras y pasar a texto), Texto inline y Puntero láser.
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

## Compilar y ejecutar

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/finily
```

Instalación opcional para tu usuario:
```bash
install -m 755 build/finily "$HOME/.local/bin/finily"
```

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
