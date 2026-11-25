// =============================================================================
//  ASSINATURAS (16/11/2026, decisão do dono; docs/release/assinaturas.md).
//  Par do Android: hyper/Subscriptions.kt (loja) e hyper/Paywall.kt (tela).
//
//  1.0.9 (decisão do dono, 25/11/2026): o AureaHyper tem três planos à venda —
//  SEMANAL (R$ 7), MENSAL (R$ 30) e ANUAL (R$ 150) — e todos liberam tudo o que é
//  pago. Na App Store os três ficam no MESMO grupo (22585277), então trocar de
//  plano é upgrade/downgrade da loja, não uma segunda assinatura. Os avulsos
//  (efeitos, Texto 3D; cada um no seu grupo) não se vendem mais; quem já tem
//  continua com o acesso e o preço até renovar (coisa da loja). Os preços vivem
//  na loja: a tela mostra o `displayPrice` do Product e, sem ele, o texto padrão.
//  Os ids vêm do motor (hyper::hyper_plans_on_sale para vender,
//  hyper::subscription_product_ids para restaurar).
//
//  Compra: StoreKit 2 (Product.purchase) → o id da transação vai ao Worker
//  (POST /api/entitlements/purchase, que confere na App Store) → só depois do OK
//  do servidor a transação é finalizada. Restaurar: Transaction.currentEntitlements
//  ao abrir o app/entrar e no botão "Restaurar compras"; renovações e compras de
//  fora do app chegam por Transaction.updates.
//
//  accessibilityIdentifier (as mesmas tags do Android):
//    assinatura · assinatura.fechar · assinatura.semanal · assinatura.mensal ·
//    assinatura.hyper (o anual) · assinatura.economia · assinatura.restaurar ·
//    assinatura.entrar · assinatura.verificar · assinatura.aviso
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

    /// O AureaHyper ANUAL (o plano destacado).
    var hyperProduct: String { AureaEngine.hyperSubscriptionId() }
    /// Tudo o que o app reconhece, para restaurar (hyper::subscription_product_ids).
    var productIds: [String] { AureaEngine.subscriptionProductIds() }
    /// 1.0.9: os planos à venda na ordem da tela — semanal, mensal, anual (hyper::hyper_plans_on_sale).
    var plansOnSale: [String] { AureaEngine.hyperPlansOnSale() }

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

    /// O preço localizado da loja (por período do plano), ou nil (a tela usa o texto padrão).
    func price(_ id: String) -> String? { products[id]?.displayPrice }

    /// "Economize N%" do anual contra um ano do plano curto mais barato, só com os
    /// preços da loja carregados e na mesma moeda (a conta é do motor). 0 = sem selo.
    func annualSavingsPercent(weekly: String?, monthly: String?, annual: String?) -> Int {
        guard let annual, let a = products[annual] else { return 0 }
        let currency = a.priceFormatStyle.currencyCode
        func micros(_ id: String?) -> Int64 {
            guard let id, let p = products[id], p.priceFormatStyle.currencyCode == currency else { return 0 }
            return NSDecimalNumber(decimal: p.price * 1_000_000).int64Value
        }
        let w = micros(weekly), m = micros(monthly)
        guard w > 0 || m > 0 else { return 0 }
        return Int(AureaEngine.hyperAnnualSavingsPercent(weekly: w, monthly: m, annual: micros(annual)))
    }

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

/// Como a tela mostra um plano do AureaHyper (o id vem do motor; as mesmas tags do Android).
private struct HyperPlanRow: Identifiable {
    let id: String
    let titleKey: String
    let perKey: String
    let fallbackKey: String
    let tag: String
    let best: Bool

    init(_ id: String, annual: String) {
        self.id = id
        best = id == annual
        let row: [String]
        if best {
            row = ["assinatura_plano_anual", "assinatura_por_ano", "assinatura_preco_hyper", "assinatura.hyper"]
        } else if id.hasSuffix("_semanal") {
            row = ["assinatura_plano_semanal", "assinatura_por_semana", "assinatura_preco_semanal", "assinatura.semanal"]
        } else {
            row = ["assinatura_plano_mensal", "assinatura_por_mes", "assinatura_preco_mensal", "assinatura.mensal"]
        }
        titleKey = row[0]
        perKey = row[1]
        fallbackKey = row[2]
        tag = row[3]
    }
}

/// Os planos do AureaHyper à venda (1.0.9: semanal, mensal, anual; todos liberam
/// tudo) e "Restaurar compras". Também aparece no painel bloqueado do AureaHyper.
struct SubscriptionOptions: View {
    @ObservedObject private var store = AureaSubscriptions.shared
    @ObservedObject private var entitlements = HyperEntitlements.shared
    let item: PaidItem

    private var plans: [HyperPlanRow] {
        let annual = store.hyperProduct
        return store.plansOnSale.map { HyperPlanRow($0, annual: annual) }
    }

    private var savings: Int {
        let rows = plans
        return store.annualSavingsPercent(weekly: rows.first { $0.tag == "assinatura.semanal" }?.id,
                                          monthly: rows.first { $0.tag == "assinatura.mensal" }?.id,
                                          annual: rows.first { $0.best }?.id)
    }

    /// Um cartão (em função à parte: o Swift infere o `body` mais rápido).
    private func card(_ plan: HyperPlanRow, saved: Int, purchasable: Set<String>) -> SubscriptionOptionCard {
        let shown: String = store.price(plan.id) ?? AureaText.t(plan.fallbackKey)
        let badge: String? = plan.best ? AureaText.t("assinatura_melhor_valor") : nil
        let savings: String? = plan.best && saved > 0 ? AureaText.t("assinatura_economize", saved) : nil
        let enabled: Bool = store.busy == nil && purchasable.contains(plan.id)
        return SubscriptionOptionCard(title: AureaText.t(plan.titleKey), price: AureaText.t(plan.perKey, shown), badge: badge,
                                      savings: savings, id: plan.tag, highlighted: plan.best, busy: store.busy == plan.id,
                                      enabled: enabled) { Task { await store.buy(plan.id) } }
    }

    var body: some View {
        let purchasable = entitlements.purchasable
        let rows = plans
        let saved = savings
        VStack(alignment: .leading, spacing: 0) {
            // 1.0.9: semanal, mensal e anual; os avulsos não se vendem mais (quem já
            // tinha um continua com ele: hyper::has_access). O selo só com os preços da loja.
            if !rows.isEmpty {
                Text(AureaText.t("assinatura_escolha_plano")).font(.aurea(size: 13, weight: .semibold)).foregroundStyle(AureaColors.muted)
                    .fixedSize(horizontal: false, vertical: true)
                Spacer().frame(height: 10)
            }
            ForEach(Array(rows.enumerated()), id: \.element.id) { index, plan in
                if index > 0 { Spacer().frame(height: 10) }
                card(plan, saved: saved, purchasable: purchasable)
            }
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
    let badge: String?
    let savings: String?
    let id: String
    let highlighted: Bool
    let busy: Bool
    let enabled: Bool
    let action: () -> Void

    private var spoken: String { [title, badge, price, savings].compactMap { $0 }.joined(separator: ", ") }

    var body: some View {
        Button(action: action) {
            HStack(spacing: 12) {
                VStack(alignment: .leading, spacing: 2) {
                    HStack(spacing: 8) {
                        Text(title).font(.aurea(size: 16, weight: .bold))
                            .foregroundStyle(enabled || busy ? AureaColors.text : AureaColors.disabled)
                        if let badge {
                            Text(badge).font(.aurea(size: 11, weight: .bold)).foregroundStyle(AureaColors.onAccent)
                                .padding(.horizontal, 6).padding(.vertical, 2)
                                .background(RoundedRectangle(cornerRadius: 6).fill(AureaColors.accent))
                        }
                    }
                    Text(price).font(.aurea(size: 14, weight: .semibold))
                        .foregroundStyle(!enabled && !busy ? AureaColors.disabled : (highlighted ? AureaColors.accent : AureaColors.muted))
                    if let savings {
                        Text(savings).font(.aurea(size: 13, weight: .bold)).foregroundStyle(AureaColors.accent)
                            .accessibilityIdentifier("assinatura.economia")
                    }
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
        .accessibilityLabel(spoken)
        .accessibilityValue(busy ? AureaText.t("assinatura_processando") : "")
        .accessibilityAddTraits(.isButton)
        .accessibilityIdentifier(id)
    }
}
