// =============================================================================
//  Aurea iOS — a ligação do AureaAdsManager com o app (o `AureaAds.kt` do
//  Android): cria o backend LevelPlay com os IDs do iOS e inicia depois que a
//  janela existe. Nenhuma tela chama o SDK.
// =============================================================================
import UIKit

enum AureaAds {
    static func start() {
        // Depois do primeiro frame: a janela já é a key window.
        DispatchQueue.main.async {
            guard let root = rootViewController() else { return }
            #if DEBUG
            let debug = true
            #else
            let debug = false
            #endif
            // DEBUG: rodar com o argumento `-levelplay_test_suite` abre a Test Suite oficial.
            let backend = LevelPlayAdsBackend(
                appKey: AdsIds.levelPlayAppKey, debug: debug,
                openTestSuite: debug && ProcessInfo.processInfo.arguments.contains("-levelplay_test_suite"))
            AureaAdsManager.shared.initialize(from: root, backend: backend,
                                              frequency: AdsFrequencyController(), ids: .current)
        }
    }

    static func rootViewController() -> UIViewController? {
        let windows = UIApplication.shared.connectedScenes
            .compactMap { $0 as? UIWindowScene }
            .flatMap { $0.windows }
        return (windows.first { $0.isKeyWindow } ?? windows.first)?.rootViewController
    }
}
