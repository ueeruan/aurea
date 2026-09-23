// =============================================================================
//  Aurea / platform / ios / app / ContentView.swift
//
//  A raiz: Home ou Editor, mais o aviso flutuante (o "toast" que o Android usa
//  para dizer o que aconteceu sem interromper). Nada de navegação de sistema
//  entre as duas: o editor é uma TELA, não uma pilha — voltar salva o projeto e
//  volta para a Home, como no Android.
// =============================================================================
import SwiftUI

struct ContentView: View {
    @EnvironmentObject private var model: AureaModel

    var body: some View {
        ZStack {
            AureaColors.background.ignoresSafeArea()

            switch model.screen {
            case .home:
                HomeView()
                    .transition(.opacity)
            case .editor:
                EditorView()
                    .transition(.opacity)
            }

            // O motor não subiu: a UI DIZ (nunca uma tela que parece pronta e
            // não desenha nada). Sem GPU o editor continua abrindo — timeline,
            // comandos e export — e é isso que a mensagem explica.
            if !model.started, let failure = model.startError {
                VStack {
                    Spacer()
                    Text(failure)
                        .font(AureaType.tiny)
                        .foregroundStyle(AureaColors.warning)
                        .multilineTextAlignment(.center)
                        .padding(.horizontal, 16)
                        .padding(.vertical, 10)
                        .background(AureaColors.surface.opacity(0.96), in: RoundedRectangle(cornerRadius: 10))
                        .overlay(RoundedRectangle(cornerRadius: 10).stroke(AureaColors.warning.opacity(0.4), lineWidth: 1))
                        .padding(.horizontal, 24)
                        .padding(.bottom, 120)
                }
            }

            if let toast = model.toast {
                VStack {
                    Spacer()
                    Text(toast)
                        .font(AureaType.label)
                        .foregroundStyle(AureaColors.text)
                        .padding(.horizontal, 16)
                        .padding(.vertical, 10)
                        .background(AureaColors.chip.opacity(0.96), in: Capsule())
                        .overlay(Capsule().stroke(AureaColors.border, lineWidth: 1))
                        .padding(.bottom, 90)
                        .onTapGesture { model.toast = nil }
                }
                .transition(.opacity)
            }
        }
        .animation(.easeInOut(duration: 0.18), value: model.screen)
        .onChange(of: model.toast) { value in
            guard value != nil else { return }
            // O aviso some sozinho, como o do Android. Tocar nele também fecha.
            DispatchQueue.main.asyncAfter(deadline: .now() + 3.2) {
                if model.toast == value { model.toast = nil }
            }
        }
    }
}

#Preview {
    ContentView().environmentObject(AureaModel())
}
