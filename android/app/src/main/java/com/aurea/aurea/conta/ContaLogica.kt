package com.aurea.aurea.conta

/**
 * A lógica PURA da conta opcional — sem Android, testável na JVM.
 *
 * Regras que não mudam (iguais no iOS, `Conta.swift`):
 *  - sem sessão guardada, projetos e editor continuam acessíveis;
 *  - com sessão guardada, o app abre DIRETO, mesmo sem rede (ninguém fica
 *    trancado fora por estar offline);
 *  - ao revalidar com rede: 401 derruba a sessão; qualquer outra falha (sem
 *    rede, 5xx, limite) mantém quem já entrou;
 *  - a mensagem de erro do login é uma só para "e-mail não existe" e "senha
 *    errada" (quem decide isso é o servidor; aqui só traduzimos o código).
 */
sealed interface ContaEstado {
    /** Sem sessão: o app continua disponível, com entrada opcional no menu. */
    data object Fora : ContaEstado

    /** Sessão guardada (validada ou ainda não — offline também vale). */
    data class Dentro(val email: String) : ContaEstado
}

/** O que o aparelho guarda (no cofre do Keystore): SÓ o token e o e-mail. */
data class SessaoGuardada(val token: String, val email: String)

enum class ErroConta { EMAIL, SENHA, CREDENCIAIS, EM_USO, LIMITE, REDE, SERVICO, EXPIRADA }

/** Resultado de uma revalidação da sessão no servidor. */
enum class Revalidacao { VALIDA, INVALIDA, SEM_RESPOSTA }

object ContaLogica {
    const val SENHA_MIN = 8
    const val SENHA_MAX = 128
    private val LOCAL = Regex("^[a-z0-9.!#\$%&'*+/=?^_`{|}~-]+$")
    private val ROTULO = Regex("^[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?$")
    private val TLD = Regex("^[a-z]{2,63}$")

    fun inicial(guardada: SessaoGuardada?): ContaEstado =
        if (guardada != null && guardada.token.isNotBlank() && guardada.email.isNotBlank()) ContaEstado.Dentro(guardada.email)
        else ContaEstado.Fora

    /** O mesmo `normalizarEmail` do Worker: trim + minúsculas + formato; null se inválido. */
    fun normalizarEmail(valor: String): String? {
        val e = valor.trim().lowercase()
        if (e.length < 6 || e.length > 254) return null
        val arroba = e.indexOf('@')
        if (arroba < 1 || arroba != e.lastIndexOf('@')) return null
        val local = e.substring(0, arroba)
        val dominio = e.substring(arroba + 1)
        if (local.length > 64 || local.startsWith(".") || local.endsWith(".") || local.contains("..")) return null
        if (!LOCAL.matches(local)) return null
        val rotulos = dominio.split('.')
        if (rotulos.size < 2 || rotulos.any { !ROTULO.matches(it) }) return null
        if (!TLD.matches(rotulos.last())) return null
        return e
    }

    /** Conta pontos de código, como o Worker (um emoji é UM caractere). */
    fun senhaValida(senha: String): Boolean = senha.codePointCount(0, senha.length) in SENHA_MIN..SENHA_MAX

    /** Validação local antes de gastar a rede; null = pode enviar. */
    fun validar(email: String, senha: String): ErroConta? = when {
        normalizarEmail(email) == null -> ErroConta.EMAIL
        !senhaValida(senha) -> ErroConta.SENHA
        else -> null
    }

    /** Código do servidor (`{error}`) + HTTP → o erro que a tela mostra. */
    fun erroDoServidor(status: Int, codigo: String?): ErroConta = when {
        status == 429 || codigo == "muitas_tentativas" -> ErroConta.LIMITE
        codigo == "email_em_uso" || status == 409 -> ErroConta.EM_USO
        codigo == "email_invalido" -> ErroConta.EMAIL
        codigo == "senha_curta" || codigo == "senha_longa" || codigo == "senha_invalida" -> ErroConta.SENHA
        status == 401 -> ErroConta.CREDENCIAIS
        else -> ErroConta.SERVICO
    }

    /** O estado depois de revalidar. Só um 401 tira alguém de dentro. */
    fun aposRevalidar(atual: ContaEstado, resultado: Revalidacao): ContaEstado = when {
        atual !is ContaEstado.Dentro -> atual
        resultado == Revalidacao.INVALIDA -> ContaEstado.Fora
        else -> atual
    }

    /** Revalida de novo quando a última conferida passou de 6 h (ou nunca houve). */
    fun precisaRevalidar(ultimaMs: Long, agoraMs: Long): Boolean = ultimaMs <= 0L || agoraMs - ultimaMs >= 6 * 3600_000L
}
