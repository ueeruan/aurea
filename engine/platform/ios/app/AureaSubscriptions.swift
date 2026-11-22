// =============================================================================
//  ASSINATURAS (16/11/2026, decisão do dono; docs/release/assinaturas.md).
//  Par do Android: hyper/Subscriptions.kt (loja) e hyper/Paywall.kt (tela).
//
//  AureaHyper (R$ 59,90/mês) libera tudo o que é pago; cada efeito pago e o
//  Texto 3D são uma assinatura avulsa (R$ 5,00/mês). Na App Store cada avulso
//  fica no SEU grupo de assinatura (senão assinar um segundo efeito trocaria o
//  primeiro) e o AureaHyper também. Os preços vivem na loja: a tela mostra o
//  `displayPrice` do Product e, sem ele, o texto padrão. Os ids vêm do motor
//  (hyper::subscription_product_ids).
//
//  Compra: StoreKit 2 (Product.purchase) → o id da transação vai ao Worker
//  (POST /api/entitlements/purchase, que confere na App Store) → só depois do OK
//  do servidor a transação é finalizada. Restaurar: Transaction.currentEntitlements
//  ao abrir o app/entrar e no botão "Restaurar compras"; renovações e compras de
//  fora do app chegam por Transaction.updates.
//
//  accessibilityIdentifier (as mesmas tags do Android):
//    assinatura · assinatura.fechar · assinatura.avulso · assinatura.hyper ·
//    assinatura.restaurar · assinatura.entrar · assinatura.verificar · assinatura.aviso
// =============================================================================
import StoreKit
import SwiftUI

/// O item pago que o paywall vende. `product` = assinatura avulsa (nil = só o AureaHyper).
struct PaidItem: Equatable, Identifiable {
    enum Kind: Equatable { case effect, text3D, hyperTools }
    let product: String?
    let kind: Kind
    var name: String? = nil
    var subscriptionOnly = false
    var id: String { "\(kind)-\(product ?? "hyper")-\(name ?? "")-\(subscriptionOnly)" }

    static func effect(_ typeId: UInt32, name: String?) -> PaidItem {
        PaidItem(product: AureaEngine.effectProductId(typeId), kind: .effect, name: name)
    }
    static func text3D() -> PaidItem { PaidItem(product: AureaEngine.text3DProductId(), kind: .text3D) }
    static let hyperTools = PaidItem(product: nil, kind: .hyperTools)
    static let subscriptionTools = PaidItem(product: nil, kind: .hyperTools, subscriptionOnly: true)
}

@MainActor
final class AureaSubscriptions: ObservableObject {
    static let shared = AureaSubscriptions()
    static let restoring = "restore"
    /// O intervalo mínimo entre duas restaurações silenciosas (abrir o app/entrar).
    private static let silentRestoreInterval: TimeInterval = 10 * 60

    /// Os produtos da loja (preço localizado em `displayPrice`).
    @Published private(set) var products: [String: Product] = [:]
    /// O produto em compra, ou `restoring`; nil = nada em andamento.
    @Published private(set) var busy: String?
    /// O resultado do último passo (chave de texto).
    @Published private(set) var notice: String?

    private var loadingProducts = false
    private var lastSilentRestore: Date?
    private var lastRestoreSession: String?
    private var delivering: Set<UInt64> = []
    private var updates: Task<Void, Never>?

    private init() {}

    var hyperProduct: String { AureaEngine.hyperSubscriptionId() }
    var productIds: [String] { AureaEngine.subscriptionProductIds() }

    func clearNotice() { notice = nil }

    /// Renovações, compras feitas fora do app (Ajustes, outro aparelho) e pagamentos que estavam pendentes.
    func startListening() {
        guard updates == nil else { return }
        updates = Task { [weak self] in
            for await result in StoreKit.Transaction.updates {
                guard case .verified(let t) = result else { continue }
                await self?.handleUpdate(t)
            }
        }
    }

    private func handleUpdate(_ t: StoreKit.Transaction) async {
        guard productIds.contains(t.productID) else { return }
        guard let session = ContaModel.lerSessao() else { return }
        if t.revocationDate != nil || (t.expirationDate.map { $0 < Date() } ?? false) {
            // Reembolso/vencimento: o servidor já não vai devolver o produto.
            await t.finish()
            ContaModel.atualizarAcessosGuardados()
            return
        }
        _ = await deliver(t, session: session)
    }

    func loadProducts() async {
        guard !loadingProducts, products.isEmpty else { return }
        loadingProducts = true
        defer { loadingProducts = false }
        guard let found = try? await Product.products(for: productIds) else { return }
        var map: [String: Product] = [:]
        for p in found { map[p.id] = p }
        products = map
    }

    /// O preço mensal localizado da loja, ou nil (a tela usa o texto padrão).
    func price(_ id: String) -> String? { products[id]?.displayPrice }

    /// Assina `id` pela App Store e entrega ao servidor.
    func buy(_ id: String) async {
        guard busy == nil else { return }
        guard let session = ContaModel.lerSessao() else { notice = "assinatura_entrar"; return }
        busy = id; notice = nil
        defer { busy = nil }
        // Já assinada nesta conta da loja (o app caiu antes de entregar): só entrega.
        var owned: StoreKit.Transaction?
        for await result in StoreKit.Transaction.currentEntitlements {
            if case .verified(let t) = result, t.productID == id, t.revocationDate == nil { owned = t; break }
        }
        if owned == nil {
            if products[id] == nil { await loadProducts() }
            guard let product = products[id] else { notice = "assinatura_indisponivel"; return }
            do {
                switch try await product.purchase() {
                case .success(let verification):
                    guard case .verified(let t) = verification else { notice = "assinatura_invalida"; return }
                    owned = t
                case .pending: notice = "assinatura_pendente"; return
                case .userCancelled: return
                @unknown default: return
                }
            } catch {
                notice = "assinatura_indisponivel"; return
            }
        }
        guard let transaction = owned else { notice = "assinatura_indisponivel"; return }
        notice = await deliver(transaction, session: session)
    }

    /// Restaura as assinaturas desta conta da loja. `silent` = abertura do app ou login:
    /// no máximo a cada 10 min, só o que a conta ainda não tem, sem aviso.
    func restore(silent: Bool, retry: Bool = false) async {
        guard busy == nil else { return }
        guard let session = ContaModel.lerSessao() else { if !silent { notice = "assinatura_entrar" }; return }
        if silent {
            if !retry, lastRestoreSession == session.token, let last = lastSilentRestore,
               Date().timeIntervalSince(last) < Self.silentRestoreInterval { return }
            lastRestoreSession = session.token
            lastSilentRestore = Date()
        } else {
            busy = Self.restoring; notice = nil
            // A App Store sincroniza as compras desta conta da loja (pode pedir a senha do ID Apple).
            try? await AppStore.sync()
        }
        defer { if !silent { busy = nil } }
        let ours = Set(productIds)
        let have = HyperEntitlements.shared.ownedProducts()
        var found: [StoreKit.Transaction] = []
        var seen = Set<UInt64>()
        // An already charged purchase can still be waiting for delivery.
        for await result in StoreKit.Transaction.unfinished {
            guard case .verified(let t) = result, ours.contains(t.productID), t.revocationDate == nil else { continue }
            if seen.insert(t.id).inserted { found.append(t) }
        }
        for await result in StoreKit.Transaction.currentEntitlements {
            guard case .verified(let t) = result, ours.contains(t.productID), t.revocationDate == nil else { continue }
            if silent && have.contains(t.productID) { continue }
            if seen.insert(t.id).inserted { found.append(t) }
        }
        var result: String? = found.isEmpty ? "assinatura_nada_restaurar" : nil
        for t in found {
            let r = await deliver(t, session: session)
            // O pior resultado fica (uma falha não some atrás de um sucesso).
            if result == nil || result == "assinatura_obrigado" { result = r }
        }
        if !silent { notice = result == "assinatura_obrigado" ? "assinatura_restaurada" : result }
    }

    /// Entrega ao servidor; com o OK, finaliza a transação. Devolve a chave do aviso.
    private func deliver(_ t: StoreKit.Transaction, session: ContaSessao) async -> String {
        guard delivering.insert(t.id).inserted else { return "assinatura_processando" }
        defer { delivering.remove(t.id) }
        let r = await ContaAPI.chamar("/api/entitlements/purchase", metodo: "POST",
                                      corpo: ["platform": "apple", "product": t.productID, "token": String(t.id)], token: session.token)
        switch r.status {
        case 200:
            let products = ServerEntitlementProvider.stringSet(r.corpo["products"] as? [Any] ?? [])
            guard products.contains(t.productID) else { return "assinatura_processando" }
            guard HyperEntitlements.shared.acceptServerProducts(products, expires: ServerEntitlementProvider.expiresOf(r.corpo), session: session)
            else { return "assinatura_entrar" }
            await t.finish()
            return "assinatura_obrigado"
        case 409: return "assinatura_outra_conta"
        case 402:
            switch r.corpo["error"] as? String {
            case "purchase_pending": return "assinatura_pendente"
            case "purchase_expired": return "assinatura_vencida"
            default: return "assinatura_invalida"
            }
        case 401: return "assinatura_entrar"
        default: return "assinatura_indisponivel"
        }
    }
}

@MainActor
extension ContaModel {
    /// Para quem não tem a ContaModel à mão (Transaction.updates): confere os acessos da sessão guardada.
    static func atualizarAcessosGuardados() {
        let s = lerSessao()
        HyperEntitlements.shared.refresh(token: s?.token, email: s?.email)
    }
}

// MARK: - Tela

/// A folha do paywall: abre ao tocar num efeito pago sem o acesso e ao criar Texto 3D sem o acesso.
struct PaywallSheet: View {
    @EnvironmentObject private var conta: ContaModel
    @ObservedObject private var entitlements = HyperEntitlements.shared
    @State private var signingIn = false
    let item: PaidItem
    let onDismiss: () -> Void

    var body: some View {
        NavigationStack {
            Group {
                if signingIn { ContaView() }
                else {
                    ScrollView {
                        PaywallContent(item: item, access: entitlements.accessFor(item.product, subscriptionOnly: item.subscriptionOnly),
                                       onSignIn: { conta.entrando = true; signingIn = true },
                                       onRetry: { conta.atualizarAcessos(retryPurchases: true) })
                            .padding(.horizontal, 18)
                        Spacer().frame(height: 24)
                    }
                }
            }
            .background(AureaColors.background.ignoresSafeArea()).foregroundStyle(AureaColors.text)
            .toolbar { ToolbarItem(placement: .confirmationAction) {
                Button(AureaText.t("editor_fechar")) { onDismiss() }.accessibilityIdentifier("assinatura.fechar")
            } }
        }
        .preferredColorScheme(.dark)
        .accessibilityIdentifier("assinatura")
        .onAppear {
            AureaSubscriptions.shared.clearNotice()
            conta.atualizarAcessos()
        }
        .onChange(of: conta.logado) { if $0 { signingIn = false; conta.atualizarAcessos() } }
        .onChange(of: entitlements.revision) { _ in
            if entitlements.accessFor(item.product, subscriptionOnly: item.subscriptionOnly) == .unlocked { onDismiss() }
        }
    }
}

private struct PaywallContent: View {
    let item: PaidItem
    let access: HyperAccess
    let onSignIn: () -> Void
    let onRetry: () -> Void

    private var title: String {
        if let name = item.name { return name }
        return item.kind == .text3D ? AureaText.t("sh_add_text_3d") : AureaText.t("hyper_title")
    }
    private var introKey: String {
        if item.product == nil { return "assinatura_intro_hyper" }
        return item.kind == .text3D ? "assinatura_intro_texto3d" : "assinatura_intro_efeito"
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            Spacer().frame(height: 8)
            HStack(spacing: 12) {
                Image(systemName: "lock.fill").font(.system(size: 20, weight: AureaIconWeight.symbol)).foregroundStyle(AureaColors.accent)
                    .frame(width: 44, height: 44).background(RoundedRectangle(cornerRadius: 12).fill(AureaColors.accent.opacity(0.18)))
                    .accessibilityHidden(true)
                VStack(alignment: .leading, spacing: 2) {
                    Text(title).font(.aurea(size: 22, weight: .bold)).accessibilityAddTraits(.isHeader)
                    Text(AureaText.t("assinatura_badge")).font(.aurea(size: 13, weight: .semibold)).foregroundStyle(AureaColors.accent)
                }
            }
            Spacer().frame(height: 14)
            Text(AureaText.t(introKey)).font(.aurea(size: 15)).fixedSize(horizontal: false, vertical: true)
            Spacer().frame(height: 18)
            switch access {
            case .signedOut:
                PaywallStatus(text: AureaText.t("assinatura_entrar"))
                Spacer().frame(height: 14)
                HyperButton(title: AureaText.t("conta_entrar"), id: "assinatura.entrar", action: onSignIn)
            case .needsConfig:
                PaywallStatus(text: AureaText.t("hyper_needs_internet"))
                Spacer().frame(height: 14)
                HyperButton(title: AureaText.t("home_retry"), id: "assinatura.verificar", primary: false) {
                    AppConfigStore.shared.refresh(force: true)
                    onRetry()
                }
            case .off:
                PaywallStatus(text: AppConfigStore.shared.config?.message(for: AureaText.language) ?? AureaText.t("hyper_off_default"))
            case .checking:
                PaywallStatus(text: AureaText.t("hyper_locked_checking"))
                Spacer().frame(height: 12)
                ProgressView().tint(AureaColors.accent)
            default:
                SubscriptionOptions(item: item)
            }
        }
    }
}

private struct PaywallStatus: View {
    let text: String
    var body: some View {
        Text(text).font(.aurea(size: 14, weight: .semibold)).fixedSize(horizontal: false, vertical: true)
            .frame(maxWidth: .infinity, alignment: .leading).padding(14)
            .background(RoundedRectangle(cornerRadius: 10).fill(AureaColors.surface))
    }
}

/// A assinatura do AureaHyper anual (1.0.2.1: a única à venda) e "Restaurar compras".
/// Também aparece no painel bloqueado do AureaHyper (só o AureaHyper).
struct SubscriptionOptions: View {
    @ObservedObject private var store = AureaSubscriptions.shared
    @ObservedObject private var entitlements = HyperEntitlements.shared
    let item: PaidItem

    var body: some View {
        let purchasable = entitlements.purchasable
        VStack(alignment: .leading, spacing: 0) {
            // 1.0.2.1 (19/11/2026): só o AureaHyper ANUAL se vende; acabaram os avulsos
            // (quem já tinha um continua com ele: hyper::has_access).
            let hyper = store.hyperProduct
            let hyperPrice = store.price(hyper) ?? AureaText.t("assinatura_preco_hyper")
            SubscriptionOptionCard(
                title: AureaText.t("hyper_title"),
                price: AureaText.t("assinatura_por_ano", hyperPrice) + " · " + AureaText.t("assinatura_libera_tudo"),
                id: "assinatura.hyper", highlighted: true,
                busy: store.busy == hyper, enabled: store.busy == nil && purchasable.contains(hyper)
            ) { Task { await store.buy(hyper) } }
            if purchasable.isEmpty {
                Spacer().frame(height: 12)
                Text(AureaText.t("assinatura_em_breve")).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted)
                    .fixedSize(horizontal: false, vertical: true)
            }
            Spacer().frame(height: 14)
            HyperButton(title: AureaText.t("assinatura_restaurar"), id: "assinatura.restaurar", primary: false,
                        enabled: store.busy == nil) { Task { await store.restore(silent: false) } }
            if store.busy == AureaSubscriptions.restoring {
                Spacer().frame(height: 8)
                ProgressView().tint(AureaColors.accent).accessibilityLabel(AureaText.t("assinatura_processando"))
            }
            if let key = store.notice {
                Spacer().frame(height: 12)
                Text(AureaText.t(key)).font(.aurea(size: 14, weight: .semibold)).fixedSize(horizontal: false, vertical: true)
                    .frame(maxWidth: .infinity, alignment: .leading).padding(14)
                    .background(RoundedRectangle(cornerRadius: 10).fill(AureaColors.surface))
                    .accessibilityIdentifier("assinatura.aviso")
                    .accessibilityAddTraits(.updatesFrequently)
            }
            Spacer().frame(height: 14)
            Text(AureaText.t("assinatura_renovacao", "App Store")).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
                .fixedSize(horizontal: false, vertical: true)
        }
        .task { await store.loadProducts() }
    }
}

private struct SubscriptionOptionCard: View {
    let title: String
    let price: String
    let id: String
    let highlighted: Bool
    let busy: Bool
    let enabled: Bool
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            HStack(spacing: 12) {
                VStack(alignment: .leading, spacing: 2) {
                    Text(title).font(.aurea(size: 16, weight: .bold))
                        .foregroundStyle(enabled || busy ? AureaColors.text : AureaColors.disabled)
                    Text(price).font(.aurea(size: 14, weight: .semibold))
                        .foregroundStyle(!enabled && !busy ? AureaColors.disabled : (highlighted ? AureaColors.accent : AureaColors.muted))
                }
                Spacer(minLength: 0)
                if busy { ProgressView().tint(AureaColors.accent) }
                else {
                    Image(systemName: highlighted ? "star.fill" : "checkmark").font(.system(size: 15, weight: AureaIconWeight.symbol))
                        .foregroundStyle(enabled ? AureaColors.accent : AureaColors.disabled).accessibilityHidden(true)
                }
            }
            .padding(.horizontal, 16).padding(.vertical, 12)
            .frame(maxWidth: .infinity, minHeight: 64, alignment: .leading)
            .background(RoundedRectangle(cornerRadius: 14).fill(highlighted ? AureaColors.accent.opacity(0.14) : AureaColors.surface))
            .overlay(RoundedRectangle(cornerRadius: 14).stroke(highlighted ? AureaColors.accent : AureaColors.border, lineWidth: highlighted ? 1.5 : 1))
        }
        .buttonStyle(.plain).disabled(!enabled)
        .accessibilityElement(children: .ignore)
        .accessibilityLabel("\(title), \(price)")
        .accessibilityValue(busy ? AureaText.t("assinatura_processando") : "")
        .accessibilityAddTraits(.isButton)
        .accessibilityIdentifier(id)
    }
}
