# Identidade oficial do Aurea — preservada

Fonte: `C:\Users\SnyX\Documents\Projetos - Claude\Aurea` (projeto antigo).
O projeto antigo foi usado **somente** para recuperar os itens abaixo. Nada de
arquitetura, renderer, código Dart/Flutter ou workaround foi trazido.

## Identificadores

| Item | Valor |
| --- | --- |
| Nome do app | Aurea Editor |
| `android:label` | Aurea |
| Android `applicationId` | `com.aurea.aurea` |
| Android `namespace` | `com.aurea.aurea` |
| iOS `PRODUCT_BUNDLE_IDENTIFIER` | `com.aurea.aurea` |
| iOS `CFBundleDisplayName` | Aurea |
| iOS `CFBundleName` | aurea |
| Versão de origem | `1.2.1-beta-a01` (versionCode `2101`) |
| `minSdk` / `targetSdk` | herdado do template Flutter (ver `android/app/build.gradle.kts` do projeto novo) |

### versionCode: duas fontes divergem

| Fonte | versionCode | versionName |
| --- | --- | --- |
| `pubspec.yaml` do projeto antigo | `2101` | `1.2.1-beta-a01` |
| `aurea-release.apk` que está na pasta | `75` | `1.0.0-beta.3` |

O APK arquivado é uma build ANTIGA — nunca foi regerado depois que a versão
subiu no `pubspec`. O `local.properties` do último build registra
`flutter.versionCode=2101`, então **2101 é o número mais recente que o projeto
antigo produziu**, e é o que a instalação do dono provavelmente reporta.

O novo projeto usa `versionCode = 2102`: acima dos dois valores conhecidos.
Se o dono tiver instalado alguma build com número maior que 2102, ele precisa
subir este número em `android/app/build.gradle.kts`.

### minSdk: 26, e isso tem uma consequência

O Aurea antigo declarava `minSdk 24` (Android 7.0). O novo exige **26 (Android
8.0)**, porque o pipeline zero-copy precisa de:

- `AHardwareBuffer` (API 26) — sem ele o frame decodificado teria que ser
  copiado para a CPU antes de virar textura, que é justamente o caminho que a
  arquitetura nova existe para eliminar;
- Vulkan 1.1 de forma confiável — abaixo da 26 a presença de Vulkan é
  esporádica por fabricante.

Consequência: um aparelho em API 24 ou 25 **não consegue instalar a
atualização**. Não é um detalhe de configuração — é uma escolha entre
"instalar e não funcionar" e "não instalar". Fica registrada como decisão
consciente, e o dono pode revertê-la se souber de usuários nessa faixa.

## Assinatura Android — MESMA chave do Aurea oficial

Auditoria do `aurea-release.apk` (o APK oficial distribuído):

```
$ apksigner verify --print-certs aurea-release.apk
Signer #1 certificate DN: C=US, O=Android, CN=Android Debug
Signer #1 certificate SHA-256: 55bf3cc844050df48dff27e71a70cb54e5bca40a5c0db9e43f9d21cfff52b5c8
Signer #1 certificate SHA-1:   9e485d7b003d9327a682a142ecab4005046d59ed
```

Auditoria da debug keystore da máquina:

```
$ keytool -list -v -keystore %USERPROFILE%\.android\debug.keystore -alias androiddebugkey
SHA256: 55:BF:3C:C8:44:05:0D:F4:8D:FF:27:E7:1A:70:CB:54:E5:BC:A4:0A:5C:0D:B9:E4:3F:9D:21:CF:FF:52:B5:C8
SHA1:   9E:48:5D:7B:00:3D:93:27:A6:82:A1:42:EC:AB:40:05:04:6D:59:ED
```

**Os fingerprints são idênticos.** Conclusão: o Aurea oficial nunca teve keystore
de release — ele era assinado com a chave de *debug* da máquina do dono.

Não existe nenhum `*.jks` / `key.properties` em disco além desta debug keystore.

Portanto, para que o novo APK/AAB **atualize por cima** do Aurea instalado, o novo
projeto usa exatamente:

```
storeFile      = C:\Users\SnyX\.android\debug.keystore
storePassword  = android
keyAlias       = androiddebugkey
keyPassword    = android
```

Configurado em `android/key.properties` (ignorado pelo git) e lido por
`android/app/build.gradle.kts`. **Nenhuma chave privada foi copiada, movida,
impressa em claro fora do arquivo de configuração, ou enviada para lugar nenhum.**

### Recomendação registrada (não executada)

Assinar release com chave de debug é frágil: qualquer perda/reinstalação do SDK
invalida a atualização. Quando o dono autorizar, gerar uma keystore de release
própria e migrar **exige** desinstalar/reinstalar uma vez (assinatura diferente é
recusada pelo Android). Decisão do dono — não automática.

## iOS

`com.aurea.aurea`, display name `Aurea`. `ExportOptions.plist` preservado em
`_identity/signing/`. Nenhum certificado ou provisioning profile foi encontrado
no projeto antigo; a distribuição iOS sempre foi por assinatura automática com
`TEAM_ID` vindo de segredo do CI.

## Branding preservado

- `branding/icon/` — `app_icon.png`, `app_icon_foreground.png`, `app_icon_monochrome.png`
- `branding/android-mipmap/` — mipmaps em 5 densidades + `ic_launcher.xml` adaptativo
- `branding/splash/` — `splash_logo.png` nas 5 densidades, `LaunchImage` iOS
- `branding/ios-appicon/` — `AppIcon.appiconset` completo
- `branding/colors.xml` — cor de marca `aurea_background` = `#0F141A`

A cor `#0F141A` é o fundo da marca: pinta o ícone adaptativo, a splash e o
`windowBackground`. Ícone, splash e app precisam ser o mesmo tom.
