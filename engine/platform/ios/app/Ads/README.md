# Anúncios no iOS (AdMob) — preparado, ainda não ligado

Mesma arquitetura do Android (`android/app/src/main/java/com/aurea/aurea/ads/`):

- `AdsPolicy` e `AdsFrequencyController` guardam os números de frequência e a persistência (UserDefaults, domínio `aurea.ads`).
- `AureaAdsManager` decide quando um anúncio pode aparecer e garante que `continuar` seja chamado exatamente uma vez.
- `GoogleAdsBackend` é o único lugar que importa o SDK (GoogleMobileAds 12.x e UMP).

**Não foi compilado nem testado**: não há Mac neste ambiente. Os arquivos também não entraram no `Aurea.xcodeproj` nem no `Info.plist`, porque os dois têm alterações pendentes de outra sessão.

## Para ligar (num Mac)

1. Adicionar os pacotes pelo Swift Package Manager:
   - `https://github.com/googleads/swift-package-manager-google-mobile-ads`, que já traz o UMP.
2. Colocar `Ads/*.swift` no alvo do app.
3. No `Info.plist`:
   - `GADApplicationIdentifier`: em DEBUG, o App ID de teste do Google, `ca-app-pub-3940256099942544~1458002511`; em release, o real.
   - `SKAdNetworkItems`: a lista do Google.
   - `AureaAdAppOpenUnit` e `AureaAdInterstitialUnit`: os IDs reais, usados só em release. Vazio = aquele formato não pede anúncio.
4. No app:
   - Na abertura:
     - `AureaAdsManager.shared.initialize(from: rootVC, backend: GoogleAdsBackend(), frequency: AdsFrequencyController(), ids: .current)`.
     - Quando o motor ficar pronto: `onAppLoaded(host:working:)`.
   - Lifecycle da cena:
     - `sceneDidEnterBackground` → `onBackground()`.
     - `sceneWillEnterForeground` → `onForeground(host:working:)`.
     - Chamar `attach(_:)` e `detach(_:)` com o view controller visível.
   - Exportação: ao terminar, com o vídeo já salvo, chamar `showExportInterstitialIfAvailable { mostrarResultado() }`.
