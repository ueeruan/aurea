@echo off
rem Aurea PC - compila a aurea.dll (ponte JNI da interface Compose Desktop) com MSVC + Ninja.
rem VARIANTE: compila a copia congelada em build\pc-snap\engine na pasta build\pc-snapd.
rem Uso: tools\build_aurea_pc_jni.cmd [alvo]   (padrao: aurea e aurea_pc_smoke)
rem Pasta de build: engine\build\pc-jni. Saida: engine\build\pc-jni\bin (aurea.dll + DLLs do FFmpeg).
rem Nada e apagado. MSVC em pt-BR: depois de mudar um cabecalho, apague os .obj
rem desta pasta antes de confiar num erro (o Ninja nao rastreia os cabecalhos).
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
set "JDK=D:\Program Files\Eclipse Adoptium\jdk-21.0.12.101-hotspot"
set "PATH=%FFROOT%\bin;%PATH%"
set "BD=%ROOT%\build\pc-snap\bd"
rem Fontes do ncnn/whisper ja baixadas pelo build do Studio (sem rede; so leitura).
set "DEPS=%ROOT%\engine\build\pc-studio\_deps"
set "FETCH="
if exist "%DEPS%\aurea_ncnn-src\CMakeLists.txt" set "FETCH=%FETCH% -DFETCHCONTENT_SOURCE_DIR_AUREA_NCNN=%DEPS:\=/%/aurea_ncnn-src -DFETCHCONTENT_SOURCE_DIR_AUREA_AI_GLSLANG=%DEPS:\=/%/aurea_ncnn-src/glslang"
if exist "%DEPS%\aurea_whisper-src\CMakeLists.txt" set "FETCH=%FETCH% -DFETCHCONTENT_SOURCE_DIR_AUREA_WHISPER=%DEPS:\=/%/aurea_whisper-src"
if not exist "%BD%\build.ninja" (
  cmake -S "%ROOT%\build\pc-snap\engine" -B "%BD%" -G Ninja -DCMAKE_BUILD_TYPE=Release -DAUREA_BUILD_TESTS=OFF -DAUREA_BUILD_DESKTOP_JNI=ON "-DAUREA_GLSLC=D:/Users/Ruan/AppData/Local/Android/Sdk/ndk/28.2.13676358/shader-tools/windows-x86_64/glslc.exe" "-DCMAKE_C_FLAGS=/utf-8" "-DCMAKE_CXX_FLAGS=/utf-8" "-DAUREA_FFMPEG_ROOT=%FFROOT:\=/%" "-DAUREA_JDK_HOME=%JDK:\=/%" %FETCH% "-DCMAKE_RC_COMPILER=D:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/rc.exe" "-DCMAKE_MT=D:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/mt.exe" || exit /b 1
)
if "%~1"=="" (
  cmake --build "%BD%" --parallel 10 --target aurea aurea_pc_smoke || exit /b 1
) else (
  cmake --build "%BD%" --parallel 10 --target %* || exit /b 1
)
endlocal
