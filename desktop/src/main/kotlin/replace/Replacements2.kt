@file:Suppress("unused", "UNUSED_PARAMETER")

package com.aurea.aurea.conta

import android.app.Activity

/**
 * Login com Google no PC: o Credential Manager do Android não existe no
 * Windows. Sem ele o botão do Google some (como no Android sem configuração);
 * a conta Aurea por e-mail e senha continua igual.
 */
object GoogleLogin {
    val disponivel: Boolean get() = false

    sealed interface Resultado {
        data class Token(val idToken: String) : Resultado
        data object Cancelado : Resultado
        data class Falhou(val motivo: String) : Resultado
    }

    suspend fun entrar(activity: Activity): Resultado = Resultado.Falhou("pc")
}
