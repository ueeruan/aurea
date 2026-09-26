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
    @State private var pageWidth = UIScreen.main.bounds.width

    var body: some View {
        ZStack {
            AureaColors.background.ignoresSafeArea()

            switch model.screen {
            case .home:
                HomeView()
                    .transition(.offset(x: -pageWidth / 3)).zIndex(0)
            case .editor:
                EditorView()
                    .transition(.offset(x: pageWidth)).zIndex(1)
            }

            if model.importingMedia {
                Color(hex: 0x17191D).opacity(221.0 / 255.0).ignoresSafeArea().contentShape(Rectangle()).onTapGesture {}
                VStack(spacing: 12) {
                    AureaActivityIndicator()
                    Text(model.operationMessage).font(.aurea(size: 13)).foregroundStyle(AureaColors.text)
                }
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
                        .font(.aurea(size: 13))
                        .foregroundStyle(AureaColors.text)
                        .padding(.horizontal, 16)
                        .padding(.vertical, 10)
                        .background(AureaColors.surfaceHigh, in: RoundedRectangle(cornerRadius: 12))
                        .padding(.horizontal, 24)
                        .padding(.bottom, 96)
                        .onTapGesture { model.toast = nil }
                }
                .transition(.opacity)
            }
        }
        .overlay {
            if let request = model.liveNoticePopup { LiveNoticePopup(request: request) }
            if model.showProjectSettings { ProjectSettingsPanel(onDismiss: { model.showProjectSettings = false }) }
            if let request = model.actionSheet { AureaActionSheet(title: request.title, actions: request.actions) { model.actionSheet = nil } }
            if let request = model.colorSheet { ColorPickerSheet(request: request) { model.colorSheet = nil; request.onDone() }.id(request.id) }
            if let request = model.expressionSheet { ExpressionSheet(request: request) { model.expressionSheet = nil }.id(request.id) }
            if let request = model.text3DFontSheet { T3DFontSheet(request: request) { model.text3DFontSheet = nil }.id(request.id) }
            if let request = model.numericKeypad { NumericKeypadSheet(request: request) { model.numericKeypad = nil }.id(request.id) }
            if let request = model.namePrompt { AureaNamePrompt(title: request.title, initial: request.initial, onConfirm: request.onConfirm, onDismiss: { model.namePrompt = nil }).id(request.id) }
            if let request = model.presetDialog { PresetDialog(request: request) { model.presetDialog = nil }.id(request.id) }
        }
        .background {
            GeometryReader { geometry in
                Color.clear.onAppear { pageWidth = geometry.size.width }
                    .onChange(of: geometry.size.width) { pageWidth = $0 }
            }
        }
        .animation(.timingCurve(0.4, 0, 0.2, 1, duration: 0.5), value: model.screen)
        .overlay(alignment: .bottom) { PerformanceTestBadge().padding(.bottom, 6) }
        .onChange(of: model.toast) { value in
            guard value != nil else { return }
            // O aviso some sozinho, como o do Android. Tocar nele também fecha.
            DispatchQueue.main.asyncAfter(deadline: .now() + 3.2) {
                if model.toast == value { model.toast = nil }
            }
        }
    }
}

/// ChromeKit.kt's twelve spoke indicator, one discrete revolution per second.
struct AureaActivityIndicator: View {
    var size: CGFloat = 22
    var body: some View {
        SwiftUI.TimelineView(.animation(minimumInterval: 1.0 / 12.0)) { timeline in
            Canvas { context, bounds in
                let step = Int(timeline.date.timeIntervalSinceReferenceDate * 12) % 12
                let radius = min(bounds.width, bounds.height) / 2, width = radius * 0.18
                for index in 0..<12 {
                    let angle = Double((index + step) * 30 - 90) * .pi / 180
                    let x = CGFloat(cos(angle)), y = CGFloat(sin(angle))
                    var line = Path()
                    line.move(to: CGPoint(x: bounds.width / 2 + x * radius * 0.5, y: bounds.height / 2 + y * radius * 0.5))
                    line.addLine(to: CGPoint(x: bounds.width / 2 + x * (radius - width / 2), y: bounds.height / 2 + y * (radius - width / 2)))
                    context.stroke(line, with: .color(AureaColors.text.opacity(0.25 + 0.75 * Double(index) / 11)), style: StrokeStyle(lineWidth: width, lineCap: .round))
                }
            }.frame(width: size, height: size)
        }
    }
}

#Preview {
    { let m = AureaModel(); return ContentView().environmentObject(m).environmentObject(m.playheadClock) }()
}
