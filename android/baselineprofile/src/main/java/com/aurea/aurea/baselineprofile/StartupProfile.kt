package com.aurea.aurea.baselineprofile

import androidx.benchmark.macro.junit4.BaselineProfileRule
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

/**
 * 0.5.3: percurso que gera o perfil de base do Aurea. NÃO LIGADO AO BUILD e nunca
 * executado; ver o cabeçalho de `android/baselineprofile/build.gradle.kts`.
 *
 * Cobre a abertura fria até a primeira tela desenhada e o motor pronto. Abrir um
 * projeto e mexer na timeline ficam para quando houver um caminho sem conta no
 * build de medição: o release pede login.
 */
@RunWith(AndroidJUnit4::class)
class StartupProfile {
    @get:Rule
    val rule = BaselineProfileRule()

    @Test
    fun coldStart() = rule.collect(
        packageName = "com.aurea.aurea",
        includeInStartupProfile = true,
    ) {
        pressHome()
        startActivityAndWait()
        // O motor sobe numa thread própria depois do primeiro quadro: espera a
        // tela assentar para as classes desse trecho entrarem no perfil.
        device.waitForIdle(5_000)
    }
}
