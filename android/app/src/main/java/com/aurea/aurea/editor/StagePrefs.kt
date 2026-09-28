package com.aurea.aurea.editor

import android.content.Context
import android.content.SharedPreferences
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue

/**
 * Preferências do PALCO, do aparelho (não do projeto).
 *
 * [hideSelectionBox]: a caixa da camada escolhida, o centro e as setas dos
 * eixos somem do preview — a camada continua escolhida e o dedo continua
 * movendo, girando e escalando por gesto; só o desenho (e as alças) sai da
 * frente de quem está editando algo pequeno por baixo dela.
 */
internal object StagePrefs {
    private const val FILE = "aurea.palco"
    private const val KEY_HIDE_BOX = "esconder_caixa_selecao"
    private var prefs: SharedPreferences? = null

    var hideSelectionBox by mutableStateOf(false)
        private set

    fun load(context: Context) {
        if (prefs != null) return
        val p = context.applicationContext.getSharedPreferences(FILE, Context.MODE_PRIVATE)
        prefs = p
        hideSelectionBox = p.getBoolean(KEY_HIDE_BOX, false)
    }

    fun toggleSelectionBox() {
        hideSelectionBox = !hideSelectionBox
        prefs?.edit()?.putBoolean(KEY_HIDE_BOX, hideSelectionBox)?.apply()
    }
}
