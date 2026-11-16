@echo off
rem AUREA Studio (PC) - compila o editor Windows com MSVC + Ninja.
rem Uso: tools\build_aurea_studio.cmd [alvo]   (padrao: aurea_studio)
rem Pasta de build: engine\build\pc-studio. Nada e apagado.
setlocal
set "VSROOT=D:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools"
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul
rem O Windows SDK mora em D: e o vcvars nao o acha sozinho.
set "WSDK=D:\Program Files (x86)\Windows Kits\10"
set "WSDKV=10.0.26100.0"
set "PATH=%WSDK%\bin\%WSDKV%\x64;%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"
set "INCLUDE=%INCLUDE%;%WSDK%\Include\%WSDKV%\ucrt;%WSDK%\Include\%WSDKV%\um;%WSDK%\Include\%WSDKV%\shared;%WSDK%\Include\%WSDKV%\winrt;%WSDK%\Include\%WSDKV%\cppwinrt"
set "LIB=%LIB%;%WSDK%\Lib\%WSDKV%\ucrt\x64;%WSDK%\Lib\%WSDKV%\um\x64"
set "VSLANG=1033"
set "ROOT=%~dp0.."
set "FFROOT=%ROOT%\build\astra-ffmpeg\host-dev\ffmpeg-8.1.2-full_build-shared"
set "PATH=%FFROOT%\bin;%PATH%"
set "BD=%ROOT%\engine\build\pc-studio"
set "TGT=%~1"
if "%TGT%"=="" set "TGT=aurea_studio"
if not exist "%BD%\build.ninja" (
  cmake -S "%ROOT%\engine" -B "%BD%" -G Ninja -DCMAKE_BUILD_TYPE=Release -DAUREA_BUILD_TESTS=OFF -DAUREA_BUILD_DESKTOP=ON "-DCMAKE_C_FLAGS=/utf-8" "-DCMAKE_CXX_FLAGS=/utf-8" "-DAUREA_FFMPEG_ROOT=%FFROOT:\=/%" "-DCMAKE_RC_COMPILER=D:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/rc.exe" "-DCMAKE_MT=D:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/mt.exe" || exit /b 1
)
cmake --build "%BD%" --parallel 12 --target %TGT% || exit /b 1
endlocal
