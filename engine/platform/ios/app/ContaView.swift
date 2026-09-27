// =============================================================================
//  Aurea / platform / ios / app / ContaView.swift
//
//  A tela da conta obrigatória (par de android/.../conta/ContaScreen.kt):
//  cadastro na primeira abertura, entrada depois (sair, reinstalar, sessão
//  expirada). Cobre o app inteiro até a pessoa estar dentro.
//
//  A senha fica num @State local: não vai para UserDefaults, log ou restauração.
// =============================================================================
import SwiftUI

struct ContaView: View {
    @EnvironmentObject private var conta: ContaModel
    @State private var email: String = ""
    @State private var senha: String = ""
    @State private var mostrar: Bool = false
    @FocusState private var campoSenha: Bool

    private func enviar() {
        campoSenha = false
        conta.enviar(email: email, senha: senha)
    }

    var body: some View {
        ZStack {
            AureaColors.background.ignoresSafeArea()
            ScrollView {
                VStack(spacing: 0) {
                    HomeBrandLogo().frame(width: 72, height: 72)
                    Text("aurea").font(.aurea(size: 34, weight: .bold)).tracking(-1)
                        .foregroundStyle(AureaColors.text).padding(.top, 16)
                    Text(AureaText.t(conta.entrando ? "conta_titulo_entrar" : "conta_titulo_cadastro"))
                        .font(.aurea(size: 22, weight: .bold)).foregroundStyle(AureaColors.text)
                        .multilineTextAlignment(.center).padding(.top, 20)
                    Text(AureaText.t("conta_subtitulo"))
                        .font(.aurea(size: 12.5)).foregroundStyle(AureaColors.muted)
                        .multilineTextAlignment(.center).padding(.top, 8)

                    campoEmail.padding(.top, 24)
                    campoDaSenha.padding(.top, 12)
                    Text(AureaText.t("conta_senha_dica"))
                        .font(.aurea(size: 11)).foregroundStyle(AureaColors.subtle)
                        .frame(maxWidth: .infinity, alignment: .leading).padding(.top, 6).padding(.leading, 4)

                    if let erro: ContaErro = conta.erro {
                        Text(AureaText.t(erro.chave))
                            .font(.aurea(size: 12.5)).foregroundStyle(AureaColors.danger)
                            .multilineTextAlignment(.center).frame(maxWidth: .infinity).padding(.top, 14)
                            .accessibilityIdentifier("conta.erro")
                    }

                    Button(action: enviar) {
                        ZStack {
                            if conta.ocupado {
                                AureaActivityIndicator()
                            } else {
                                Text(AureaText.t(conta.entrando ? "conta_entrar" : "conta_criar"))
                                    .font(.aurea(size: 17, weight: .semibold)).foregroundStyle(AureaColors.onAccent)
                            }
                        }
                        .frame(maxWidth: .infinity, minHeight: 52)
                        .background(conta.ocupado ? AureaColors.accentDim : AureaColors.accent,
                                    in: RoundedRectangle(cornerRadius: 14))
                    }
                    .buttonStyle(.plain).disabled(conta.ocupado).padding(.top, 20)
                    .accessibilityIdentifier("conta.enviar")

                    Button {
                        conta.entrando.toggle()
                        conta.erro = nil
                    } label: {
                        Text(AureaText.t(conta.entrando ? "conta_ir_cadastro" : "conta_ir_entrar"))
                            .font(.aurea(size: 14.5, weight: .semibold)).foregroundStyle(AureaColors.accent)
                            .padding(.horizontal, 12).frame(minHeight: 44)
                    }
                    .buttonStyle(.plain).disabled(conta.ocupado).padding(.top, 8)
                    .accessibilityIdentifier("conta.alternar")

                    Text(AureaText.t("conta_privacidade"))
                        .font(.aurea(size: 11)).foregroundStyle(AureaColors.subtle)
                        .multilineTextAlignment(.center).padding(.top, 24)
                }
                .frame(maxWidth: 440)
                .padding(.horizontal, 24).padding(.vertical, 32)
                .frame(maxWidth: .infinity)
            }
            .scrollDismissesKeyboard(.interactively)
        }
    }

    private var campoEmail: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text(AureaText.t("conta_email")).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted).padding(.leading, 4)
            TextField("", text: $email)
                .font(.aurea(size: 17)).foregroundStyle(AureaColors.text)
                .keyboardType(.emailAddress).textContentType(.username)
                .textInputAutocapitalization(.never).autocorrectionDisabled(true)
                .submitLabel(.next).onSubmit { campoSenha = true }
                .onChange(of: email) { _ in conta.erro = nil }
                .padding(.horizontal, 14).frame(minHeight: 50)
                .background(AureaColors.surfaceHigh, in: RoundedRectangle(cornerRadius: 12))
                .overlay(RoundedRectangle(cornerRadius: 12).stroke(AureaColors.border, lineWidth: 1))
                .accessibilityLabel(AureaText.t("conta_email"))
                .accessibilityIdentifier("conta.email")
        }
    }

    private var campoDaSenha: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text(AureaText.t("conta_senha")).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted).padding(.leading, 4)
            HStack(spacing: 8) {
                Group {
                    if mostrar {
                        TextField("", text: $senha)
                    } else {
                        SecureField("", text: $senha)
                    }
                }
                .font(.aurea(size: 17)).foregroundStyle(AureaColors.text)
                .textContentType(conta.entrando ? UITextContentType.password : UITextContentType.newPassword)
                .textInputAutocapitalization(.never).autocorrectionDisabled(true)
                .focused($campoSenha)
                .submitLabel(.done).onSubmit(enviar)
                .onChange(of: senha) { _ in conta.erro = nil }
                .accessibilityLabel(AureaText.t("conta_senha"))
                .accessibilityIdentifier("conta.senha")
                Button(AureaText.t(mostrar ? "conta_ocultar" : "conta_mostrar")) { mostrar.toggle() }
                    .font(.aurea(size: 12, weight: .semibold)).foregroundStyle(AureaColors.accent)
                    .buttonStyle(.plain).frame(minHeight: 44)
            }
            .padding(.leading, 14).padding(.trailing, 8).frame(minHeight: 50)
            .background(AureaColors.surfaceHigh, in: RoundedRectangle(cornerRadius: 12))
            .overlay(RoundedRectangle(cornerRadius: 12).stroke(AureaColors.border, lineWidth: 1))
        }
    }
}
