// =============================================================================
//  Aurea / platform / ios / app / GoogleLogin.swift
//
//  0.4.1: login com Google pelo Firebase Auth. Par de
//  android/.../conta/GoogleLogin.kt.
//
//  Folha do Google (GoogleSignIn) → ID token do Google → Firebase Auth
//  (signIn(with:)) → ID token do FIREBASE, que vai para o servidor do Aurea
//  (POST /api/auth/firebase, discovery/contas.js). A sessão continua a do Aurea.
//
//  O Firebase é iniciado aqui com os valores do Info.plist (AureaFirebase*),
//  preenchidos pelas build settings AUREA_FIREBASE_* (Firebase.xcconfig.example),
//  sem GoogleService-Info.plist. Sem os valores o botão do Google não aparece.
// =============================================================================
import FirebaseAuth
import FirebaseCore
import GoogleSignIn
import UIKit

enum GoogleLogin {
    /// 0.4.1: o resultado da folha do Google: token do Firebase, cancelado ou erro.
    enum Resultado {
        case token(String)
        case cancelado
        case falhou(String)
    }

    private static let nome: String = "aurea"

    private static func valor(_ chave: String) -> String {
        let bruto: String = (Bundle.main.object(forInfoDictionaryKey: chave) as? String) ?? ""
        let limpo: String = bruto.trimmingCharacters(in: .whitespacesAndNewlines)
        // 0.4.1: variável não expandida conta como vazia.
        return limpo.hasPrefix("$(") ? "" : limpo
    }

    private static var projeto: String { valor("AureaFirebaseProjectID") }
    private static var appID: String { valor("AureaFirebaseAppID") }
    private static var chaveAPI: String { valor("AureaFirebaseAPIKey") }
    private static var clienteID: String { valor("AureaFirebaseClientID") }
    private static var remetente: String { valor("AureaFirebaseSenderID") }

    /// 0.4.1: sem configuração do Firebase o botão do Google não aparece.
    static var disponivel: Bool {
        return !projeto.isEmpty && !appID.isEmpty && !chaveAPI.isEmpty && !clienteID.isEmpty && !remetente.isEmpty
    }

    private static func app() -> FirebaseApp? {
        if let existente: FirebaseApp = FirebaseApp.app(name: nome) { return existente }
        let opcoes: FirebaseOptions = FirebaseOptions(googleAppID: appID, gcmSenderID: remetente)
        opcoes.apiKey = chaveAPI
        opcoes.projectID = projeto
        opcoes.clientID = clienteID
        FirebaseApp.configure(name: nome, options: opcoes)
        return FirebaseApp.app(name: nome)
    }

    /// 0.4.1: a volta da folha do Google pelo esquema de URL (AureaApp.onOpenURL).
    @MainActor
    static func tratar(_ url: URL) -> Bool {
        return GIDSignIn.sharedInstance.handle(url)
    }

    @MainActor
    private static func controladorDoTopo() -> UIViewController? {
        let cenas: [UIWindowScene] = UIApplication.shared.connectedScenes.compactMap { $0 as? UIWindowScene }
        let janelas: [UIWindow] = cenas.flatMap { $0.windows }
        var topo: UIViewController? = (janelas.first { $0.isKeyWindow } ?? janelas.first)?.rootViewController
        while let apresentado: UIViewController = topo?.presentedViewController { topo = apresentado }
        return topo
    }

    @MainActor
    static func entrar() async -> Resultado {
        guard disponivel else { return .falhou("config") }
        guard let firebase: FirebaseApp = app() else { return .falhou("firebase") }
        guard let tela: UIViewController = controladorDoTopo() else { return .falhou("tela") }
        GIDSignIn.sharedInstance.configuration = GIDConfiguration(clientID: clienteID)
        let google: GIDSignInResult
        do {
            google = try await GIDSignIn.sharedInstance.signIn(withPresenting: tela)
        } catch {
            if let e = error as? GIDSignInError, e.code == .canceled { return .cancelado }
            return .falhou(String(describing: type(of: error)))
        }
        guard let idGoogle: String = google.user.idToken?.tokenString else { return .falhou("credencial") }
        let credencial: AuthCredential = GoogleAuthProvider.credential(withIDToken: idGoogle,
                                                                       accessToken: google.user.accessToken.tokenString)
        let auth: Auth = Auth.auth(app: firebase)
        do {
            let dados: AuthDataResult = try await auth.signIn(with: credencial)
            let token: String = try await dados.user.getIDTokenResult(forcingRefresh: true).token
            // 0.4.1: a sessão que vale é a do Aurea; o Firebase só provou o e-mail.
            try? auth.signOut()
            GIDSignIn.sharedInstance.signOut()
            return token.isEmpty ? .falhou("token") : .token(token)
        } catch {
            try? auth.signOut()
            GIDSignIn.sharedInstance.signOut()
            return .falhou(String(describing: type(of: error)))
        }
    }
}
