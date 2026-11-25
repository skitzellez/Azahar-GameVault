@echo off
rem Game Vault · Azahar: desktop MSVC build of Azahar's SDL frontend (the Xbox app wraps this exe). Usage: xbox\build_gv.bat
rem SDL2 = our sdl2-compat (MSVC build in ..\sdl2-compat\build-msvc), so the Xbox app can load it on top of our Xbox SDL3.
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "%~dp0.."
cmake -S . -B build-gv -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo ^
  -DENABLE_QT=OFF -DENABLE_SDL2_FRONTEND=ON -DUSE_SYSTEM_SDL2=ON -DSDL2_LIBRARY=%~dp0..\..\sdl2-compat\build-msvc\SDL2.lib -DSDL2_INCLUDE_DIR=%~dp0..\..\sdl2-compat\include\SDL2 ^
  -DENABLE_TESTS=OFF -DENABLE_ROOM_STANDALONE=OFF -DENABLE_WEB_SERVICE=OFF -DENABLE_SCRIPTING=OFF -DENABLE_GDBSTUB=OFF ^
  -DENABLE_OPENAL=OFF -DENABLE_CUBEB=OFF -DENABLE_LIBUSB=OFF -DENABLE_VULKAN=OFF -DENABLE_DISCORD_RPC=OFF -DCITRA_WARNINGS_AS_ERRORS=OFF ^
  -DENABLE_LTO=OFF || exit /b 1
cmake --build build-gv --target citra_meta || exit /b 1
