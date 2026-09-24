# Anúncios no iOS (Unity LevelPlay)

A arquitetura é a mesma do Android (`android/app/src/main/java/com/aurea/aurea/ads/`), com os IDs do iOS.

| Arquivo | O que faz |
|---|---|
| `AureaAdsManager.swift` | `AdsPolicy`, `AdsFrequencyController` e o manager. O manager decide quando um anúncio pode aparecer e garante que `continuar` seja chamado exatamente uma vez. |
| `LevelPlayAdsBackend.swift` | O provedor ativo e o ÚNICO arquivo que importa o LevelPlay (`import IronSource`). |
| `UmpConsent.swift` | Consentimento pelo UMP do Google (TCF), lido pelo LevelPlay e pelas redes. |
| `AureaAds.swift` | Liga o manager ao app: `AureaApp`, quando a cena fica ativa. |
| `GoogleAdsBackend.swift` | AdMob direto. Está desligado e fica fora do alvo do Xcode; pode voltar como rede mediada. |

## IDs do iOS

Nunca usar os do Android.

- App Key do LevelPlay: `284ec147d`
- Rewarded do AI Video: `mgykirb9g392nodz`
- Interstitial da exportação: `a26boa5s3ws0el0v`
- Unity Ads (Game ID `800380103`, placements `BP_Rewarded_iOS` e `BP_Interstitial_iOS`): configurados no painel do LevelPlay. O app não usa esses valores.

## Pacotes SPM

Estão no `Aurea.xcodeproj`, com versão exata:

- `LevelPlay-Swift-Package` 9.6.0, produto `UnityMediationSDK`
- `LevelPlay-UnityAds-Adapter-Swift-Package` 5.12.0, produto `UnityAdsAdapter`, que traz o Unity Ads 4.20.1
- `swift-package-manager-google-user-messaging-platform` 3.1.0, produto `GoogleUserMessagingPlatform`

O `-ObjC` em `OTHER_LDFLAGS` é exigido pelo LevelPlay.

## Info.plist

- `SKAdNetworkItems`: ironSource e a lista oficial da Unity.
- `NSAdvertisingAttributionReportEndpoint`.
- `NSAppTransportSecurity`.
- `GADApplicationIdentifier`: o App ID de TESTE, que o UMP exige.

## Fluxos

- **Exportação:** quando o vídeo já está salvo, o `AureaModel` chama `showExportInterstitialIfAvailable {}`. Isso não segura o render nem a exportação.
- **AI Video:** use `preloadRewarded` e `showRewarded(opened:reward:closed:failed:)`.
  - Só `reward` libera o vídeo; ele vem do `didRewardAd` do SDK.
  - `closed(earned)` nunca libera.
  - A tela AI Video está em `app/Ai/` (`AiVideoPanel`, `AureaAiState`, `AiRewardFlow`), porte do Android.
- **Test Suite (DEBUG):** rode com o argumento de launch `-levelplay_test_suite`.
