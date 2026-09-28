# Pacotes e validação — Aurea 2110

Os pacotes ficam em `build/releases/2110/`. Esta é uma entrega beta; não declara
concluído todo o rebuild P0–P10. Notas: [NOTAS_2110.md](NOTAS_2110.md).

## Android

Código compilado: `29dac3f3`. Pacote `com.aurea.aurea`, versionCode2110,
versionName2.0.0-beta2. APKs de release assinados com o certificado existente;
verificação apksigner v2/v3 passou. Cada APK contém somente a ABI indicada.

| Arquivo | ABI | Bytes | SHA256 |
| --- | --- | ---: | --- |
| aurea-2110-android-32bit.apk | armeabi-v7a | 14615583 | `75d0dc20f2ced901905daec8f0226cff5ee0db7793824541d224549408846696` |
| aurea-2110-android-64bit.apk | arm64-v8a | 16739391 | `5bd7798ea63bccf2f6bc389d999911d35af21b422e018826c593639bc3a022ab` |

SHA256 do certificado: `55bf3cc844050df48dff27e71a70cb54e5bca40a5c0db9e43f9d21cfff52b5c8`.
Logs: `engine/build/android-p0/2110-delivery-release-32.log` e
`2110-delivery-release-64.log`. Build debug do mesmo código e103 testes JVM
passaram (`2110-temporal-null-debug.log`). Os testes de interação foram no
emulador API35 x86_64; não constituem teste de execução em ARM físico.

## iOS

O run [36187809403](https://github.com/ueeruan/aurea/actions/runs/36187809403)
compilou código29dac3f3, validou shaders com Apple Metal e gerou o bundle.
A verificação local detectou CFBundleVersion2109; esse primeiro IPA foi guardado
somente como diagnóstico. A correção450974e0 faz o Info.plist usar a versão do
projeto e o verificador rejeitar pacotes desatualizados. O pacote corrigido
passou no run [36189998665](https://github.com/ueeruan/aurea/actions/runs/36189998665)
e na verificação local como2110 (15.002.638bytes,
SHA256 `e31d168d2538f2b966af26de8ca30290da947f7c04c6d4608f36a2bda3eafe1f`).
É um **candidato**, pois a suíte de gestos do mesmo runtime29dac3f3 reproduziu
o travamento ao mover um vídeo pela seta; não constitui a entrega iOS final.

Resultado nativo de gestos36187809403:6/8 passaram (arrastar, pinça, redimensionar
forma, toque curto, gizmoText3D e scrub). O teste de efeito falhou antes de adicionar:
AX retornava o ícone junto de `Add effect`; o nome foi corrigido emffc047e7.
O segundo caso travou a thread principal após mover a camada de vídeo. A próxima
execução coleta amostras externas do processo para diagnosticar a causa, mantendo
os gestos e as verificações. Capturas direcionadas são registradas no manifesto;
a matriz completa permanece o padrão do workflow.

## Evidências e limites

- [Fontes](../architecture/P0_FONT_FALLBACK_2026-09-25.md): regressões compartilhadas,
  fonte sem glifos latinos testada no app, save/reopen e texto legível na exportação.
- [Cena/Nulo 3D](../architecture/P3_CAMERA_NULL_UI_2026-09-25.md): UI→keys→preview→
  save/reopen→export, com filho parentado; exportação300 frames H.264720p decodificada.
- [TimeWarpRGB/VFR](../architecture/P1_TEMPORAL_RGB_DECODE_2026-09-25.md): a exportação
  que falhava em56/63 agora termina63/63, mantendo o efeito e os limites de memória.
- [iOS nativo](../architecture/P0_IOS_NATIVE_2110_2026-09-25.md):25 cenários capturados,
  405 verificações dos decoders, exportação H.264/AAC e limites da inspeção.
- Graph Editor: dois handles respondem, curvas persistem após reabrir, presets
  Bounce/Elastic/Steps e Undo conferidos no app Android.

Os totais históricos721 testes do core e182 GLES passaram antes dos últimos
ajustes; os testes focados posteriores estão discriminados no checkpoint.
Não são uma execução única de toda a suíte no último commit.

Pendentes: Samsung/iPhone físicos, celulares fracos, sessões longas, o vídeo
ampliado ao trocar qualidade (ainda não reproduzido), aceitação completa de
3D/PBR/tracking/flow/blur/partículas e a falha interna Unity/WebView após exportar.
Upscale neural usa CPU e modelo de anime/ilustração, não um modelo temporal geral.
