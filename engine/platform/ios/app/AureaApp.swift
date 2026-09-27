// =============================================================================
//  Aurea / platform / ios / app / AureaApp.swift
//
//  O ponto de entrada. É o equivalente do `MainActivity.kt`: uma sessão
//  (@StateObject) para o app inteiro, o motor subindo quando a janela aparece e
//  indo para trás quando ela sai — o mesmo ciclo de vida do Android
//  (`onEnterForeground` / `onEnterBackground` no Activity).
// =============================================================================
import SwiftUI

@main
struct AureaApp: App {
    @StateObject private var model = AureaModel()
    /// A conta obrigatória (Conta.swift): sem ela a UI inteira é a ContaView.
    @StateObject private var conta = ContaModel()
    @Environment(\.scenePhase) private var scenePhase

    init() {
        // Antes de qualquer tela: a pilha de um crash precisa ser gravada mesmo
        // que ele aconteça no primeiro quadro (CrashReporter.swift).
        CrashReporter.shared.instalar()
    }

    var body: some Scene {
        WindowGroup {
            ContentView()
                .id(model.themeId)
                .environmentObject(model)
                .environmentObject(conta)
                .environmentObject(model.playheadClock)
                .environment(\.layoutDirection, model.language.resolved == .ar ? .rightToLeft : .leftToRight)
                .preferredColorScheme(.dark)   // o Aurea é escuro em todas as telas
                .onAppear {
                    model.start()
                    // Número de cadastrados + revalidação (com rede; offline, segue dentro).
                    conta.aoAbrir()
                    // Relatórios de crash só saem com conta: o cadastro avisa do envio antes.
                    if conta.logado { CrashReporter.shared.enviarPendentes(sessao: conta.sessao()) }
#if DEBUG
                    model.prepareParityCapture()
#endif
                }
                .onChange(of: conta.logado) { dentro in
                    CrashReporter.shared.enviarPendentes(sessao: dentro ? conta.sessao() : nil)
                }
        }
        .onChange(of: scenePhase) { phase in
            if phase == .active { AureaAds.start() }   // anúncios: uma vez (o manager ignora as repetidas)
            // O motor tem uma thread de render e decoders de hardware: em
            // segundo plano ele PAUSA e devolve os decoders ao sistema. O
            // projeto fica intacto (é o `Engine::suspend`).
            switch phase {
            case .active:
                model.enterForeground()
                // "Na frente": se o app morrer sem ir ao segundo plano, a próxima abertura sabe.
                CrashReporter.shared.primeiroPlano(true, etapa: "screen=\(model.screen)")
                conta.revalidarSeVencido()
            case .background, .inactive:
                model.enterBackground()
                if phase == .background { CrashReporter.shared.primeiroPlano(false, etapa: "") }
            @unknown default: break
            }
        }
    }
}
