# Aurea

Editor de video e composicao para Android/iOS, construido em Flutter.

## Ambiente (tudo local, nada para baixar)

| Ferramenta | Caminho |
| --- | --- |
| Flutter 3.47.2 / Dart 3.13.2 | `.tooling/flutter` (copia local do projeto) |
| Atalho sem espacos (usar este!) | `C:\Users\SnyX\.aurea\flutter` (junction para `.tooling/flutter`) |
| Android SDK | `C:\Users\SnyX\AppData\Local\Android\Sdk` (platforms 34/36, build-tools 34/35, platform-tools, emulador) |
| JDK 21 | `C:\Users\SnyX\AppData\Local\Java\jdk-21.0.12.1+1` (fixado em `android/gradle.properties`) |
| Emulador | AVD `am2test` em `C:\Users\SnyX\.android\avd` |

> **Importante:** invoque o Flutter sempre pelo atalho `C:\Users\SnyX\.aurea\flutter\bin\flutter.bat`
> (ou rode `tool\env.ps1` para colocar tudo no PATH). O caminho do projeto contem espacos
> ("Projetos - Claude") e o compilador de native assets do Dart quebra com o SDK em caminho
> com espacos — a junction resolve isso.

## Comandos do dia a dia

```powershell
# carrega flutter + adb + emulador no PATH da sessao
. .\tool\env.ps1

flutter run                 # roda no dispositivo/emulador conectado
flutter test                # testes
flutter analyze             # analise estatica
flutter build apk --debug   # APK de debug

# subir o emulador am2test
emulator -avd am2test
```

## Estrutura

```
lib/
  main.dart                     # bootstrap (ProviderScope + SharedPreferences)
  src/
    app.dart                    # MaterialApp + tema
    core/
      theme/                    # paleta do logo (grafite #12151A, lima #B8FF3D, violeta #7C62FF)
      widgets/aurea_logo.dart   # logo vetorial em CustomPaint
      storage/prefs.dart        # provider de SharedPreferences
    features/
      projects/
        domain/                 # presets de proporcao/resolucao/fps
        application/            # lista de projetos recentes
        presentation/           # HomeShell (4 abas) + aba Inicio + sheet de novo projeto
      settings/                 # aba Ajustes (padroes de projeto, exportacao, cache)
      user/                     # aba Usuario (perfil local persistido)
      about/                    # aba Sobre (versao, licencas)
      editor/
        domain/                 # motor de composicao: VideoProject > Layer (video/imagem/
                                #   texto/forma/audio) + keyframes (AnimatedDouble/Offset),
                                #   easing por segmento (bezier/bounce/elastic/cyclic/steps),
                                #   efeitos animaveis, blend modes, pivo/skew, e o motor de
                                #   animadores de texto (multi-seletor + wiggly + presets;
                                #   spec em docs/AM2-motor-de-texto.md)
        application/            # EditorController (ops de camada), PlaybackController
                                #   (clock Ticker + ValueNotifier), VideoLayerManager (sync)
        presentation/           # UI estilo Alight: PreviewStage (gestos), TimelineView
                                #   (playhead central, scrub, zoom, trim), toolbar contextual,
                                #   sheets de camada/transform
      media/application/        # importacao de midia (image_picker)
      export/application/       # exportacao via FFmpeg (ffmpeg_kit_flutter_new)
```

### Stack

- **Estado:** flutter_riverpod (Notifier)
- **Preview:** video_player (por enquanto toca o primeiro clip; player de composicao vira depois)
- **Processamento/Export:** ffmpeg_kit_flutter_new
- **Import:** image_picker (galeria/camera)
- **Preferencias:** shared_preferences
- **Icone do app:** gerado do logo (assets/icon) com flutter_launcher_icons
  (`dart run flutter_launcher_icons` para regenerar)

### Ja implementado (esqueleto funcional)

- Home com 4 abas: Inicio, Ajustes, Usuario e Sobre (NavigationBar)
- Criar projeto com configuracoes reais: nome, proporcao (16:9, 9:16, 1:1, 4:5),
  resolucao (720p/1080p/4K) e FPS (24/30/60); atalhos por formato na Home
- Lista de projetos recentes (abrir/excluir; em memoria por enquanto)
- Ajustes persistidos: padroes de novos projetos, salvar na galeria, vibracao, limpar cache
- Perfil local editavel (nome/e-mail) persistido
- Editor: preview + toolbar + timeline; importar video da galeria mede duracao real
- Permissoes de midia/camera/microfone configuradas (AndroidManifest + Info.plist)
- Icone do app (Android adaptive + iOS) nas cores do logo

### Proximos passos sugeridos

1. Player de composicao (playhead unico percorrendo a timeline)
2. Corte/split e arrastar clips na timeline
3. Trilhas de overlay (texto, imagens) e efeitos
4. Export real da timeline via FFmpeg (concat + mix de audio)
5. Persistencia de projetos (JSON em path_provider)
