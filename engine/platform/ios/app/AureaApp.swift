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
    /// A conta opcional, compartilhada com a Home.
    @StateObject private var conta = ContaModel()
    @Environment(\.scenePhase) private var scenePhase

    init() {
        // Antes de qualquer tela: a pilha de um crash precisa ser gravada mesmo
        // que ele aconteça no primeiro quadro (CrashReporter.swift).
        CrashReporter.shared.instalar()
    }

    var body: some Scene {
        WindowGroup {
            LanguageWelcome { CreatorWelcome { ContentView() } }
                .id(model.themeId)
                .environmentObject(model)
                .environmentObject(conta)
                .environmentObject(model.playheadClock)
                // Idioma do APP (não o do sistema): RTL em árabe e o locale com
                // algarismos ocidentais (DesignSystem.swift, `uiLocale`). `model`
                // é @StateObject e `language` é @Published: trocar nos Ajustes
                // refaz este body e o ambiente desce na hora — folhas e capas
                // apresentadas abaixo herdam. O que é dado no eixo X fica LTR
                // por `keepLtr()` (timeline, palco, transporte, curvas).
                .environment(\.layoutDirection, model.language.layoutDirection)
                .environment(\.locale, model.language.uiLocale)
                // O editor é escuro; a Home do Omega é clara (D6): barra de status
                // escura e controles do sistema claros só enquanto ela está na tela.
                .preferredColorScheme(model.screen == .home && AureaColors.lightIsLight ? .light : .dark)
                .onAppear {
                    model.start()
                    // 0.3.2: config remota assinada (atualização obrigatória, política do AureaHyper, aviso).
                    AppConfigStore.shared.refresh(force: true)
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
                // No máximo a cada 30 min; o prazo do Hyper "free" é recalculado.
                AppConfigStore.shared.refresh()
                HyperEntitlements.shared.recheck()
            case .background:
                model.enterBackground()
                CrashReporter.shared.primeiroPlano(false, etapa: "")
            case .inactive:
                model.enterInactive()
            @unknown default: break
            }
        }
    }
}

private struct LanguageWelcome<Content: View>: View {
    @AppStorage("aurea.languageChosen") private var chosen = false
    @EnvironmentObject private var model: AureaModel
    @ViewBuilder var content: () -> Content
    private var testing: Bool {
#if DEBUG
        return ProcessInfo.processInfo.environment["AUREA_PARITY_SCENE"] != nil
#else
        return false
#endif
    }
    var body: some View {
        if chosen || testing { content() }
        else {
            ScrollView {
                VStack(alignment: .leading, spacing: 12) {
                    Text("AUREA").font(.title).bold()
                    Text("Choose your language · Escolha seu idioma").font(.title2)
                    ForEach(AureaLanguage.allCases) { language in
                        Button {
                            model.language = language
                            chosen = true
                        } label: {
                            Text(language.label).frame(maxWidth: .infinity, minHeight: 48)
                        }.buttonStyle(.bordered).accessibilityIdentifier("welcome.language.\(language.rawValue)")
                    }
                }.padding(24)
            }.background(AureaColors.background)
        }
    }
}

/// O bonequinho do Aurea em pixel art mandando um coração. Desenho próprio em
/// grade — a MESMA grade e as mesmas cores do Android (home/PrivacyMascot.kt).
private struct PrivacyMascot: View {
    static let figure = [
        "...KKKKKK...",
        ".KKYYYYYYKK.",
        ".KYYYYYYYYK.",
        "KYYYYYYYYYYK",
        "KYYKYYYYKYYK",
        "KYYKYYYYKYYK",
        "KYPYYYYYYPYK",
        "KYYYKYYKYYYK",
        "KYYYYKKYYYYK",
        ".KYYYYYYYYK.",
        ".KKYYYYYYKK.",
        "...KKKKKK...",
    ]
    static let heart = [".RR.RR.", "RLRRRRR", "RRRRRRR", ".RRRRR.", "..RRR..", "...R..."]

    static func color(_ c: Character) -> Color? {
        switch c {
        case "K": return Color(red: 0x3B / 255, green: 0x2A / 255, blue: 0x1A / 255)
        case "Y": return Color(red: 0xF5 / 255, green: 0xC5 / 255, blue: 0x42 / 255)
        case "P": return Color(red: 1, green: 0x8F / 255, blue: 0xA3 / 255)
        case "R": return Color(red: 1, green: 0x3B / 255, blue: 0x5C / 255)
        case "L": return Color(red: 1, green: 0xD1 / 255, blue: 0xDC / 255)
        default: return nil
        }
    }

    var body: some View {
        // SwiftUI.: o app tem a própria TimelineView (a timeline do editor).
        SwiftUI.TimelineView(.animation) { timeline in
            let t = timeline.date.timeIntervalSinceReferenceDate
            Canvas { context, size in
                let px = size.height / 16
                let top = size.height - 12 * px
                Self.draw(&context, Self.figure, x0: 0, y0: top, cell: px, alpha: 1)
                // O coração sai do bonequinho: sobe, pulsa e some um pouco no alto.
                let rise = t.truncatingRemainder(dividingBy: 1.6) / 1.6
                let beat = 1 + 0.18 * (0.5 - 0.5 * cos(t * .pi / 0.52))
                Self.draw(&context, Self.heart, x0: 13 * px, y0: top + px - rise * 3 * px, cell: px * beat, alpha: 1 - rise * 0.35)
            }
        }
        .frame(width: 132, height: 96)
        .accessibilityElement()
        .accessibilityLabel(AureaText.t("privacy_mascot_desc"))
    }

    private static func draw(_ context: inout GraphicsContext, _ rows: [String], x0: CGFloat, y0: CGFloat,
                             cell: CGFloat, alpha: Double) {
        for (r, line) in rows.enumerated() {
            for (c, ch) in line.enumerated() {
                guard let color = Self.color(ch) else { continue }
                let rect = CGRect(x: x0 + CGFloat(c) * cell, y: y0 + CGFloat(r) * cell, width: cell + 0.5, height: cell + 0.5)
                context.fill(Path(rect), with: .color(color.opacity(alpha)))
            }
        }
    }
}

private struct CreatorWelcome<Content: View>: View {
    /// Versão do texto de privacidade/termos aceito (o build em que mudou).
    static var currentConsent: Int { 2181 }
    @AppStorage("consentVersion") private var consentVersion = 0
    @AppStorage("creatorWelcomeOpened") private var opened = 0
    @Environment(\.openURL) private var openURL
    @State private var failed = false
    @ViewBuilder var content: () -> Content
    private var testing: Bool {
#if DEBUG
        return ProcessInfo.processInfo.environment["AUREA_PARITY_SCENE"] != nil
#else
        return false
#endif
    }
    var body: some View {
        // 0.1.7 BETA 3: privacidade + termos com o bonequinho (par do Android,
        // MainActivity.CreatorWelcome), uma vez a todos.
        if consentVersion < Self.currentConsent && !testing {
            VStack(alignment: .leading, spacing: 16) {
                ScrollView {
                    VStack(alignment: .leading, spacing: 12) {
                        PrivacyMascot().frame(maxWidth: .infinity)
                        Text(AureaText.t("privacy_title")).font(.title2).bold()
                        Text(AureaText.t("privacy_body")).font(.body)
                        Text(AureaText.t("consent_title")).font(.title3).bold().padding(.top, 12)
                        Text(AureaText.t("consent_body")).font(.body)
                        Text(AureaText.t("terms_liability")).font(.body)
                    }.frame(maxWidth: .infinity, alignment: .leading)
                }
                Button(AureaText.t("consent_accept")) { consentVersion = Self.currentConsent }
                    .buttonStyle(.borderedProminent).frame(maxWidth: .infinity, minHeight: 44)
                    .accessibilityIdentifier("welcome.consent.accept")
            }.padding(24).background(Color.black)
        }
        else if opened == 3 || testing { content() }
        else {
            VStack(alignment: .leading, spacing: 20) {
                Text(AureaText.t("social_title")).font(.title)
                Text(AureaText.t("social_body"))
                profile("TikTok @ruanzitwo", "https://www.tiktok.com/@ruanzitwo", bit: 1)
                profile("Instagram @ofruanzitwo", "https://www.instagram.com/ofruanzitwo/", bit: 2)
                if failed { Text(AureaText.t("social_open_error")).foregroundStyle(.red) }
            }.padding(24).frame(maxWidth: .infinity, maxHeight: .infinity).background(Color.black)
        }
    }
    private func profile(_ title: String, _ address: String, bit: Int) -> some View {
        Button(title) {
            guard let url = URL(string: address) else { return }
            openURL(url) { accepted in
                if accepted { opened |= bit; failed = false } else { failed = true }
            }
        }.buttonStyle(.borderedProminent).frame(minHeight: 44).disabled(opened & bit != 0)
    }
}
