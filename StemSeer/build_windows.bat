@echo off
setlocal
echo ==============================================
echo   StemSeer - compilacion para Windows (VST3)
echo ==============================================
where cmake >nul 2>nul || (echo [ERROR] No se encuentra CMake. Instalalo desde https://cmake.org/download/ & goto :fin)
where git   >nul 2>nul || (echo [ERROR] No se encuentra Git. Instalalo desde https://git-scm.com/download/win & goto :fin)

cmake -S . -B build -G "Visual Studio 17 2022" -A x64
if errorlevel 1 (echo [ERROR] Fallo la configuracion. Revisa que Visual Studio 2022 tenga "Desarrollo para el escritorio con C++". & goto :fin)

rem  --parallel 2 para no agotar la RAM (4 GB)
cmake --build build --config Release --parallel 2
if errorlevel 1 (echo [ERROR] Fallo la compilacion. & goto :fin)

echo.
echo ============ LISTO ============
echo VST3:       build\StemSeer_artefacts\Release\VST3\StemSeer.vst3
echo Standalone: build\StemSeer_artefacts\Release\Standalone\StemSeer.exe
echo.
echo Copia la carpeta StemSeer.vst3 a:  C:\Program Files\Common Files\VST3\
:fin
pause
