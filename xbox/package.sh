#!/bin/bash
# Copyright (c) 2026 Skitzelles - part of Game Vault, https://github.com/skitzellez/xbox-game-vault
# PolyForm Noncommercial License 1.0.0: not for sale, and this notice stays in every copy.
# Packs xbox-dist/AzaharForGameVault_<version>_x64.appx for Xbox Dev Mode: the desktop build (xbox/build_gv.bat), our
# Xbox SDL3 (sdl3-uwp) as SDL3.dll, sdl2-compat (MSVC) as SDL2.dll, our Mesa (D3D12) for OpenGL, the Visual C++
# runtime app-local; marks the exe AppContainer and signs it.   xbox/package.sh <version>
set -e
V=${1:-2127.0.1.1}
REPO=$(cd "$(dirname "$0")/.." && pwd)
ROOT=$(dirname "$REPO")
SDL3_UWP=${SDL3_UWP:-$ROOT/sdl3-uwp}
SDL2_COMPAT=${SDL2_COMPAT:-$ROOT/sdl2-compat}
# our Mesa for Xbox (deps/mesa-ra-3ds, the same files RetroArch for Game Vault ships for the 3DS)
MESA=${MESA:-$ROOT/release-staging/retroarch-gltest/w}
PFX_FILE=${PFX_FILE:-$ROOT/GameVault/cert/GameVault.pfx}
BIN=$REPO/build-gv/bin/RelWithDebInfo
P=$REPO/build-gv/xbox-pkg
VC=$(ls -d "/c/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/"*/bin/Hostx64/x64 | tail -1)
CRT=$(ls -d "/c/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Redist/MSVC/"*/x64/Microsoft.VC143.CRT | tail -1)
SDK="/c/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64"

rm -rf "$P"; mkdir -p "$P/licenses"
cp "$REPO/xbox/AppxManifest.xml" "$P/"
cp -r "$REPO/xbox/Assets" "$P/"
cp "$BIN/azahar.exe" "$P/"
cp "$SDL3_UWP/build-uwp/RelWithDebInfo/SDL3.dll" "$P/SDL3.dll"
cp "$SDL2_COMPAT/build-msvc/SDL2.dll" "$P/SDL2.dll"
for f in opengl32.dll libgallium_wgl.dll dxil.dll spirv_to_dxil.dll; do cp "$MESA/$f" "$P/"; done
for f in vcruntime140.dll vcruntime140_1.dll msvcp140.dll msvcp140_atomic_wait.dll; do cp "$CRT/$f" "$P/"; done
cp "$REPO/license.txt" "$P/licenses/Azahar-license.txt" 2>/dev/null || cp "$REPO/LICENSE.txt" "$P/licenses/Azahar-license.txt"
cp "$SDL3_UWP/LICENSE.txt" "$P/licenses/SDL3-LICENSE.txt"
cp "$SDL2_COMPAT/LICENSE.txt" "$P/licenses/sdl2-compat-LICENSE.txt"
cat > "$P/licenses/SOURCE.txt" <<'SRC'
Game Vault · Azahar is Azahar (https://github.com/azahar-emu/azahar, GPLv2+) with its SDL frontend, built for Xbox by
Skitzelles for Game Vault. The source of this build, with all changes: https://github.com/skitzellez/Azahar-GameVault
(branch gamevault). OpenGL comes from Mesa (MIT) built for Xbox: https://gitlab.freedesktop.org/mesa/mesa.
The libraries keep their own licences.
SRC

PY=${PY:-/c/Users/GamerDad/AppData/Local/Programs/Python/Python312/python.exe}
"$PY" "$(cygpath -w "$REPO/xbox/scrub_paths.py")" "$(cygpath -w "$P")" "$(cygpath -w "$ROOT")"
"$VC/editbin.exe" //nologo //APPCONTAINER //SUBSYSTEM:WINDOWS "$P/azahar.exe"
"$VC/editbin.exe" //nologo //APPCONTAINER "$P/SDL2.dll"
sed -i "s/ Version=\"[0-9.]*\" ProcessorArchitecture/ Version=\"$V\" ProcessorArchitecture/" "$P/AppxManifest.xml"
mkdir -p "$REPO/xbox-dist"; OUT="$REPO/xbox-dist/AzaharForGameVault_$V""_x64.appx"; rm -f "$OUT"
"$SDK/makeappx.exe" pack //o //d "$(cygpath -w "$P")" //p "$(cygpath -w "$OUT")" >/dev/null
PFX_PASSWORD=${PFX_PASSWORD:-$(/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe -NoProfile -ExecutionPolicy Bypass \
    -File "$(cygpath -w "$ROOT/GameVault/pfx_password.ps1")" | tr -d '\r\n')}
"$SDK/signtool.exe" sign //fd SHA256 //a //f "$(cygpath -w "$PFX_FILE")" //p "$PFX_PASSWORD" "$(cygpath -w "$OUT")" >/dev/null
ls -la "$OUT"
