// =============================================================================
//  Aurea / platform / ios / app / CrashReporter.swift
//
//  Crash e fechamento anormal → relatório para o e-mail do desenvolvedor.
//  Par de android/.../diagnostics/CrashReporter.kt; o POST é o mesmo
//  (/api/crash no Worker) e o formato do relatório também.
//
//  De onde vem cada relatório:
//   · MetricKit (MXDiagnosticPayload): crash com a pilha simbolizável, travada
//     (HANG) e excesso de CPU — o iOS entrega na abertura seguinte;
//   · NSException não tratada: nome, motivo e pilha, gravados antes de morrer;
//   · sinal fatal (SIGSEGV, SIGABRT, trap do Swift...): um marcador de 4 bytes
//     escrito pelo handler (só chamadas seguras para sinal);
//   · marcador "na frente": o app morreu sem ir para o segundo plano (memória,
//     watchdog) → ABNORMAL_EXIT.
//
//  Cada relatório é coletado UMA vez (id em UserDefaults), fica na caixa de
//  saída e sai quando há conta (o cadastro avisa desse envio). Nada de projeto,
//  caminho de mídia ou URI: a pilha passa por `sanitizar` antes de sair.
// =============================================================================
import CryptoKit
import Darwin
import Foundation
import MetricKit

// Globais do handler de sinal: preparadas na instalação, lidas sem alocar.
private var aureaSinalCaminho: UnsafeMutablePointer<CChar>? = nil
private var aureaSinaisAnteriores: UnsafeMutablePointer<sig_t?>? = nil
private var aureaExcecaoAnterior: NSUncaughtExceptionHandler? = nil

private func aureaTratarSinal(_ sinal: Int32) {
    if let caminho: UnsafeMutablePointer<CChar> = aureaSinalCaminho {
        let fd: Int32 = open(caminho, O_WRONLY | O_CREAT | O_TRUNC, 0o644)
        if fd >= 0 {
            var numero: Int32 = sinal
            _ = write(fd, &numero, 4)
            _ = close(fd)
        }
    }
    var anterior: sig_t? = nil
    if let tabela: UnsafeMutablePointer<sig_t?> = aureaSinaisAnteriores, sinal > 0, sinal < 32 {
        anterior = tabela[Int(sinal)]
    }
    // Devolve o sinal a quem o tinha antes (ou ao padrão) e o repete: o
    // processo morre como morreria sem o Aurea no meio.
    _ = signal(sinal, anterior ?? SIG_DFL)
    _ = raise(sinal)
}

private func aureaTratarExcecao(_ excecao: NSException) {
    CrashReporter.gravarExcecao(excecao)
    aureaExcecaoAnterior?(excecao)
}

struct CrashRelatorio: Codable {
    var reportId: String
    var platform: String
    var appVersion: String
    var appBuild: String
    var os: String
    var osVersion: String
    var deviceModel: String
    var manufacturer: String
    var abi: String
    var reason: String
    var phase: String
    var timestamp: Int64
    var stack: String
    var stackTruncated: Bool

    func corpo(instalacao: String, email: String?) -> [String: Any] {
        var d: [String: Any] = ["reportId": reportId, "platform": platform, "appVersion": appVersion,
                                "appBuild": appBuild, "os": os, "osVersion": osVersion,
                                "deviceModel": deviceModel, "manufacturer": manufacturer, "abi": abi,
                                "reason": reason, "phase": phase, "timestamp": timestamp,
                                "stack": stack, "stackTruncated": stackTruncated, "installId": instalacao]
        if let email: String = email, !email.isEmpty { d["email"] = email }
        return d
    }
}

final class CrashReporter: NSObject, MXMetricManagerSubscriber {
    static let shared: CrashReporter = CrashReporter()
    static let pilhaMaxBytes: Int = 200 * 1024

    private let fila: DispatchQueue = DispatchQueue(label: "com.aurea.crash", qos: .utility)
    private var instalado: Bool = false
    private var appVersion: String = ""
    private var appBuild: String = ""
    private var osVersion: String = ""
    private var modelo: String = ""
    /// A sessão para o envio (só lida e escrita na `fila`).
    private var sessaoAtual: ContaSessao?

    private static let chaveVistos: String = "aurea.crash.vistos"
    private static let chaveInstalacao: String = "aurea.crash.instalacao"

    // --- pastas -----------------------------------------------------------------
    private static var raiz: URL {
        let base: URL = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask).first
            ?? URL(fileURLWithPath: NSTemporaryDirectory())
        return base.appendingPathComponent("crash", isDirectory: true)
    }
    private static var caixa: URL { return raiz.appendingPathComponent("outbox", isDirectory: true) }
    private static var arquivoSinal: URL { return raiz.appendingPathComponent("sinal.bin") }
    private static var arquivoExcecao: URL { return raiz.appendingPathComponent("excecao.json") }
    private static var marcador: URL { return raiz.appendingPathComponent("aberto") }
    private static var marcadorAnterior: URL { return raiz.appendingPathComponent("aberto.anterior") }

    /// Chamado no init do App, antes de qualquer tela. Uma vez por processo.
    func instalar() {
        guard !instalado else { return }
        instalado = true
        let fm: FileManager = FileManager.default
        try? fm.createDirectory(at: CrashReporter.caixa, withIntermediateDirectories: true)
        // O marcador "na frente" da sessão anterior, antes que esta escreva o dela.
        if fm.fileExists(atPath: CrashReporter.marcador.path) {
            try? fm.removeItem(at: CrashReporter.marcadorAnterior)
            try? fm.moveItem(at: CrashReporter.marcador, to: CrashReporter.marcadorAnterior)
        }
        appVersion = Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? ""
        appBuild = Bundle.main.object(forInfoDictionaryKey: "CFBundleVersion") as? String ?? ""
        let v: OperatingSystemVersion = ProcessInfo.processInfo.operatingSystemVersion
        osVersion = "\(v.majorVersion).\(v.minorVersion).\(v.patchVersion)"
        modelo = CrashReporter.identificadorDoAparelho()

        CrashReporter.instalarSinais(caminho: CrashReporter.arquivoSinal.path)
        aureaExcecaoAnterior = NSGetUncaughtExceptionHandler()
        NSSetUncaughtExceptionHandler(aureaTratarExcecao)

        MXMetricManager.shared.add(self)
        let passados: [MXDiagnosticPayload] = MXMetricManager.shared.pastDiagnosticPayloads
        fila.async {
            self.coletarMarcadores()
            self.processar(passados)
        }
    }

    private static func instalarSinais(caminho: String) {
        let bytes: [CChar] = Array(caminho.utf8CString)
        let buffer: UnsafeMutablePointer<CChar> = UnsafeMutablePointer<CChar>.allocate(capacity: bytes.count)
        buffer.initialize(from: bytes, count: bytes.count)
        aureaSinalCaminho = buffer
        let tabela: UnsafeMutablePointer<sig_t?> = UnsafeMutablePointer<sig_t?>.allocate(capacity: 32)
        tabela.initialize(repeating: nil, count: 32)
        aureaSinaisAnteriores = tabela
        let sinais: [Int32] = [SIGABRT, SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGTRAP]
        for sinal: Int32 in sinais {
            let anterior: sig_t? = signal(sinal, aureaTratarSinal)
            tabela[Int(sinal)] = anterior
        }
    }

    /// Ida e volta do primeiro plano (scenePhase). Sem o "voltar", a próxima
    /// abertura sabe que o app morreu na frente.
    func primeiroPlano(_ naFrente: Bool, etapa: String) {
        fila.async {
            if naFrente {
                try? Data(etapa.utf8).write(to: CrashReporter.marcador)
            } else {
                try? FileManager.default.removeItem(at: CrashReporter.marcador)
            }
        }
    }

    /// Roda no handler de exceção (processo morrendo): escreve e sai.
    static func gravarExcecao(_ excecao: NSException) {
        let pilha: String = excecao.callStackSymbols.joined(separator: "\n")
        let d: [String: Any] = ["name": excecao.name.rawValue, "reason": excecao.reason ?? "",
                                "stack": pilha, "ts": Int64(Date().timeIntervalSince1970 * 1000)]
        if let dados: Data = try? JSONSerialization.data(withJSONObject: d) {
            try? dados.write(to: arquivoExcecao, options: .atomic)
        }
    }

    // --- MetricKit --------------------------------------------------------------
    func didReceive(_ payloads: [MXDiagnosticPayload]) {
        fila.async {
            self.processar(payloads)
            self.enviarCaixa()   // com sessão já conhecida, sai agora
        }
    }

    private func processar(_ payloads: [MXDiagnosticPayload]) {
        for payload: MXDiagnosticPayload in payloads {
            let quando: Int64 = Int64(payload.timeStampEnd.timeIntervalSince1970 * 1000)
            for d: MXCrashDiagnostic in payload.crashDiagnostics ?? [] {
                var detalhe: String = "sinal=\(d.signal?.stringValue ?? "-") exceptionType=\(d.exceptionType?.stringValue ?? "-")"
                detalhe += " exceptionCode=\(d.exceptionCode?.stringValue ?? "-")"
                detalhe += "\nterminationReason: \(d.terminationReason ?? "-")"
                detalhe += "\nvirtualMemoryRegionInfo: \(d.virtualMemoryRegionInfo ?? "-")"
                adicionar(d, motivo: "CRASH", detalhe: detalhe, quando: quando)
            }
            for d: MXHangDiagnostic in payload.hangDiagnostics ?? [] {
                adicionar(d, motivo: "HANG", detalhe: "duração: \(d.hangDuration)", quando: quando)
            }
            for d: MXCPUExceptionDiagnostic in payload.cpuExceptionDiagnostics ?? [] {
                adicionar(d, motivo: "EXCESSIVE_RESOURCE_USAGE",
                          detalhe: "CPU \(d.totalCPUTime) em \(d.totalSampledTime)", quando: quando)
            }
        }
    }

    private func adicionar(_ d: MXDiagnostic, motivo: String, detalhe: String, quando: Int64) {
        let json: Data = d.jsonRepresentation()
        let resumo: SHA256.Digest = SHA256.hash(data: json)
        let hex: String = resumo.map { (b: UInt8) -> String in String(format: "%02x", b) }.joined()
        let id: String = "mx-" + String(hex.prefix(32))
        let meta: MXMetaData = d.metaData
        let texto: String = detalhe + "\n\n" + (String(data: json, encoding: .utf8) ?? "")
        gravarSeNovo(id: id, motivo: motivo, etapa: "", quando: quando, pilha: texto,
                     versao: d.applicationVersion, build: meta.applicationBuildVersion,
                     sistema: meta.osVersion, aparelho: meta.deviceType, abi: meta.platformArchitecture)
    }

    // --- marcadores da sessão anterior ----------------------------------------------
    private func coletarMarcadores() {
        let fm: FileManager = FileManager.default
        let agora: Int64 = Int64(Date().timeIntervalSince1970 * 1000)
        let temAnterior: Bool = fm.fileExists(atPath: CrashReporter.marcadorAnterior.path)
        let etapa: String = (try? String(contentsOf: CrashReporter.marcadorAnterior, encoding: .utf8)) ?? ""
        if let dados: Data = try? Data(contentsOf: CrashReporter.arquivoExcecao),
           let o: [String: Any] = (try? JSONSerialization.jsonObject(with: dados)) as? [String: Any] {
            let ts: Int64 = (o["ts"] as? NSNumber)?.int64Value ?? agora
            let nome: String = o["name"] as? String ?? "NSException"
            let pilha: String = "\(nome): \(o["reason"] as? String ?? "")\n\n\(o["stack"] as? String ?? "")"
            gravarSeNovo(id: "exc-\(ts)", motivo: "NSEXCEPTION", etapa: etapa, quando: ts, pilha: pilha)
        } else if let dados: Data = try? Data(contentsOf: CrashReporter.arquivoSinal), dados.count >= 4 {
            let numero: Int32 = dados.withUnsafeBytes { (p: UnsafeRawBufferPointer) -> Int32 in p.loadUnaligned(as: Int32.self) }
            let atributos: [FileAttributeKey: Any]? = try? fm.attributesOfItem(atPath: CrashReporter.arquivoSinal.path)
            let data: Date = (atributos?[.modificationDate] as? Date) ?? Date()
            let ts: Int64 = Int64(data.timeIntervalSince1970 * 1000)
            let nome: String = String(cString: strsignal(numero))
            gravarSeNovo(id: "sig-\(ts)", motivo: "SIGNAL", etapa: etapa, quando: ts,
                         pilha: "sinal \(numero) (\(nome)). A pilha completa chega em outro relatório (MetricKit), normalmente na abertura seguinte.")
        } else if temAnterior {
            let atributos: [FileAttributeKey: Any]? = try? fm.attributesOfItem(atPath: CrashReporter.marcadorAnterior.path)
            let data: Date = (atributos?[.modificationDate] as? Date) ?? Date()
            let ts: Int64 = Int64(data.timeIntervalSince1970 * 1000)
            gravarSeNovo(id: "abnormal-\(ts)", motivo: "ABNORMAL_EXIT", etapa: etapa, quando: ts,
                         pilha: "O app fechou com a tela aberta sem ir para o segundo plano (memória, watchdog do sistema ou crash sem marcador). Se foi crash, o MetricKit manda a pilha em outro relatório.")
        }
        try? fm.removeItem(at: CrashReporter.arquivoExcecao)
        try? fm.removeItem(at: CrashReporter.arquivoSinal)
        try? fm.removeItem(at: CrashReporter.marcadorAnterior)
    }

    private func gravarSeNovo(id: String, motivo: String, etapa: String, quando: Int64, pilha: String,
                              versao: String? = nil, build: String? = nil, sistema: String? = nil,
                              aparelho: String? = nil, abi: String? = nil) {
        var vistos: [String] = UserDefaults.standard.stringArray(forKey: CrashReporter.chaveVistos) ?? []
        guard !vistos.contains(id) else { return }
        let limpa: (String, Bool) = CrashReporter.truncarUtf8(CrashReporter.sanitizar(pilha), max: CrashReporter.pilhaMaxBytes)
        #if arch(arm64)
        let abiAtual: String = "arm64"
        #else
        let abiAtual: String = "x86_64"
        #endif
        let r: CrashRelatorio = CrashRelatorio(
            reportId: id, platform: "ios", appVersion: versao ?? appVersion, appBuild: build ?? appBuild,
            os: "iOS", osVersion: sistema ?? osVersion, deviceModel: aparelho ?? modelo, manufacturer: "Apple",
            abi: abi ?? abiAtual, reason: motivo, phase: CrashReporter.sanitizar(etapa), timestamp: quando,
            stack: limpa.0, stackTruncated: limpa.1)
        guard let dados: Data = try? JSONEncoder().encode(r) else { return }
        let destino: URL = CrashReporter.caixa.appendingPathComponent(id + ".json")
        guard (try? dados.write(to: destino, options: .atomic)) != nil else { return }
        vistos.append(id)
        if vistos.count > 200 { vistos.removeFirst(vistos.count - 200) }
        UserDefaults.standard.set(vistos, forKey: CrashReporter.chaveVistos)
        // A caixa não cresce sem fim (sem conta ou sem rede por meses).
        let arquivos: [URL] = (try? FileManager.default.contentsOfDirectory(at: CrashReporter.caixa,
            includingPropertiesForKeys: [.contentModificationDateKey])) ?? []
        if arquivos.count > 20 {
            let ordenados: [URL] = arquivos.sorted { (a: URL, b: URL) -> Bool in a.lastPathComponent < b.lastPathComponent }
            for velho: URL in ordenados.prefix(arquivos.count - 20) { try? FileManager.default.removeItem(at: velho) }
        }
    }

    // --- envio ----------------------------------------------------------------------
    /// Envia a caixa de saída. Só com sessão (o cadastro avisa do envio).
    /// 2xx → sai da caixa; 400/413 → descartado; 429 ou sem rede → próxima abertura.
    func enviarPendentes(sessao: ContaSessao?) {
        fila.async {
            // nil = saiu da conta: nada mais sai até entrar de novo.
            self.sessaoAtual = sessao
            self.enviarCaixa()
        }
    }

    /// Só na `fila`.
    private func enviarCaixa() {
        guard let sessao: ContaSessao = sessaoAtual else { return }
        let instalacao: String = CrashReporter.instalacao()
        let arquivos: [URL] = (try? FileManager.default.contentsOfDirectory(at: CrashReporter.caixa,
            includingPropertiesForKeys: nil)) ?? []
        let pendentes: [URL] = arquivos.filter { (u: URL) -> Bool in u.pathExtension == "json" }
            .sorted { (a: URL, b: URL) -> Bool in a.lastPathComponent < b.lastPathComponent }
        for arquivo: URL in pendentes.prefix(5) {
            guard let dados: Data = try? Data(contentsOf: arquivo),
                  let r: CrashRelatorio = try? JSONDecoder().decode(CrashRelatorio.self, from: dados),
                  let corpo: Data = try? JSONSerialization.data(withJSONObject: r.corpo(instalacao: instalacao, email: sessao.email)) else {
                try? FileManager.default.removeItem(at: arquivo)
                continue
            }
            let status: Int = CrashReporter.postar(corpo, token: sessao.token)
            if (200...299).contains(status) || status == 400 || status == 413 {
                try? FileManager.default.removeItem(at: arquivo)
            } else if status == 0 || status == 429 {
                return
            }
        }
    }

    private static func postar(_ corpo: Data, token: String) -> Int {
        guard let url: URL = URL(string: ContaAPI.base + "/api/crash") else { return 0 }
        var pedido: URLRequest = URLRequest(url: url, cachePolicy: .reloadIgnoringLocalCacheData, timeoutInterval: 30)
        pedido.httpMethod = "POST"
        pedido.httpBody = corpo
        pedido.setValue("application/json; charset=utf-8", forHTTPHeaderField: "Content-Type")
        pedido.setValue("Bearer " + token, forHTTPHeaderField: "Authorization")
        let pronto: DispatchSemaphore = DispatchSemaphore(value: 0)
        var status: Int = 0
        let tarefa: URLSessionDataTask = URLSession.shared.dataTask(with: pedido) { (_: Data?, resposta: URLResponse?, _: Error?) in
            status = (resposta as? HTTPURLResponse)?.statusCode ?? 0
            pronto.signal()
        }
        tarefa.resume()
        _ = pronto.wait(timeout: .now() + 40)
        return status
    }

    // --- utilidades -------------------------------------------------------------------
    private static func instalacao() -> String {
        if let id: String = UserDefaults.standard.string(forKey: chaveInstalacao) { return id }
        let novo: String = UUID().uuidString.lowercased()
        UserDefaults.standard.set(novo, forKey: chaveInstalacao)
        return novo
    }

    private static func identificadorDoAparelho() -> String {
        var info: utsname = utsname()
        _ = uname(&info)
        let espelho: Mirror = Mirror(reflecting: info.machine)
        var texto: String = ""
        for filho: Mirror.Child in espelho.children {
            guard let valor: Int8 = filho.value as? Int8, valor != 0 else { continue }
            texto.append(Character(UnicodeScalar(UInt8(bitPattern: valor))))
        }
        return texto
    }

    /// Apaga URIs e caminhos de dados do usuário (mesmas regras do Android).
    static func sanitizar(_ texto: String) -> String {
        var s: String = substituir(texto, padrao: "\\b(content|file)://[^\\s'\"<>)\\]]+", modelo: "$1://<removido>", linhas: false)
        s = substituir(s, padrao: "(/storage/|/sdcard/|/mnt/|/data/user/\\d+/|/data/data/|/data/media/|/private/var/mobile/|/var/mobile/)[^\\n'\"]*?(?=:\\s|['\"]|\\s\\(|$)",
                       modelo: "$1<removido>", linhas: true)
        return s
    }

    private static func substituir(_ texto: String, padrao: String, modelo: String, linhas: Bool) -> String {
        let opcoes: NSRegularExpression.Options = linhas ? [.anchorsMatchLines] : []
        guard let re: NSRegularExpression = try? NSRegularExpression(pattern: padrao, options: opcoes) else { return texto }
        let faixa: NSRange = NSRange(texto.startIndex..<texto.endIndex, in: texto)
        return re.stringByReplacingMatches(in: texto, options: [], range: faixa, withTemplate: modelo)
    }

    /// Corta em `max` bytes de UTF-8 sem partir caractere.
    static func truncarUtf8(_ texto: String, max: Int) -> (String, Bool) {
        let dados: [UInt8] = Array(texto.utf8)
        if dados.count <= max { return (texto, false) }
        var fim: Int = max
        while fim > 0 && (dados[fim] & 0xC0) == 0x80 { fim -= 1 }
        return (String(decoding: dados[0..<fim], as: UTF8.self), true)
    }
}
