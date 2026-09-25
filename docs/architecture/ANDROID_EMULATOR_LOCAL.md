# Emulador local do Aurea — 25/09/2026

O executável/AVD anterior não foi localizado. Foi instalado o Android Emulator
oficial 37.1.11 e criada a AVD `Aurea_API35` (Android 15/API 35, Google APIs,
x86_64, perfil Pixel 5). Aceleração WHPX verificada como disponível.

SDK deste host:
`C:/Users/Ruan/AppData/Local/Packages/Claude_pzs8sxrjxfjjc/LocalCache/Local/Android/Sdk`.
O caminho também está no `android/local.properties` local, ignorado pelo Git.
AVD persistida em `C:/Users/Ruan/.android/avd/Aurea_API35.avd`.

Para iniciar em segundo plano no PowerShell, a partir da raiz do projeto:

```powershell
$aureaSdk = (Get-Content android/local.properties | Where-Object { $_ -like 'sdk.dir=*' }) -replace '^sdk.dir=', ''
Start-Process -FilePath "$aureaSdk/emulator/emulator.exe" -ArgumentList '-avd','Aurea_API35','-no-window','-no-audio','-no-snapshot','-gpu','host','-memory','4096' -WindowStyle Hidden
& "$aureaSdk/platform-tools/adb.exe" devices
```

Não iniciar outra instância se `emulator-5554` já estiver conectado. Para encerrar:
`adb -s emulator-5554 emu kill` usando o executável do SDK acima.

Build para o emulador: `android/gradlew.bat :app:assembleDebug -PaureaAbi=x86_64`
(executar dentro de `android`, usando `./gradlew.bat`). Pacote debug:
`com.aurea.aurea.debug`, Activity: `com.aurea.aurea.MainActivity`.
O APK x86_64 usado neste teste foi copiado para
`engine/build/android-p0/aurea-emulator-x86_64.apk`. O APK padrão em
`build/android/app/outputs/apk/debug/app-debug.apk` foi reconstruído para arm64
depois dessa cópia, preservando o artefato de celular.

## Teste executado

- Instalação e abertura do APK debug no emulador.
- Importação pelo seletor de mídia de um fixture existente no backup:
  `motion-source.mp4`, MPEG-4 Part 2 (`video/mp4v-es`), 160×90, 15 fps, 2 s.
- Preview inicial visível; playback/pausa avançou do frame 0 ao frame 12.
- Scrub avançou a imagem para o frame 24 (1,6 s).
- Ida à Home do Android e retorno: processo preservado, sem fechamento observado.
- Nenhum `Fatal signal`, `FATAL EXCEPTION`, `decode falhou` ou `seek falhou`
  encontrado no log capturado desse teste. Houve avisos de áudio não suportado;
  áudio não foi validado.

Capturas e logs locais estão em `engine/build/android-p0/`:
`preview-import.png`, `preview-playback.png`, `preview-scrub.png`,
`emulator-smoke.log`, `emulator-boot.log`.

O log confirma Vulkan com planos YUV pela CPU (política existente para emulador),
decoder `c2.android.mpeg4.decoder` em software. Isto não valida zero-copy,
decodificação por hardware de celular, H.264/HEVC, VFR, HDR, 4K ou exportação.

Downloads oficiais foram verificados por SHA-1 contra os manifests do SDK:
emulador `54fa750822ff462d57e04fc8e98e60f08df2bb61`, imagem
`0103e6dab21290c4b9d16550a3ce99476f884eef`. O sdkmanager ficou bloqueado no
download; os mesmos pacotes oficiais foram baixados por HTTPS e extraídos no SDK.
