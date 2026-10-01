// =============================================================================
//  Aurea / platform / ios / app / Conta.swift
//
//  A conta obrigatória — par de android/.../conta/ContaLogica.kt, ContaApi.kt e
//  ContaViewModel.kt. As regras são as MESMAS nas duas plataformas:
//
//   · sem sessão guardada, o app inteiro é a tela de conta (ContaView.swift);
//   · com sessão guardada, o app abre direto, mesmo offline;
//   · revalidação com rede: só um 401 derruba; sem rede ou servidor fora, segue;
//   · no aparelho ficam SÓ o token de sessão e o e-mail, no Keychain
//     (AfterFirstUnlockThisDeviceOnly: não vai para backup nem para outro
//     aparelho). A senha nunca é guardada nem registrada.
// =============================================================================
import Foundation
import Security

enum ContaKeychain {
    private static let servico: String = "com.aurea.conta"

    private static func consulta(_ conta: String) -> [String: Any] {
        return [kSecClass as String: kSecClassGenericPassword,
                kSecAttrService as String: servico,
                kSecAttrAccount as String: conta]
    }

    static func ler(_ conta: String) -> String? {
        var q: [String: Any] = consulta(conta)
        q[kSecReturnData as String] = true
        q[kSecMatchLimit as String] = kSecMatchLimitOne
        var resultado: CFTypeRef?
        let status: OSStatus = SecItemCopyMatching(q as CFDictionary, &resultado)
        guard status == errSecSuccess, let dados = resultado as? Data else { return nil }
        guard let texto: String = String(data: dados, encoding: .utf8), !texto.isEmpty else { return nil }
        return texto
    }

    @discardableResult
    static func gravar(_ conta: String, _ valor: String) -> Bool {
        let atributos: [String: Any] = [kSecValueData as String: Data(valor.utf8),
                                        kSecAttrAccessible as String: kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly]
        let status: OSStatus = SecItemUpdate(consulta(conta) as CFDictionary, atributos as CFDictionary)
        if status == errSecItemNotFound {
            let novo: [String: Any] = consulta(conta).merging(atributos) { _, nova in nova }
            return SecItemAdd(novo as CFDictionary, nil) == errSecSuccess
        }
        return status == errSecSuccess
    }

    static func apagar(_ conta: String) {
        let _: OSStatus = SecItemDelete(consulta(conta) as CFDictionary)
    }
}

struct ContaSessao {
    let token: String
    let email: String
}

enum ContaErro: String {
    case email, senha, credenciais, emUso, limite, rede, servico, expirada

    var chave: String {
        switch self {
        case .email: return "conta_erro_email"
        case .senha: return "conta_erro_senha"
        case .credenciais: return "conta_erro_credenciais"
        case .emUso: return "conta_erro_em_uso"
        case .limite: return "conta_erro_limite"
        case .rede: return "conta_erro_rede"
        case .servico: return "conta_erro_servico"
        case .expirada: return "conta_erro_expirada"
        }
    }
}

enum ContaLogica {
    static let senhaMin: Int = 8
    static let senhaMax: Int = 128

    /// O mesmo `normalizarEmail` do Worker e do Android: trim + minúsculas + formato.
    static func normalizarEmail(_ valor: String) -> String? {
        let e: String = valor.trimmingCharacters(in: .whitespacesAndNewlines).lowercased()
        guard e.utf16.count >= 6, e.utf16.count <= 254 else { return nil }
        let partes: [Substring] = e.split(separator: "@", omittingEmptySubsequences: false)
        guard partes.count == 2 else { return nil }
        let local: String = String(partes[0])
        let dominio: String = String(partes[1])
        guard !local.isEmpty, local.utf16.count <= 64, !local.hasPrefix("."), !local.hasSuffix("."),
              !local.contains("..") else { return nil }
        guard local.range(of: "^[a-z0-9.!#$%&'*+/=?^_`{|}~-]+$", options: .regularExpression) != nil else { return nil }
        let rotulos: [Substring] = dominio.split(separator: ".", omittingEmptySubsequences: false)
        guard rotulos.count >= 2 else { return nil }
        for rotulo in rotulos {
            guard String(rotulo).range(of: "^[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?$", options: .regularExpression) != nil else { return nil }
        }
        guard let ultimo: Substring = rotulos.last,
              String(ultimo).range(of: "^[a-z]{2,63}$", options: .regularExpression) != nil else { return nil }
        return e
    }

    /// Pontos de código, como o Worker (um emoji é UM caractere).
    static func senhaValida(_ senha: String) -> Bool {
        let n: Int = senha.unicodeScalars.count
        return n >= senhaMin && n <= senhaMax
    }

    static func validar(email: String, senha: String) -> ContaErro? {
        if normalizarEmail(email) == nil { return .email }
        if !senhaValida(senha) { return .senha }
        return nil
    }

    static func erroDoServidor(status: Int, codigo: String?) -> ContaErro {
        if status == 429 || codigo == "muitas_tentativas" { return .limite }
        if codigo == "email_em_uso" || status == 409 { return .emUso }
        if codigo == "email_invalido" { return .email }
        if codigo == "senha_curta" || codigo == "senha_longa" || codigo == "senha_invalida" { return .senha }
        if status == 401 { return .credenciais }
        return .servico
    }

    static func precisaRevalidar(ultima: Date?, agora: Date) -> Bool {
        guard let ultima: Date = ultima else { return true }
        return agora.timeIntervalSince(ultima) >= 6 * 3600
    }
}

struct ContaResposta {
    let status: Int
    let corpo: [String: Any]
    var ok: Bool { return (200...299).contains(status) }
    var codigo: String? { return corpo["error"] as? String }
}

/// As rotas de conta do Worker (discovery/contas.js). Só HTTPS, só o endereço fixo.
final class AureaPrivateSessionDelegate: NSObject, URLSessionDownloadDelegate, @unchecked Sendable {
    func urlSession(_ session: URLSession, task: URLSessionTask,
                    willPerformHTTPRedirection response: HTTPURLResponse, newRequest request: URLRequest,
                    completionHandler: @escaping (URLRequest?) -> Void) {
        // Credentials and account payloads never follow a redirected endpoint.
        completionHandler(nil)
    }
    func urlSession(_ session: URLSession, downloadTask: URLSessionDownloadTask, didFinishDownloadingTo location: URL) {}
    func urlSession(_ session: URLSession, downloadTask: URLSessionDownloadTask,
                    didWriteData bytesWritten: Int64, totalBytesWritten: Int64, totalBytesExpectedToWrite: Int64) {
        if max(totalBytesWritten, totalBytesExpectedToWrite) > 300 * 1024 * 1024 { downloadTask.cancel() }
    }
}

enum ContaAPI {
    static let base: String = "https://aurea-ai-discovery.aureaapp.workers.dev"
    static let session: URLSession = {
        let configuration = URLSessionConfiguration.ephemeral
        configuration.urlCache = nil
        configuration.httpCookieStorage = nil
        configuration.httpShouldSetCookies = false
        configuration.requestCachePolicy = .reloadIgnoringLocalCacheData
        return URLSession(configuration: configuration, delegate: AureaPrivateSessionDelegate(), delegateQueue: nil)
    }()

    static func boundedData(for request: URLRequest, limit: Int) async throws -> (Data, URLResponse) {
        let (stream, response) = try await session.bytes(for: request)
        guard response.expectedContentLength <= Int64(limit) else { throw URLError(.dataLengthExceedsMaximum) }
        var data = Data()
        for try await byte in stream {
            guard data.count < limit else { throw URLError(.dataLengthExceedsMaximum) }
            data.append(byte)
        }
        return (data, response)
    }

    static func chamar(_ caminho: String, metodo: String, corpo: [String: Any]?, token: String?) async -> ContaResposta {
        guard let url: URL = URL(string: base + caminho) else { return ContaResposta(status: 0, corpo: [:]) }
        var pedido: URLRequest = URLRequest(url: url, cachePolicy: .reloadIgnoringLocalCacheData, timeoutInterval: 20)
        pedido.httpMethod = metodo
        pedido.setValue("application/json", forHTTPHeaderField: "Accept")
        if let token: String = token { pedido.setValue("Bearer " + token, forHTTPHeaderField: "Authorization") }
        if let corpo: [String: Any] = corpo {
            pedido.httpBody = try? JSONSerialization.data(withJSONObject: corpo)
            pedido.setValue("application/json; charset=utf-8", forHTTPHeaderField: "Content-Type")
        }
        do {
            let resultado: (Data, URLResponse) = try await boundedData(for: pedido, limit: 16_384)
            let status: Int = (resultado.1 as? HTTPURLResponse)?.statusCode ?? 0
            let dados: Data = resultado.0.count > 16_384 ? Data() : resultado.0
            let objeto: Any? = try? JSONSerialization.jsonObject(with: dados)
            let json: [String: Any] = (objeto as? [String: Any]) ?? [:]
            return ContaResposta(status: status, corpo: json)
        } catch {
            return ContaResposta(status: 0, corpo: [:])
        }
    }
}

/// O estado da conta que a UI observa (o ContaViewModel do Android).
@MainActor
final class ContaModel: ObservableObject {
    @Published private(set) var email: String?
    @Published private(set) var usuarios: Int?
    @Published private(set) var ocupado: Bool = false
    @Published var erro: ContaErro?
    /// false = cadastro (a primeira abertura), true = entrar.
    @Published var entrando: Bool = false
    private var ultimaRevalidacao: Date?
    private var revogando = false

    private static let chaveUsuarios: String = "aurea.conta.usuarios"
    private static let chaveInstalada: String = "aurea.conta.instalada"

    var logado: Bool { return email != nil }

    init() {
        let padroes: UserDefaults = UserDefaults.standard
        // O Keychain do iOS sobrevive à desinstalação; os dados do Android não.
        // Para as duas plataformas agirem igual, instalação nova pede login.
        if !padroes.bool(forKey: ContaModel.chaveInstalada) {
            ContaKeychain.apagar("token")
            ContaKeychain.apagar("email")
            padroes.set(true, forKey: ContaModel.chaveInstalada)
        }
        let guardada: ContaSessao? = ContaModel.lerSessao()
        email = guardada?.email
        usuarios = padroes.object(forKey: ContaModel.chaveUsuarios) as? Int
        #if DEBUG
        // Só a captura de paridade da CI (build Debug do simulador) pula a conta.
        if guardada == nil, ProcessInfo.processInfo.environment["AUREA_PARITY_SCENE"] != nil { email = "ci@aurea.test" }
        #endif
    }

    static func lerSessao() -> ContaSessao? {
        guard let token: String = ContaKeychain.ler("token"), let email: String = ContaKeychain.ler("email") else { return nil }
        return ContaSessao(token: token, email: email)
    }

    func sessao() -> ContaSessao? { return ContaModel.lerSessao() }

    /// Na abertura: número de cadastrados e revalidação da sessão.
    func aoAbrir() {
        Task { await atualizarUsuarios() }
        revalidarSeVencido()
    }

    func atualizarUsuarios() async {
        let r: ContaResposta = await ContaAPI.chamar("/api/stats/users", metodo: "GET", corpo: nil, token: nil)
        if r.ok, let n: Int = (r.corpo["count"] as? NSNumber)?.intValue, n >= 0 { guardarUsuarios(n) }
    }

    private func guardarUsuarios(_ n: Int) {
        usuarios = n
        UserDefaults.standard.set(n, forKey: ContaModel.chaveUsuarios)
    }

    /// No máximo a cada 6 h. Sem rede, quem já entrou continua dentro; só 401 derruba.
    func revalidarSeVencido() {
        revogarPendentes()
        guard let guardada: ContaSessao = sessao() else { return }
        let agora: Date = Date()
        guard ContaLogica.precisaRevalidar(ultima: ultimaRevalidacao, agora: agora) else { return }
        ultimaRevalidacao = agora
        Task { @MainActor in
            let r: ContaResposta = await ContaAPI.chamar("/api/auth/session", metodo: "GET", corpo: nil, token: guardada.token)
            guard sessao()?.token == guardada.token else { return }
            if r.status == 401 {
                apagarSessao()
                email = nil
                entrando = true
                erro = .expirada
            } else if !r.ok {
                ultimaRevalidacao = nil   // tenta de novo na próxima volta ao app
            }
        }
    }

    /// Cadastro ou entrada, conforme `entrando`. A senha só vive nesta chamada.
    func enviar(email digitado: String, senha: String) {
        guard !ocupado else { return }
        if let invalido: ContaErro = ContaLogica.validar(email: digitado, senha: senha) { erro = invalido; return }
        guard let email: String = ContaLogica.normalizarEmail(digitado) else { return }
        let caminho: String = entrando ? "/api/auth/login" : "/api/auth/signup"
        ocupado = true
        erro = nil
        Task { @MainActor in
            let r: ContaResposta = await ContaAPI.chamar(caminho, metodo: "POST",
                                                         corpo: ["email": email, "password": senha], token: nil)
            ocupado = false
            if r.status == 0 { erro = .rede; return }
            if !r.ok {
                let e: ContaErro = ContaLogica.erroDoServidor(status: r.status, codigo: r.codigo)
                erro = e
                if e == .emUso { entrando = true }
                return
            }
            let token: String = (r.corpo["token"] as? String) ?? ""
            let confirmado: String = (r.corpo["email"] as? String) ?? email
            guard token.count == 43, ContaKeychain.gravar("token", token), ContaKeychain.gravar("email", confirmado) else {
                apagarSessao()
                erro = .servico
                return
            }
            if let n: Int = (r.corpo["users"] as? NSNumber)?.intValue, n >= 0 { guardarUsuarios(n) }
            ultimaRevalidacao = Date()
            self.email = confirmado
        }
    }

    /// Sai: apaga o token local na hora; avisa o servidor quando der.
    func sair() {
        let guardada: ContaSessao? = sessao()
        if let guardada {
            let fila = Set(pendentes() + [guardada.token])
            // Keep failed/offline revocations in the device-only Keychain.
            ContaKeychain.gravar("revogar", fila.sorted().joined(separator: "\n"))
        }
        apagarSessao()
        email = nil
        entrando = true
        erro = nil
        revogarPendentes()
    }

    private func pendentes() -> [String] {
        (ContaKeychain.ler("revogar") ?? "").components(separatedBy: "\n").filter {
            $0.range(of: "^[A-Za-z0-9_-]{43}$", options: .regularExpression) != nil
        }
    }

    private func revogarPendentes() {
        guard !revogando else { return }
        let fila = pendentes()
        guard !fila.isEmpty else { return }
        revogando = true
        Task { @MainActor in
            defer { revogando = false }
            for token in fila {
                let r = await ContaAPI.chamar("/api/auth/logout", metodo: "POST", corpo: [:], token: token)
                if r.ok || r.status == 401 {
                    let restam = pendentes().filter { $0 != token }
                    if restam.isEmpty { ContaKeychain.apagar("revogar") }
                    else { ContaKeychain.gravar("revogar", restam.joined(separator: "\n")) }
                }
            }
        }
    }

    private func apagarSessao() {
        ContaKeychain.apagar("token")
        ContaKeychain.apagar("email")
    }
}
