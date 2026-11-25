@echo off
rem Copyright (c) 2026 Skitzelles - part of Game Vault, https://github.com/skitzellez/xbox-game-vault
rem Builds Game Vault · Azahar (Xbox, UWP x64) with the Visual Studio WindowsStore generator: xbox\build_gv.bat
rem The build folder must be OUTSIDE any git checkout: externals\sdl2 applies its Xbox patch with git apply, which
rem silently skips every file when it runs inside another git work tree.
cd /d "%~dp0.."
cmake -B C:\Users\GamerDad\gvbuild\azahar -G "Visual Studio 17 2022" -A x64 -DCMAKE_SYSTEM_NAME=WindowsStore -DCMAKE_SYSTEM_VERSION=10.0.26100.0 ^
  -DCMAKE_BUILD_TYPE=Release -DENABLE_QT=OFF -DENABLE_TESTS=OFF -DENABLE_ROOM_STANDALONE=OFF -DCITRA_WARNINGS_AS_ERRORS=OFF -DENABLE_LIBUSB=OFF -DENABLE_CUBEB=OFF -DENABLE_OPENAL=OFF -DCRYPTOPP_DISABLE_ASM=ON || exit /b 1
cmake --build C:\Users\GamerDad\gvbuild\azahar --config Release --target citra_uwp -- /m || exit /b 1
