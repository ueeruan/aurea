package com.aurea.aurea.conta

import android.app.Application
import android.content.Context
import android.os.SystemClock
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.aurea.aurea.captions.KeyVault
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/**
 * A conta obrigatória do app: quem está dentro, o número de cadastrados e a
 * tela de cadastro/entrada. A MainActivity e a Home leem o MESMO objeto (o
 * ViewModel da Activity).
 *
 * O que fica no aparelho: o token de sessão e o e-mail, cifrados pelo
 * [KeyVault] (AES-GCM com chave do Android Keystore). A senha nunca é guardada.
 * O número de cadastrados fica num SharedPreferences comum (não é segredo).
 */
class ContaViewModel(app: Application) : AndroidViewModel(app) {

    private val cofre = KeyVault(app)
    private val prefs = app.getSharedPreferences(PREFS, Context.MODE_PRIVATE)

    var estado: ContaEstado by mutableStateOf(ContaLogica.inicial(sessao()))
        private set

    /** Pessoas cadastradas (null até a primeira resposta do servidor). */
    var usuarios: Int? by mutableStateOf(prefs.getInt(KEY_USUARIOS, -1).takeIf { it >= 0 })
        private set

    var ocupado by mutableStateOf(false)
        private set

    var erro: ErroConta? by mutableStateOf(null)

    /** false = cadastro (a primeira abertura), true = entrar. */
    var entrando by mutableStateOf(false)

    private var ultimaRevalidacao = 0L
    private var revogando = false

    val logado: Boolean get() = estado is ContaEstado.Dentro

    /** A sessão guardada, ou null. Lê o cofre (rápido; o Keystore já está de pé). */
    fun sessao(): SessaoGuardada? {
        val token = cofre.get(KEY_TOKEN) ?: return null
        val email = cofre.get(KEY_EMAIL) ?: return null
        return SessaoGuardada(token, email)
    }

    /** Na abertura: número de cadastrados e revalidação da sessão (com rede). */
    fun aoAbrir() {
        atualizarUsuarios()
        revalidarSeVencido()
    }

    fun atualizarUsuarios() {
        viewModelScope.launch {
            val r = withContext(Dispatchers.IO) { ContaApi.usuarios() }
            val n = r.corpo.optInt("count", -1)
            if (r.ok && n >= 0) guardarUsuarios(n)
        }
    }

    private fun guardarUsuarios(n: Int) {
        usuarios = n
        prefs.edit().putInt(KEY_USUARIOS, n).apply()
    }

    /**
     * Confere a sessão no servidor, no máximo a cada 6 h. Sem rede ou com o
     * servidor fora, quem já entrou CONTINUA dentro; só um 401 derruba.
     */
    fun revalidarSeVencido() {
        revogarPendentes()
        val guardada = sessao() ?: return
        val agora = SystemClock.elapsedRealtime()
        if (!ContaLogica.precisaRevalidar(ultimaRevalidacao, agora)) return
        ultimaRevalidacao = agora
        viewModelScope.launch {
            val resultado = ContaApi.revalidacao(withContext(Dispatchers.IO) { ContaApi.validar(guardada.token) })
            if (sessao()?.token != guardada.token) return@launch
            if (resultado == Revalidacao.SEM_RESPOSTA) ultimaRevalidacao = 0L   // tenta de novo na próxima volta
            val novo = ContaLogica.aposRevalidar(estado, resultado)
            if (novo is ContaEstado.Fora && estado is ContaEstado.Dentro) {
                withContext(Dispatchers.IO) { apagarSessao() }
                entrando = true
                erro = ErroConta.EXPIRADA
            }
            estado = novo
        }
    }

    /** Cadastro ou entrada, conforme [entrando]. A senha só vive nesta chamada. */
    fun enviar(emailDigitado: String, senha: String) {
        if (ocupado) return
        ContaLogica.validar(emailDigitado, senha)?.let { erro = it; return }
        val email = ContaLogica.normalizarEmail(emailDigitado) ?: return
        val cadastro = !entrando
        ocupado = true
        erro = null
        viewModelScope.launch {
            val r = withContext(Dispatchers.IO) {
                if (cadastro) ContaApi.cadastrar(email, senha) else ContaApi.entrar(email, senha)
            }
            if (r.status == 0) { ocupado = false; erro = ErroConta.REDE; return@launch }
            if (!r.ok) {
                ocupado = false
                val e = ContaLogica.erroDoServidor(r.status, r.codigo)
                erro = e
                if (e == ErroConta.EM_USO) entrando = true
                return@launch
            }
            val token = r.corpo.optString("token")
            val confirmado = r.corpo.optString("email").ifBlank { email }
            val gravou = token.length == 43 && withContext(Dispatchers.IO) {
                runCatching { cofre.put(KEY_TOKEN, token); cofre.put(KEY_EMAIL, confirmado) }.isSuccess && sessao() != null
            }
            ocupado = false
            if (!gravou) { erro = ErroConta.SERVICO; return@launch }
            r.corpo.optInt("users", -1).takeIf { it >= 0 }?.let { guardarUsuarios(it) }
            ultimaRevalidacao = SystemClock.elapsedRealtime()
            estado = ContaEstado.Dentro(confirmado)
        }
    }

    /** Sai: apaga o token local na hora; avisa o servidor quando der. */
    fun sair() {
        val guardada = sessao()
        if (guardada != null) {
            val fila = pendentes().toMutableSet().apply { add(guardada.token) }
            if (runCatching { cofre.put(KEY_REVOGAR, fila.joinToString("\n")) }.isFailure) {
                viewModelScope.launch(Dispatchers.IO) { ContaApi.sair(guardada.token) }
            }
        }
        apagarSessao()
        estado = ContaEstado.Fora
        entrando = true
        erro = null
        revogarPendentes()
    }

    private fun pendentes(): List<String> = cofre.get(KEY_REVOGAR).orEmpty().lineSequence()
        .filter { it.matches(Regex("[A-Za-z0-9_-]{43}")) }.distinct().toList()

    /** Offline logout remains queued in the encrypted vault until the server confirms it. */
    private fun revogarPendentes() {
        if (revogando) return
        val fila = pendentes()
        if (fila.isEmpty()) return
        revogando = true
        viewModelScope.launch {
            try {
                for (token in fila) {
                    val r = withContext(Dispatchers.IO) { ContaApi.sair(token) }
                    if (r.ok || r.status == 401) {
                        val restam = pendentes().filter { it != token }
                        runCatching {
                            if (restam.isEmpty()) cofre.remove(KEY_REVOGAR)
                            else cofre.put(KEY_REVOGAR, restam.joinToString("\n"))
                        }
                    }
                }
            } finally { revogando = false }
        }
    }

    private fun apagarSessao() {
        cofre.remove(KEY_TOKEN)
        cofre.remove(KEY_EMAIL)
    }

    companion object {
        private const val PREFS = "aurea_conta"
        private const val KEY_USUARIOS = "usuarios"
        private const val KEY_TOKEN = "conta_token"
        private const val KEY_EMAIL = "conta_email"
        private const val KEY_REVOGAR = "conta_revogar"
    }
}
