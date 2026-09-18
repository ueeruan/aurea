package com.aurea.aurea

import android.view.Surface

/**
 * A PONTE DA JANELA NATIVA PARA O RENDERCORE C++.
 *
 * POR QUE JNI, E NAO FFI. O `ANativeWindow` nasce de um `android.view.Surface`
 * e `ANativeWindow_fromSurface` exige um `JNIEnv*` — o `dart:ffi` nao tem
 * como fornecer um, porque nao existe handle de JVM no mundo do Dart.
 *
 * A DIVISAO E ESSA DE PROPOSITO: a JANELA atravessa por JNI, uma vez, quando
 * a superficie nasce; TUDO O RESTO (apresentar quadro, redimensionar, ler
 * estado) atravessa por FFI, no caminho quente, onde o custo importa.
 *
 * A BIBLIOTECA E CARREGADA AQUI. O `dart:ffi` a carrega sozinho pelo
 * asset; este `loadLibrary` e para o lado Kotlin poder chamar os metodos
 * nativos ANTES de o Dart existir — que e exatamente o caso do
 * `onSurfaceCreated`, que pode disparar na criacao da atividade.
 */
object RenderNativo {

    @Volatile
    private var carregada = false

    private fun garantirBiblioteca(): Boolean {
        if (carregada) return true
        return try {
            System.loadLibrary("aurea_render")
            carregada = true
            true
        } catch (e: UnsatisfiedLinkError) {
            // NAO DERRUBA O APP. Se a biblioteca nao carregar, o preview
            // simplesmente continua sendo o do Flutter — degradar e melhor
            // do que nao abrir.
            false
        }
    }

    /** Devolve true quando a superficie Vulkan subiu. */
    fun anexar(superficie: Surface, largura: Int, altura: Int): Boolean {
        if (!garantirBiblioteca()) return false
        return try {
            anexarNativo(superficie, largura, altura) != 0
        } catch (e: Throwable) {
            false
        }
    }

    fun desanexar() {
        if (!carregada) return
        try {
            desanexarNativo()
        } catch (e: Throwable) {
        }
    }

    fun estado(): String {
        if (!garantirBiblioteca()) return "estado=-1 motivo=biblioteca ausente"
        return try {
            estadoNativo() ?: "estado=-1"
        } catch (e: Throwable) {
            "estado=-1 motivo=excecao no JNI"
        }
    }

    // OS NOMES NATIVOS SAO SEPARADOS DOS WRAPPERS. Um metodo Kotlin e um
    // simbolo JNI com o MESMO nome: se o wrapper `anexar` fosse o externo,
    // o `System.loadLibrary` procuraria um simbolo que nao existe e o
    // `UnsatisfiedLinkError` so apareceria na primeira superficie criada.
    @JvmStatic
    external fun anexarNativo(superficie: Surface, largura: Int, altura: Int): Int

    @JvmStatic
    external fun desanexarNativo()

    @JvmStatic
    external fun estadoNativo(): String?
}
