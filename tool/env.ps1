# Carrega o ambiente de desenvolvimento do Aurea na sessao atual do PowerShell.
# Uso:  . .\tool\env.ps1

$flutterBin = "C:\Users\SnyX\.aurea\flutter\bin"          # junction sem espacos -> .tooling\flutter
$sdkRoot    = "C:\Users\SnyX\AppData\Local\Android\Sdk"
$jdkHome    = "C:\Users\SnyX\AppData\Local\Java\jdk-21.0.12.1+1"

$env:ANDROID_HOME     = $sdkRoot
$env:ANDROID_SDK_ROOT = $sdkRoot
$env:JAVA_HOME        = $jdkHome

$env:Path = "$flutterBin;$sdkRoot\platform-tools;$sdkRoot\emulator;$jdkHome\bin;$env:Path"

Write-Host "Ambiente Aurea carregado:"
Write-Host "  flutter  -> $flutterBin"
Write-Host "  adb      -> $sdkRoot\platform-tools"
Write-Host "  emulator -> $sdkRoot\emulator (AVD: am2test)"
Write-Host "  java     -> $jdkHome"
