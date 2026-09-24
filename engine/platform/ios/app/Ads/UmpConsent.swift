// =============================================================================
//  Aurea iOS — o consentimento oficial do Google (UMP), comum a qualquer
//  provedor: grava o consentimento no padrão IAB TCF, que o LevelPlay e as
//  redes mediadas leem. Nada de consentimento presumido: `decide(true)` só
//  quando `canRequestAds` permite. (O mesmo `UmpConsent` do Android.)
// =============================================================================
import UIKit
import UserMessagingPlatform

enum UmpConsent {
    static func request(from vc: UIViewController, decide: @escaping (_ canRequestAds: Bool) -> Void) {
        var answered = false
        let answer: (Bool) -> Void = { ok in
            DispatchQueue.main.async { if !answered { answered = true; decide(ok) } }
        }
        ConsentInformation.shared.requestConsentInfoUpdate(with: RequestParameters()) { error in
            // Sem rede para atualizar: vale o que já estava decidido antes.
            if error != nil { answer(ConsentInformation.shared.canRequestAds); return }
            // O formulário oficial só aparece se a região/usuário exigir.
            DispatchQueue.main.async {
                ConsentForm.loadAndPresentIfRequired(from: vc) { _ in answer(ConsentInformation.shared.canRequestAds) }
            }
        }
        // Sessões anteriores já decidiram: pode seguir em paralelo (recomendação do Google).
        if ConsentInformation.shared.canRequestAds { answer(true) }
    }
}
