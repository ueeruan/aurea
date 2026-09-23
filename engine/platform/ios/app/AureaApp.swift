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
    @Environment(\.scenePhase) private var scenePhase

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(model)
                .preferredColorScheme(.dark)   // o Aurea é escuro em todas as telas
                .onAppear { model.start() }
        }
        .onChange(of: scenePhase) { phase in
            // O motor tem uma thread de render e decoders de hardware: em
            // segundo plano ele PAUSA e devolve os decoders ao sistema. O
            // projeto fica intacto (é o `Engine::suspend`).
            switch phase {
            case .active: model.enterForeground()
            case .background, .inactive: model.enterBackground()
            @unknown default: break
            }
        }
    }
}
