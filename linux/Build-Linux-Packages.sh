#!/usr/bin/env bash
set -euo pipefail

# Empaqueta Finily para Linux x64:
#   - dist/Finily-Linux-x64.tar.gz (bundle portable con Qt 6 y script install.sh)
#   - dist/Finily-Linux-x64.deb    (si dpkg-deb está disponible, p. ej. Ubuntu/Debian)

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
RAW_BUILD_DIR="${1:-$ROOT_DIR/build-linux-release}"
RAW_DIST_DIR="${2:-$ROOT_DIR/dist}"
mkdir -p "$RAW_BUILD_DIR" "$RAW_DIST_DIR"
BUILD_DIR="$(cd -- "$RAW_BUILD_DIR" && pwd)"
DIST_DIR="$(cd -- "$RAW_DIST_DIR" && pwd)"

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DFINILY_DEV_SNAPSHOTS=OFF \
  -DFINILY_BUILD_TESTS=ON
cmake --build "$BUILD_DIR"
QT_QPA_PLATFORM=offscreen ctest --test-dir "$BUILD_DIR" --output-on-failure

VERSION="$(tr -d '[:space:]' < "$BUILD_DIR/finily-version.txt")"
mkdir -p "$DIST_DIR"

# ---------- 1. Bundle portable (.tar.gz) ----------
BUNDLE_STAGE="$BUILD_DIR/bundle-stage"
BUNDLE_DIR="$BUNDLE_STAGE/Finily"
rm -rf "$BUNDLE_STAGE"
mkdir -p "$BUNDLE_DIR/bin" "$BUNDLE_DIR/lib" "$BUNDLE_DIR/plugins" "$BUNDLE_DIR/share" "$BUNDLE_DIR/Licencias"

cp -a "$BUILD_DIR/finily" "$BUNDLE_DIR/bin/finily"
chmod 755 "$BUNDLE_DIR/bin/finily"
cp -a "$ROOT_DIR/linux/finily.desktop" "$BUNDLE_DIR/share/finily.desktop"
cp -a "$ROOT_DIR/linux/finily-mime.xml" "$BUNDLE_DIR/share/finily.xml"
cp -a "$ROOT_DIR/resources/icons/app/finily-app.svg" "$BUNDLE_DIR/share/finily.svg"
cp -a "$ROOT_DIR/LICENSE" "$BUNDLE_DIR/Licencias/LICENSE"
cp -a "$ROOT_DIR/resources/fonts/Inter-OFL.txt" "$BUNDLE_DIR/Licencias/Inter-OFL.txt"

if command -v qtpaths6 >/dev/null 2>&1; then
  QT_PLUGINS_DIR="$(qtpaths6 --plugin-dir)"
elif [[ -x /usr/lib/qt6/bin/qtpaths6 ]]; then
  QT_PLUGINS_DIR="$(/usr/lib/qt6/bin/qtpaths6 --plugin-dir)"
elif command -v qmake6 >/dev/null 2>&1; then
  QT_PLUGINS_DIR="$(qmake6 -query QT_INSTALL_PLUGINS)"
elif [[ -d /usr/lib/x86_64-linux-gnu/qt6/plugins ]]; then
  QT_PLUGINS_DIR="/usr/lib/x86_64-linux-gnu/qt6/plugins"
elif [[ -d /usr/lib/qt6/plugins ]]; then
  QT_PLUGINS_DIR="/usr/lib/qt6/plugins"
else
  echo "Error: no se pudo localizar el directorio de plugins de Qt 6." >&2
  exit 1
fi

for group in platforms wayland-shell-integration wayland-graphics-integration-client \
             wayland-decoration-client imageformats iconengines tls platforminputcontexts \
             networkinformation; do
  if [[ -d "$QT_PLUGINS_DIR/$group" ]]; then
    mkdir -p "$BUNDLE_DIR/plugins/$group"
    cp -a "$QT_PLUGINS_DIR/$group"/*.so "$BUNDLE_DIR/plugins/$group/" 2>/dev/null || true
    rm -f "$BUNDLE_DIR/plugins/$group"/libqtvirtualkeyboardplugin.so 2>/dev/null || true
  fi
done

should_exclude_lib() {
  local base="$1"
  case "$base" in
    linux-vdso.so*|ld-linux*.so*) return 0 ;;
    libc.so*|libm.so*|libmvec.so*|libdl.so*|libpthread.so*|librt.so*|libutil.so*|libresolv.so*|libnsl.so*|libnss_*.so*|libuuid.so*) return 0 ;;
    libstdc++.so*|libgcc_s.so*|libgomp.so*) return 0 ;;
    libGL*.so*|libEGL*.so*|libOpenGL*.so*|libvulkan*.so*|libdrm*.so*|libgbm*.so*) return 0 ;;
    libwayland-*.so*|libX11*.so*|libxcb.so*|libXau*.so*|libXdmcp*.so*|libXext*.so*|libXfixes*.so*|libXrender*.so*) return 0 ;;
    libfontconfig*.so*|libfreetype*.so*|libexpat*.so*|libz.so*|libzstd*.so*) return 0 ;;
    libglib-2.0*.so*|libgobject-2.0*.so*|libgio-2.0*.so*|libgmodule-2.0*.so*|libgthread-2.0*.so*) return 0 ;;
    libdbus-1*.so*|libsystemd*.so*|libudev*.so*|libcap*.so*|libmount*.so*|libblkid*.so*|libffi*.so*) return 0 ;;
    libssl*.so*|libcrypto*.so*|libgssapi_krb5*.so*|libkrb5*.so*|libk5crypto*.so*|libcom_err*.so*|libkeyutils*.so*) return 0 ;;
    libxkbcommon*.so*|libharfbuzz*.so*|libgraphite2*.so*|libpng16*.so*|libbrotli*.so*) return 0 ;;
    libproxy*.so*|libpxbackend*.so*|libpcre2-8*.so*|libcurl*.so*|libduktape*.so*|libnghttp*.so*|libngtcp2*.so*|libssh2*.so*|libpsl*.so*|libidn2*.so*|libunistring*.so*|liburing*.so*) return 0 ;;
    *) return 1 ;;
  esac
}

copy_deps() {
  local target="$1"
  while IFS= read -r lib; do
    [[ -n "$lib" && -f "$lib" ]] || continue
    local base
    base="$(basename "$lib")"
    if should_exclude_lib "$base"; then
      continue
    fi
    if [[ ! -e "$BUNDLE_DIR/lib/$base" ]]; then
      cp -L "$lib" "$BUNDLE_DIR/lib/$base"
      chmod 755 "$BUNDLE_DIR/lib/$base" || true
    fi
  done < <(ldd "$target" | awk '/=> \//{print $3}')
}

copy_deps "$BUNDLE_DIR/bin/finily"
while IFS= read -r -d '' so; do
  copy_deps "$so"
done < <(find "$BUNDLE_DIR/plugins" -type f -name '*.so' -print0)

# Segunda pasada sobre las bibliotecas ya copiadas (p. ej. libicudata desde libicuuc).
for bundled in "$BUNDLE_DIR/lib"/*.so*; do
  [[ -f "$bundled" ]] && copy_deps "$bundled"
done

cat > "$BUNDLE_DIR/finily" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd -- "$(dirname -- "$(readlink -f -- "$0")")" && pwd)"
export LD_LIBRARY_PATH="$HERE/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$HERE/plugins"
exec "$HERE/bin/finily" "$@"
EOF
chmod 755 "$BUNDLE_DIR/finily"

cat > "$BUNDLE_DIR/install.sh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
SRC="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
APP_HOME="${XDG_DATA_HOME:-$HOME/.local/share}/finily-app"
BIN_DIR="$HOME/.local/bin"
APPS_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
MIME_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/mime/packages"
ICON_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/icons/hicolor/scalable/apps"

rm -rf "$APP_HOME"
mkdir -p "$APP_HOME" "$BIN_DIR" "$APPS_DIR" "$MIME_DIR" "$ICON_DIR"
cp -a "$SRC/." "$APP_HOME/"
ln -sfn "$APP_HOME/finily" "$BIN_DIR/finily"
cp -a "$APP_HOME/share/finily.svg" "$ICON_DIR/finily.svg"
cp -a "$APP_HOME/share/finily.xml" "$MIME_DIR/finily.xml"
sed "s|^Exec=.*|Exec=$BIN_DIR/finily %f|" "$APP_HOME/share/finily.desktop" > "$APPS_DIR/finily.desktop"

update-mime-database "${XDG_DATA_HOME:-$HOME/.local/share}/mime" 2>/dev/null || true
update-desktop-database "$APPS_DIR" 2>/dev/null || true
gtk-update-icon-cache -f -t "${XDG_DATA_HOME:-$HOME/.local/share}/icons/hicolor" 2>/dev/null || true

echo "Finily instalado en $BIN_DIR/finily"
EOF
chmod 755 "$BUNDLE_DIR/install.sh"

cat > "$BUNDLE_DIR/uninstall.sh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
APP_HOME="${XDG_DATA_HOME:-$HOME/.local/share}/finily-app"
rm -f "$HOME/.local/bin/finily" \
      "${XDG_DATA_HOME:-$HOME/.local/share}/applications/finily.desktop" \
      "${XDG_DATA_HOME:-$HOME/.local/share}/mime/packages/finily.xml" \
      "${XDG_DATA_HOME:-$HOME/.local/share}/icons/hicolor/scalable/apps/finily.svg"
rm -rf "$APP_HOME"
update-mime-database "${XDG_DATA_HOME:-$HOME/.local/share}/mime" 2>/dev/null || true
update-desktop-database "${XDG_DATA_HOME:-$HOME/.local/share}/applications" 2>/dev/null || true
echo "Finily desinstalado."
EOF
chmod 755 "$BUNDLE_DIR/uninstall.sh"

tar -C "$BUNDLE_STAGE" -czf "$DIST_DIR/Finily-Linux-x64.tar.gz" Finily
echo "Generado $DIST_DIR/Finily-Linux-x64.tar.gz"

# ---------- 2. Paquete .deb (Ubuntu / Debian) ----------
if command -v dpkg-deb >/dev/null 2>&1; then
  DEB_STAGE="$BUILD_DIR/deb-stage"
  rm -rf "$DEB_STAGE"
  DESTDIR="$DEB_STAGE" cmake --install "$BUILD_DIR" --prefix /usr
  mkdir -p "$DEB_STAGE/DEBIAN"

  DEPS=""
  if command -v dpkg-shlibdeps >/dev/null 2>&1; then
    mkdir -p "$BUILD_DIR/debian"
    printf 'Source: finily\nMaintainer: Yorditry2 <noreply@github.com>\n\nPackage: finily\nArchitecture: amd64\n' > "$BUILD_DIR/debian/control"
    DEPS="$(cd "$BUILD_DIR" && dpkg-shlibdeps -O "$DEB_STAGE/usr/bin/finily" | sed 's/^shlibs:Depends=//')" || DEPS=""
    rm -rf "$BUILD_DIR/debian"
  fi
  if [[ -z "$DEPS" ]]; then
    DEPS="libqt6core6t64 | libqt6core6, libqt6gui6t64 | libqt6gui6, libqt6widgets6t64 | libqt6widgets6, libqt6network6t64 | libqt6network6, libqt6svg6, libqt6pdf6"
  fi

  INSTALLED_KB="$(du -sk "$DEB_STAGE/usr" | awk '{print $1}')"
  cat > "$DEB_STAGE/DEBIAN/control" <<EOF
Package: finily
Version: ${VERSION}
Section: graphics
Priority: optional
Architecture: amd64
Installed-Size: ${INSTALLED_KB}
Depends: ${DEPS}
Recommends: qt6-wayland
Maintainer: Yorditry2 <noreply@github.com>
Homepage: https://github.com/Yorditry2/Finily
Description: Pizarra digital nativa y anotador de PDF
 Finily es una pizarra digital nativa y cuaderno vectorial en C++20/Qt6 con
 soporte de tableta gráfica a 200 Hz, geometría inteligente, modo ecuación
 offline, biblioteca de cuadernos e importación/exportación PDF.
EOF

  cat > "$DEB_STAGE/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e
if command -v update-desktop-database >/dev/null 2>&1; then
  update-desktop-database /usr/share/applications >/dev/null 2>&1 || true
fi
if command -v update-mime-database >/dev/null 2>&1; then
  update-mime-database /usr/share/mime >/dev/null 2>&1 || true
fi
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
  gtk-update-icon-cache -f -t /usr/share/icons/hicolor >/dev/null 2>&1 || true
fi
EOF
  chmod 755 "$DEB_STAGE/DEBIAN/postinst"
  cp -a "$DEB_STAGE/DEBIAN/postinst" "$DEB_STAGE/DEBIAN/postrm"

  dpkg-deb --root-owner-group --build "$DEB_STAGE" "$DIST_DIR/Finily-Linux-x64.deb"
  echo "Generado $DIST_DIR/Finily-Linux-x64.deb"
fi
