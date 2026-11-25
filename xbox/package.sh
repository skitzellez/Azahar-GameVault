#!/bin/bash
# Copyright (c) 2026 Skitzelles - part of Game Vault, https://github.com/skitzellez/xbox-game-vault
# PolyForm Noncommercial License 1.0.0: not for sale, and this notice stays in every copy.
# Packs Game Vault · Azahar: dist/uwp/package.ps1 (layout, tiles, manifest, resources) unsigned, then signed with Game
# Vault's certificate as xbox-dist/AzaharForGameVault_<version>_x64.appx.   xbox/package.sh <version>
set -e
V=${1:-2126.2.1.1}
REPO=$(cd "$(dirname "$0")/.." && pwd)
ROOT=$(dirname "$REPO")
BUILD=${BUILD:-C:/Users/GamerDad/gvbuild/azahar}
PFX_FILE=${PFX_FILE:-$ROOT/GameVault/cert/GameVault.pfx}
SDK="/c/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64"
PS=/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe
"$PS" -NoProfile -ExecutionPolicy Bypass -File "$(cygpath -w "$REPO/dist/uwp/package.ps1")" -BuildDir "$BUILD" \
    -Publisher "CN=GamerDad" -PackageVersion "$V" -NoSign | tail -1
mkdir -p "$REPO/xbox-dist"; OUT="$REPO/xbox-dist/AzaharForGameVault_$V""_x64.appx"
cp "$BUILD/AppPackages/Azahar_$V.msix" "$OUT"
PFX_PASSWORD=${PFX_PASSWORD:-$("$PS" -NoProfile -ExecutionPolicy Bypass \
    -File "$(cygpath -w "$ROOT/GameVault/pfx_password.ps1")" | tr -d '\r\n')}
"$SDK/signtool.exe" sign //fd SHA256 //a //f "$(cygpath -w "$PFX_FILE")" //p "$PFX_PASSWORD" "$(cygpath -w "$OUT")" >/dev/null
ls -la "$OUT"
